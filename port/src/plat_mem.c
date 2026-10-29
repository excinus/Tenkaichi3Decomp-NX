/*
 * Memory of the PC build.
 *
 * The game was written for a machine with 32 MB at fixed addresses and it stores pointers in 32-bit fields, so the
 * 32-bit PC build gives it memory at PS2 addresses:
 *   0x003BE730..0x02000000  the game heap. The game takes it with ONE malloc at start-up (Heap_Create,
 *                           0x1EFB000 minus the end of the menu overlay); Port_Malloc returns the address the
 *                           PS2's malloc returns (0x3BE730, read from a save state), so heap pointers have the
 *                           same values as on the console and memory can be compared with an emulator dump.
 *   0x10000000, 0x12000000  hardware registers (timers, DMA, GS): plain memory here, so reads give what was last
 *                           written (0 at start: "transfer finished").
 *   0x70000000              the 16 KB scratchpad.
 */
#include "port_host.h"
#include "port_addr.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAP_BASE 0x003BE000u  /* page that holds the first address */
#define HEAP_FIRST 0x003BE730u /* what the PS2's malloc returns for the game heap (gHeapStart in a PCSX2 save state) */
#define HEAP_END 0x02000000u

static uint32_t sHeapNext = HEAP_FIRST, sHeapEnd = HEAP_END;

/* The 64-bit builds (the game's pointers 4 bytes wide, ptr32.py) keep the port's blocks for the game in a region of
   their own (Port_LowAlloc); the 32-bit build uses the C library's. */
#if defined(__x86_64__) || defined(__APPLE__) || defined(__SWITCH__)
#define PORT_LOW_REGION 1
#else
#define PORT_LOW_REGION 0
#endif

/* How far the start got, for the crash report (plat_crash.c). */
const char *volatile gPortStage = "starting";

/* Where the game's own part of its stack ends (the stack the game's main() runs on). Saving and restoring the
   game's state covers the stack up to here. On Linux the thread library's own data for the thread lies above it
   (its control block and thread-local variables, the C library's per-thread memory cache among them), which a
   restore must leave alone. */
static uint8_t *sGameStackTop;

#ifdef _WIN32
#include <direct.h> /* chdir */
/* ------------------------------------------------------------------------------------------------------ Windows
   The same needs, with Windows' means (the 64-bit build only; its pointers are 4 bytes wide, see ptr32.py):
     - the program is linked at 0x20000000 and not relocated (port/tools/link.py);
     - the hardware-register areas and the scratchpad are reserved at their PS2 addresses, which a 64-bit Windows
       process leaves free;
     - the game heap: at its PS2 address if that is free, else anywhere below 2 GB. A Windows process has its first
       stack and heap in that low area, so the PS2 address is often taken; nothing in the game depends on it (only
       comparing memory with a console dump does), and Port_Malloc just starts from wherever the region is;
     - the game's stack: if the process's own stack is above 4 GB, main() runs on a stack reserved low. */
#include <windows.h>

/* Reserves `size` bytes somewhere between 0x30000000 and 2 GB (clear of the program at 0x20000000). */
static void *low_alloc(size_t size) {
    uintptr_t addr = 0x30000000u;
    MEMORY_BASIC_INFORMATION mbi;

    size = (size + 0xFFFF) & ~(size_t)0xFFFF;
    while (addr < 0x7F000000u && VirtualQuery((void *)addr, &mbi, sizeof(mbi)) != 0) {
        uintptr_t base = ((uintptr_t)mbi.BaseAddress + 0xFFFF) & ~(uintptr_t)0xFFFF;
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_FREE && base + size <= end && base + size <= 0x7F000000u) {
            void *p = VirtualAlloc((void *)base, size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE); /* (the heap may land here) */
            if (p != NULL) {
                return p;
            }
        }
        addr = end > addr ? end : addr + 0x10000;
    }
    return NULL;
}

static void map(uint32_t addr, uint32_t size, const char *what) {
    uint32_t base = addr & ~0xFFFFu; /* reservations start on 64 KB boundaries */

    if (VirtualAlloc((void *)(uintptr_t)base, size + (addr - base), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != (void *)(uintptr_t)base) {
        fprintf(stderr, "bt3: cannot reserve %s at 0x%08X (error %lu)\n", what, addr, (unsigned long)GetLastError());
        exit(2);
    }
}

static void map_heap(void) {
    uint32_t base = HEAP_BASE & ~0xFFFFu;
    /* (write tracking: gs/state.c asks which of the heap's pages were written; costs nothing until it does) */
    uint8_t *p = VirtualAlloc((void *)(uintptr_t)base, HEAP_END - base, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);

    if (p != NULL) {
        return; /* at the PS2's address */
    }
    p = low_alloc(HEAP_END - HEAP_BASE);
    if (p == NULL) {
        fprintf(stderr, "bt3: no room for the game heap (%u MB) below 2 GB\n", (HEAP_END - HEAP_BASE) >> 20);
        exit(2);
    }
    sHeapNext = (uint32_t)(uintptr_t)p + (HEAP_FIRST - HEAP_BASE);
    sHeapEnd = (uint32_t)(uintptr_t)p + (HEAP_END - HEAP_BASE);
}

/* The same search, reserving the address range only (the region of Port_LowAlloc commits its pages as it grows). */
static void *low_reserve(size_t size) {
    uintptr_t addr = 0x30000000u;
    MEMORY_BASIC_INFORMATION mbi;

    size = (size + 0xFFFF) & ~(size_t)0xFFFF;
    while (addr < 0x7F000000u && VirtualQuery((void *)addr, &mbi, sizeof(mbi)) != 0) {
        uintptr_t base = ((uintptr_t)mbi.BaseAddress + 0xFFFF) & ~(uintptr_t)0xFFFF;
        uintptr_t end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (mbi.State == MEM_FREE && base + size <= end && base + size <= 0x7F000000u) {
            void *p = VirtualAlloc((void *)base, size, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
            if (p != NULL) {
                return p;
            }
        }
        addr = end > addr ? end : addr + 0x10000;
    }
    return NULL;
}

extern int __real_main(int argc, char **argv);
extern void Port_CallOnStack(void (*fn)(void), void *top);
PORT_HOST static int sArgc = 0, sResult = 0; /* (the process's own arguments: an address on its stack, not game state) */
PORT_HOST static char **sArgv = NULL;

/* fn() with the stack pointer at `top` (Windows x64 calling convention: fn in rcx, top in rdx). */
__asm__(".text\n.globl Port_CallOnStack\nPort_CallOnStack:\n"
        "    pushq %rbp\n    movq %rsp, %rbp\n    movq %rdx, %rsp\n    subq $32, %rsp\n    callq *%rcx\n"
        "    movq %rbp, %rsp\n    popq %rbp\n    retq\n");

static void game_main(void) {
    sResult = __real_main(sArgc, sArgv);
}

int __wrap_main(int argc, char **argv) {
    enum { STACK_SIZE = 0x01000000 };
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    void **dealloc = (void **)((uint8_t *)tib + 0x1478); /* TEB.DeallocationStack */
    void *base = tib->StackBase, *limit = tib->StackLimit, *old = *dealloc;
    uint8_t *stack;

    sArgc = argc;
    sArgv = argv;
    if (getenv("BT3_SIGN_TEST") != NULL) { /* testing the build key's signature (gs/net_match.c): prints and ends */
        extern int Match_SignTest(void);
        return Match_SignTest();
    }
    if (getenv("BT3_CRASH_TEST") != NULL) { /* testing the crash report */
        *(volatile int *)(uintptr_t)8 = 0;
    }
    if (getenv("BT3_STACK_TEST") != NULL) { /* testing the move to a low stack where the process's own is low already */
        tib = (NT_TIB *)NtCurrentTeb();
    } else
    if ((uintptr_t)&stack < 0xFFF00000u) {
        sGameStackTop = (uint8_t *)__builtin_frame_address(0);
        gPortStage = "the game is running (on the process's own stack)";
        return __real_main(argc, argv); /* the process's own stack is low already */
    }
    gPortStage = "moving to a stack below 4 GB";
    stack = low_alloc(STACK_SIZE);
    if (stack == NULL) {
        fprintf(stderr, "bt3: no room for the game's stack below 2 GB\n");
        return 2;
    }
    tib->StackBase = stack + STACK_SIZE; /* the system checks these on exceptions and when the stack grows */
    tib->StackLimit = stack;
    *dealloc = stack;
    sGameStackTop = stack + STACK_SIZE;
    gPortStage = "the game is running (on a stack of its own below 4 GB)";
    Port_CallOnStack(game_main, stack + STACK_SIZE);
    tib->StackBase = base;
    tib->StackLimit = limit;
    *dealloc = old;
    return sResult;
}

#elif defined(__APPLE__)
/* -------------------------------------------------------------------------------------------------------- macOS
   The same regions, in the window of the game's addresses (port_addr.h): each at W + its PS2 address. The program
   itself starts 0x20000000 into the window (port/tools/link.py), its globals are game addresses like the rest.
     - pages are 16 KB on Apple Silicon: a region starts on the page that holds its first address;
     - mmap is given the address as a hint (no MAP_FIXED, which would replace whatever is there): macOS takes the
       hint when the range is free, and anything else is reported;
     - the game's main() runs on a stack in the window (the game stores addresses of its locals), on the main
       thread.
   The entry point of the program is __wrap_main (link.py: ld64 has no --wrap; the game's main() is called by
   name). */
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/mman.h>
#include <unistd.h>

/* The window's base for the game's code (port/tools/macwin/macwin.cpp loads it in every function). */
void *Port_Window = (char *)&_mh_execute_header - 0x20000000u;

/* The window starts on a 4 GB boundary (link.py places the program at 1 TB + 0x20000000), so that the low 32 bits of
   any address in it ARE the game address, as on Linux, where the two are equal: the game passes addresses as
   integers and integers as addresses through its function pointers (a task's argument is a `void *` to some and a
   character number to others), and those keep their 4 bytes. The system moves programs by a random amount at
   start (ASLR); when it has, the program starts itself again in the same process with that switched off, as a
   debugger does. */
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
static void window_aligned(void) {
    char path[4096];
    uint32_t size = sizeof(path);
    posix_spawnattr_t attr;

    if ((PORT_WINDOW & 0xFFFFFFFFu) == 0) {
        unsetenv("BT3_NO_ASLR");
        return;
    }
    if (getenv("BT3_NO_ASLR") == NULL && _NSGetExecutablePath(path, &size) == 0 && posix_spawnattr_init(&attr) == 0) {
        setenv("BT3_NO_ASLR", "1", 1);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR);
        posix_spawn(NULL, path, NULL, &attr, *_NSGetArgv(), *_NSGetEnviron()); /* (returns only on failure) */
    }
    fprintf(stderr, "bt3: the program was not loaded at the address it needs (window at %p) and could not be started again there\n",
            (void *)PORT_WINDOW);
    exit(4);
}

static void *map_at(uint32_t addr, uint32_t size) {
    uintptr_t page = (uintptr_t)getpagesize(), at = (PORT_WINDOW + addr) & ~(page - 1);
    size_t n = (size_t)((PORT_WINDOW + addr + size - at + page - 1) & ~(page - 1));
    void *p = mmap((void *)at, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

    if (p != (void *)at) {
        if (p != MAP_FAILED) {
            munmap(p, n);
        }
        return NULL;
    }
    return p;
}

static void map(uint32_t addr, uint32_t size, const char *what) {
    if (map_at(addr, size) == NULL) {
        fprintf(stderr, "bt3: cannot map %s at window + 0x%08X\n", what, addr);
        exit(2);
    }
}

static void map_heap(void) {
    map(HEAP_BASE, HEAP_END - HEAP_BASE, "the game heap");
}

#define STACK_BASE 0x02000000u
#define STACK_SIZE 0x01000000u
extern int main(int argc, char **argv);
extern void Port_CallOnStack(void (*fn)(void), void *top);
PORT_HOST static int sArgc = 0, sResult = 0; /* (the process's own arguments: an address on its stack, not game state) */
PORT_HOST static char **sArgv = NULL;

/* fn() with the stack pointer at `top`. The game runs on the main thread (Cocoa, and so SDL's windows and events,
   work there only), on a stack in the window. */
__asm__(".text\n.globl _Port_CallOnStack\n.p2align 2\n_Port_CallOnStack:\n"
        "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n    mov sp, x1\n    blr x0\n"
        "    mov sp, x29\n    ldp x29, x30, [sp], #16\n    ret\n");

static void game_main(void) {
    sResult = main(sArgc, sArgv);
}

int __wrap_main(int argc, char **argv) {
    sArgc = argc;
    sArgv = argv;
    sGameStackTop = (uint8_t *)Port_Ptr(STACK_BASE + STACK_SIZE);
    gPortStage = "the game is running (on its stack in the window)";
    Port_CallOnStack(game_main, sGameStackTop);
    return sResult;
}

#elif defined(__SWITCH__)
/* ------------------------------------------------------------------------------------------- Nintendo Switch
   The macOS model (above) on Horizon (docs/port/switch_plan.md): the launcher chose a free 4 GB window, loaded this
   program at window + 0x20000000 and left the rest of it free (PORT_WINDOW, port_addr.h).
     - Horizon has no mmap at a chosen address: a region is memory from the C library's heap, mapped at its place
       with svcMapProcessCodeMemory and made read-write with svcSetProcessMemoryPermission (what homebrew loaders do
       to place a program; checked on consoles by port/switch/spike). The calls are allowed to this program by the
       launcher's NPDM, and the launcher hands over the process handle (libnx: envGetOwnProcessHandle);
     - no pages taken as first used: a region is backed in full when it is mapped (the low arena's 192 MB too;
       the console gives an application about 3.2 GB);
     - pages are 4 KB; the memory is cleared, as mmap's is;
     - the game's main() runs on a stack in the window, on the main thread, as on macOS. */
#include <malloc.h> /* memalign */
#include <fcntl.h>  /* open */
#include <unistd.h> /* chdir, fsync, dup2 */
#include <switch.h>

/* The window's base for the game's code (port/tools/macwin/macwin.cpp loads it in every function): 0x20000000 below
   the program's first byte (__start__, libnx's linker script), wherever the launcher loaded it. (An 8-byte pointer
   in data: the launcher moves it with the program, port/tools/nx_reloc.py.) */
extern char __start__[];
void *Port_Window = __start__ - 0x20000000;

/* Before everything else (and before any getenv): the window reserved, and the Switch has neither a terminal nor
   environment variables.
   - the program's output (stdout and stderr) goes to bt3_log.txt in its folder, the one before kept as
     bt3_log_prev.txt, as on Windows (plat_crash.c);
   - bt3_env.txt in its folder, if there is one, sets the environment variables the PC builds take from the shell:
     one NAME=value per line, # for a comment (for tests, e.g. BT3_DEMO=1 and BT3_GS=none for the demo fight).
   The folder is the working folder: libnx starts there, the launcher naming the program by its full path. */
static void watch_start(void);

__attribute__((constructor(101))) static void nx_start(void) {
    FILE *f;
    char line[1024];

    /* the window is this program's: nothing libnx maps later (threads' stacks, shared memory) may go there */
    virtmemLock();
    virtmemAddReservation((void *)PORT_WINDOW, 0x100000000ull);
    virtmemUnlock();

    remove("bt3_log_prev.txt");
    rename("bt3_log.txt", "bt3_log_prev.txt");
    /* Opened once, as descriptors 1 and 2 (stdout's and stderr's): the Switch's file system lets only one handle
       write a file, so a second freopen of the same file failed and closed stderr, and everything the program
       wrote there (nearly all of its output) was lost from phase 4's first run on. */
    {
        int fd = open("bt3_log.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            fflush(stdout);
            fflush(stderr);
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
            setvbuf(stdout, NULL, _IOLBF, 0);
            setvbuf(stderr, NULL, _IOLBF, 0);
        }
    }
    watch_start();
    f = fopen("bt3_env.txt", "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq, *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
            *--end = '\0';
        }
        eq = strchr(line, '=');
        if (line[0] == '#' || eq == NULL || eq == line) {
            continue;
        }
        *eq = '\0';
        setenv(line, eq + 1, 1);
        fprintf(stderr, "bt3: bt3_env.txt: %s=%s\n", line, eq + 1);
    }
    fclose(f);
}

static void *map_at(uint32_t addr, uint32_t size) {
    uint64_t at = (PORT_WINDOW + addr) & ~(uint64_t)0xFFF, n = ((PORT_WINDOW + addr + size + 0xFFF) & ~(uint64_t)0xFFF) - at;
    Handle self = envGetOwnProcessHandle();
    void *src = memalign(0x1000, n);

    if (src == NULL) {
        return NULL;
    }
    if (R_FAILED(svcMapProcessCodeMemory(self, at, (uint64_t)(uintptr_t)src, n))) {
        free(src);
        return NULL;
    }
    if (R_FAILED(svcSetProcessMemoryPermission(self, at, n, Perm_Rw))) {
        if (R_SUCCEEDED(svcUnmapProcessCodeMemory(self, at, (uint64_t)(uintptr_t)src, n))) {
            free(src);
        }
        return NULL;
    }
    memset((void *)(uintptr_t)at, 0, n); /* (the heap's memory is not cleared; the game expects mmap's zeros) */
    return (void *)(uintptr_t)at;
}

static void map(uint32_t addr, uint32_t size, const char *what) {
    if (map_at(addr, size) == NULL) {
        fprintf(stderr, "bt3: cannot map %s at window + 0x%08X\n", what, addr);
        exit(2);
    }
}

static void map_heap(void) {
    map(HEAP_BASE, HEAP_END - HEAP_BASE, "the game heap");
}

#define STACK_BASE 0x02000000u
#define STACK_SIZE 0x01000000u
extern int __real_main(int argc, char **argv);
extern void Port_CallOnStack(void (*fn)(void), void *top);
PORT_HOST static int sArgc = 0, sResult = 0; /* (the process's own arguments, not game state) */
PORT_HOST static char **sArgv = NULL;

/* fn() with the stack pointer at `top` (as on macOS; ELF names) */
__asm__(".text\n.globl Port_CallOnStack\n.type Port_CallOnStack, %function\n.p2align 2\nPort_CallOnStack:\n"
        "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n    mov sp, x1\n    blr x0\n"
        "    mov sp, x29\n    ldp x29, x30, [sp], #16\n    ret\n");

static void game_main(void) {
    sResult = __real_main(sArgc, sArgv);
}

int __wrap_main(int argc, char **argv) {
    extern void Port_WatchStop(void), Port_ProfileStart(void);

    sArgc = argc;
    sArgv = argv;
    sGameStackTop = (uint8_t *)Port_Ptr(STACK_BASE + STACK_SIZE);
    gPortStage = "the game is running (on its stack in the window)";
    Port_ProfileStart();
    Port_CallOnStack(game_main, sGameStackTop);
    Port_WatchStop();
    return sResult;
}

/* The watch thread: started first of all (nx_start, before the game's data is read and its memory reserved), beside
   the game until it returns to the launcher. Twice a second:
   the log onto the card (fsync), so that what the game wrote is kept when it is ended from outside while it hangs;
   and every 2 seconds for the first 3 minutes, after that when nothing is drawn or the file service is called more
   than 2000 times a second, a few lines in bt3_watch.txt: the time, how late the thread woke (a busy processor), the
   frame counters, the start's stage, the file service's calls (plat_nxfs.c), any call that has not come back for a
   second, and the main thread's last call. bt3_watch.txt is written through libnx's file service directly, not the C
   library (whose locks a stuck thread of the game may hold): the first kits of phase 4 hung the console with nothing
   in the log, and in the one before this the watch thread, then started at main() and writing with fopen, did not
   get to write its first line. (Nothing goes to the log from this thread: the C library's stream has a lock.) */
extern size_t NxFs_Report(char *out, size_t cap, unsigned ms);
extern size_t NxFs_Stuck(char *out, size_t cap, unsigned ms);
extern size_t NxFs_MainLast(char *out, size_t cap);
extern size_t NxTrace_Report(char *out, size_t cap);
extern void NxTrace_SetMain(void);
extern size_t GsDraw_DecodeReport(char *out, size_t cap); /* gs/gs_draw.c */
extern unsigned long long NxFs_Total(void);
extern void NxFs_SetThreads(Handle mainThread, Handle watchThread);
extern Result __real_fsFsDeleteFile(FsFileSystem *fs, const char *p);
extern Result __real_fsFsRenameFile(FsFileSystem *fs, const char *p, const char *q);
extern Result __real_fsFsCreateFile(FsFileSystem *fs, const char *p, s64 size, u32 option);
extern Result __real_fsFsOpenFile(FsFileSystem *fs, const char *p, u32 mode, FsFile *out);
extern Result __real_fsFileWrite(FsFile *f, s64 off, const void *buf, u64 size, u32 option);
extern void __real_fsFileClose(FsFile *f);
static volatile int sWatchStop; /* the game has returned to the launcher */
static Thread sWatch, sProf;
static int sWatching, sProfiling, sLogFd[2] = {-1, -1};
static Handle sMainHandle;

/* The sampling profile (a thread of its own, nx_prof): about 250 times a second the game's thread is paused, where it
   is read (svcGetThreadContext3) and it goes on. Counted by address (an offset from the program's start, __start__:
   + 0x4020000000 in the build's bt3_nx.map): every sample over the last 30 s, and the samples taken while the game
   had not reached a vertical blank for 100 ms (a stall: the main menu's at each move, 1 to 3 s of "game code",
   2026-10-29), the address and the return address (lr: its caller) apart. In bt3_watch.txt. */
#define PROF_SLOTS 4096
enum { PROF_ALL, PROF_STALL, PROF_STALL_LR, PROF_TABLES };
static struct { uint32_t off, n; } sProfTab[PROF_TABLES][PROF_SLOTS];
static unsigned sProfCount[PROF_TABLES];
static Mutex sProfLock;

static void prof_add(int k, uint64_t addr) {
    extern char __start__[];
    uint64_t base = (uint64_t)(uintptr_t)__start__;
    uint32_t off = addr >= base && addr - base < 0xFFFFFFFFull ? (uint32_t)(addr - base) : 0xFFFFFFFFu, h = off * 2654435761u, i;
    for (i = 0; i < PROF_SLOTS; i++) {
        uint32_t s = (h + i) & (PROF_SLOTS - 1);
        if (sProfTab[k][s].n == 0 || sProfTab[k][s].off == off) {
            sProfTab[k][s].off = off;
            sProfTab[k][s].n++;
            break;
        }
    }
    sProfCount[k]++;
}

static void nx_prof(void *arg) {
    extern unsigned gPortVBlanks;
    unsigned blank = gPortVBlanks;
    u64 per = armGetSystemTickFreq() / 1000, changed = armGetSystemTick();

    (void)arg;
    while (!sWatchStop) {
        ThreadContext ctx;
        u64 now;
        int stalled;
        svcSleepThread(4000000ull);
        now = armGetSystemTick();
        if (gPortVBlanks != blank) {
            blank = gPortVBlanks;
            changed = now;
        }
        stalled = (now - changed) / per > 100;
        if (R_FAILED(svcSetThreadActivity(sMainHandle, ThreadActivity_Paused))) {
            continue;
        }
        if (R_SUCCEEDED(svcGetThreadContext3(&ctx, sMainHandle))) {
            svcSetThreadActivity(sMainHandle, ThreadActivity_Runnable);
            mutexLock(&sProfLock);
            prof_add(PROF_ALL, ctx.pc.x);
            if (stalled) {
                prof_add(PROF_STALL, ctx.pc.x);
                prof_add(PROF_STALL_LR, ctx.lr);
            }
            mutexUnlock(&sProfLock);
        } else {
            svcSetThreadActivity(sMainHandle, ThreadActivity_Runnable);
        }
    }
}

/* Table k's most frequent addresses as text into out, and the table emptied. The length written. */
static size_t prof_report(int k, const char *what, char *out, size_t cap) {
    size_t at = 0;
    int r;

    mutexLock(&sProfLock);
    if (sProfCount[k] != 0) {
        at += (size_t)snprintf(out + at, cap - at, "         %s: %u samples:", what, sProfCount[k]);
        for (r = 0; r < 16 && at < cap; r++) {
            uint32_t best = 0, i, bi = 0;
            for (i = 0; i < PROF_SLOTS; i++) {
                if (sProfTab[k][i].n > best) {
                    best = sProfTab[k][i].n;
                    bi = i;
                }
            }
            if (best == 0) {
                break;
            }
            at += (size_t)snprintf(out + at, cap - at, " %#x %u%%", sProfTab[k][bi].off, (unsigned)(best * 100u / sProfCount[k]));
            sProfTab[k][bi].n = 0; /* (taken: the next best next) */
        }
        if (at < cap) {
            at += (size_t)snprintf(out + at, cap - at, "\n");
        }
    }
    memset(sProfTab[k], 0, sizeof(sProfTab[k]));
    sProfCount[k] = 0;
    mutexUnlock(&sProfLock);
    return at < cap ? at : cap - 1;
}
static FsFile sWatchFile;
static s64 sWatchAt = -1; /* where the next line goes; -1: bt3_watch.txt could not be made */

/* bt3_watch.txt (the one before kept as bt3_watch_prev.txt), in the program's folder, by its path in the SD card's
   file system (sdmc:/atmosphere/contents/<title>/Tenkaichi3Decomp -> /atmosphere/contents/<title>/) */
static void watch_open(void) {
    extern int __system_argc;
    extern char **__system_argv;
    FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
    char dir[FS_MAX_PATH], now[FS_MAX_PATH], prev[FS_MAX_PATH];
    const char *p = __system_argc > 0 && __system_argv[0] != NULL ? __system_argv[0] : "", *colon = strchr(p, ':');
    char *slash;

    snprintf(dir, sizeof(dir), "%s", colon != NULL ? colon + 1 : p);
    slash = strrchr(dir, '/');
    if (fs == NULL || slash == NULL) {
        return;
    }
    slash[1] = '\0';
    snprintf(now, sizeof(now), "%sbt3_watch.txt", dir);
    snprintf(prev, sizeof(prev), "%sbt3_watch_prev.txt", dir);
    __real_fsFsDeleteFile(fs, prev);
    __real_fsFsRenameFile(fs, now, prev);
    if (R_SUCCEEDED(__real_fsFsCreateFile(fs, now, 0, 0)) &&
        R_SUCCEEDED(__real_fsFsOpenFile(fs, now, FsOpenMode_Write | FsOpenMode_Append, &sWatchFile))) {
        sWatchAt = 0;
    }
}

static void watch_write(const char *text, size_t n) {
    if (sWatchAt >= 0 && n > 0 && R_SUCCEEDED(__real_fsFileWrite(&sWatchFile, sWatchAt, text, n, FsWriteOption_Flush))) {
        sWatchAt += (s64)n;
    }
}

/* (times in milliseconds: this file is built with software float, which has no 64-bit integer to double) */
static void watch_line(const char *what, unsigned t, unsigned late, int report) {
    extern unsigned gGsFrame, gPortVBlanks;
    static char text[8192];
    size_t at;

    extern const char *volatile gPortWhere; /* plat_stub.c: where the game's thread is, roughly */
    at = (size_t)snprintf(text, sizeof(text), "%5u.%u s: %s; woke %u ms late; frame %u, blank %u; %s; the game's thread: %s\n",
                          t / 1000, t / 100 % 10, what, late, gGsFrame, gPortVBlanks, gPortStage, gPortWhere);
    if (report && at < sizeof(text)) {
        at += (size_t)snprintf(text + at, sizeof(text) - at, "         ");
        at += NxFs_Report(text + at, sizeof(text) - at, 2000);
        at += NxFs_Stuck(text + at, sizeof(text) - at, 1000);
        at += NxFs_MainLast(text + at, sizeof(text) - at);
        if (at + 9 < sizeof(text)) {
            memcpy(text + at, "         ", 9);
            at += 9 + GsDraw_DecodeReport(text + at + 9, sizeof(text) - at - 9);
        }
        at += NxTrace_Report(text + at, sizeof(text) - at);
        at += prof_report(PROF_STALL, "the game's thread in stalls (offsets from __start__)", text + at, sizeof(text) - at);
        at += prof_report(PROF_STALL_LR, "  and its return addresses then", text + at, sizeof(text) - at);
        if (report == 2) {
            at += prof_report(PROF_ALL, "the game's thread, the last 30 s (offsets from __start__)", text + at, sizeof(text) - at);
        }
    }
    watch_write(text, at < sizeof(text) ? at : sizeof(text) - 1);
}

static void nx_watch(void *arg) {
    extern unsigned gGsFrame, gPortVBlanks;
    unsigned frame = gGsFrame, blank = gPortVBlanks, ticks = 0, n = 0, late = 0;
    u64 freq = armGetSystemTickFreq() / 1000, start = armGetSystemTick(), was = start;
    unsigned long long before = 0;

    (void)arg;
    watch_open();
    watch_line("started", 0, 0, 0);
    while (!sWatchStop) {
        u64 now;
        unsigned t, slept;

        svcSleepThread(500000000ull);
        now = armGetSystemTick();
        t = (unsigned)((now - start) / freq);
        slept = (unsigned)((now - was) / freq);
        if (slept > 500 && slept - 500 > late) {
            late = slept - 500; /* (the most in this report's 2 seconds) */
        }
        was = now;
        if (sLogFd[0] >= 0) {
            fsync(sLogFd[0]);
        }
        if (sLogFd[1] >= 0) {
            fsync(sLogFd[1]);
        }
        if (gGsFrame != frame || gPortVBlanks != blank) {
            frame = gGsFrame;
            blank = gPortVBlanks;
            ticks = 0;
        } else {
            ticks++;
        }
        if (++n % 4 == 0) { /* (every 2 seconds) */
            unsigned long long total = NxFs_Total();
            if (t < 180000 || ticks >= 4 || total - before > 4000 || n % 60 == 0) {
                watch_line(ticks >= 4 ? "nothing drawn" : "running", t, late, n % 60 == 0 ? 2 : 1);
            } else {
                NxFs_Report(NULL, 0, 2000); /* (the counts start again for the next report) */
            }
            before = total;
            late = 0;
        }
    }
    watch_line("the game returned to the launcher", (unsigned)((armGetSystemTick() - start) / freq), 0, 0);
    if (sWatchAt >= 0) {
        __real_fsFileClose(&sWatchFile);
    }
}

/* From nx_start, once the log is set up. (Core 1: the game is on core 0, its sound on core 2; a priority above the
   C library's threads.) */
static void watch_start(void) {
    sLogFd[0] = 2; /* (stdout's and stderr's descriptors are the one file: nx_start) */
    NxTrace_SetMain(); /* (this is the game's thread: the constructors run on it, and main() after them) */
    if (R_SUCCEEDED(threadCreate(&sWatch, nx_watch, NULL, NULL, 0x10000, 0x2D, 1)) ||
        R_SUCCEEDED(threadCreate(&sWatch, nx_watch, NULL, NULL, 0x10000, 0x2D, -2)) ||
        R_SUCCEEDED(threadCreate(&sWatch, nx_watch, NULL, NULL, 0x10000, 0x3B, -2))) {
        NxFs_SetThreads(threadGetCurHandle(), sWatch.handle);
        sWatching = R_SUCCEEDED(threadStart(&sWatch));
    }

    if (!sWatching) {
        fprintf(stderr, "bt3: watch: the thread did not start\n");
    }
}

/* From main(), once bt3_env.txt has been read (BT3_PROFILE=0: no profile). */
void Port_ProfileStart(void) {
    sMainHandle = threadGetCurHandle();
    if (getenv("BT3_PROFILE") == NULL || atoi(getenv("BT3_PROFILE")) != 0) {
        if (R_SUCCEEDED(threadCreate(&sProf, nx_prof, NULL, NULL, 0x10000, 0x2D, 1)) ||
            R_SUCCEEDED(threadCreate(&sProf, nx_prof, NULL, NULL, 0x10000, 0x2D, -2))) {
            sProfiling = R_SUCCEEDED(threadStart(&sProf));
        }
    }
}

void Port_WatchStop(void) {
    if (sWatching) {
        sWatchStop = 1;
        threadWaitForExit(&sWatch);
        threadClose(&sWatch);
        sWatching = 0;
    }
    if (sProfiling) {
        sWatchStop = 1;
        threadWaitForExit(&sProf);
        threadClose(&sProf);
        sProfiling = 0;
    }
}

#else
/* -------------------------------------------------------------------------------------------------------- Linux */
#include <sys/mman.h>
#include <unistd.h>

static void map(uint32_t addr, uint32_t size, const char *what) {
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);

    if (p != (void *)(uintptr_t)addr) {
        fprintf(stderr, "bt3: cannot map %s at 0x%08X\n", what, addr);
        exit(2);
    }
}

static void map_heap(void) {
    map(HEAP_BASE, HEAP_END - HEAP_BASE, "the game heap");
}

#ifdef __x86_64__
/* The 64-bit build: the game's pointers are still 4 bytes wide (port/tools/ptr32.py), so everything the game can
   point at has to lie below 4 GB.
     - the program itself: linked at 0x20000000 (port/tools/link.py), clear of the regions above;
     - the C library's heap: kept in the break area that follows the program (no mmap, one arena), because
       handles the port gives to the game (files, sound players) come from it;
     - the stack of the thread that runs the game: the game stores addresses of local variables, so its main()
       runs on a thread whose stack is mapped right above the heap (the process's own stack is far above 4 GB).
       Below the program at 0x20000000 on purpose: the C library's own heap starts at a random place in the
       gigabyte ABOVE the program, and a fixed address there (this stack used to be at 0x60000000) is sometimes
       taken, which stopped the program at start now and then. */
#include <malloc.h>
#include <pthread.h>
#define STACK_BASE 0x02000000u
#define STACK_SIZE 0x01000000u
extern int __real_main(int argc, char **argv);
PORT_HOST static int sArgc = 0, sResult = 0; /* (the process's own arguments: an address on its stack, not game state) */
PORT_HOST static char **sArgv = NULL;

/* Where the game's own part of the thread's stack ends: above this frame lie the thread library's own data for
   the thread (its control block and thread-local variables are at the top of the stack it was given, among them
   the C library's per-thread memory cache). Saving and restoring the game's state must leave those alone. */
static void *game_thread(void *arg) {
    (void)arg;
    sGameStackTop = (uint8_t *)__builtin_frame_address(0);
    sResult = __real_main(sArgc, sArgv);
    return NULL;
}

int __wrap_main(int argc, char **argv) {
    pthread_attr_t attr;
    pthread_t th;

    sArgc = argc;
    sArgv = argv;
    if (getenv("BT3_SIGN_TEST") != NULL) { /* testing the build key's signature (gs/net_match.c): prints and ends */
        extern int Match_SignTest(void);
        return Match_SignTest();
    }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, (void *)(uintptr_t)STACK_BASE, STACK_SIZE);
    if (pthread_create(&th, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "bt3: cannot start the game thread\n");
        return 2;
    }
    pthread_join(th, NULL);
    return sResult;
}
#endif
#endif

/* ---- The game's own data, fetched from the user's disc (release builds).
   A program made by port/tools/strip_data.py has the data tables of the game's two programs and the VU1
   microprograms set to zero, and a list next to it (<program>.dat) of where each run of bytes comes from: the
   user's SLUS_216.78 (as a flat image from 0x100000) or BIN/DBZP.BIN. This copies them in before anything else
   runs. A program without that list still has the data linked in (the developer's build) and nothing happens. */
#define ROM_BASE 0x100000u
#define ROM_END 0x2FF180u

/* Set to 1 in the file by strip_data.py: this program does not work without its list. (2, not 0: it has to be in
   the file, not in the zero-filled part of memory.) */
volatile uint32_t gPortDataStripped = 2;

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)n;
    return buf;
}

static void data_fail(const char *what, const char *path) {
    fprintf(stderr, "bt3: %s%s%s\n     The game's data comes from your own disc image; run the setup (Tenkaichi3Decomp-setup) to unpack it.\n", what,
            path != NULL ? ": " : "", path != NULL ? path : "");
    exit(2);
}

static void Port_LoadGameData(void) {
    char exe[1024], path[1200];
    const char *root = getenv("BT3_DATA") != NULL ? getenv("BT3_DATA") : "gamedata";
    uint8_t *list, *elf, *src[2] = {NULL, NULL};
    size_t listSize = 0, elfSize = 0, srcSize[2] = {0, 0};
    uint64_t hash = 0xCBF29CE484222325ull, want;
    uint32_t count, i, k;
#ifdef _WIN32
    long n = (long)GetModuleFileNameA(NULL, exe, sizeof(exe) - 1);
#elif defined(__APPLE__)
    uint32_t exeSize = sizeof(exe) - 1;
    long n = _NSGetExecutablePath(exe, &exeSize) == 0 ? (long)strlen(exe) : -1;
#elif defined(__SWITCH__)
    /* the path the launcher started it with (sdmc:/atmosphere/contents/<title>/Tenkaichi3Decomp) */
    extern int __system_argc;
    extern char **__system_argv;
    long n = __system_argc > 0 && __system_argv[0] != NULL ? (long)snprintf(exe, sizeof(exe), "%s", __system_argv[0]) : -1;
#else
    long n = (long)readlink("/proc/self/exe", exe, sizeof(exe) - 1);
#endif

    if (n <= 0 || n >= (long)sizeof(exe) - 1) {
        return;
    }
    exe[n] = '\0';
    if (gPortDataStripped == 1 && getenv("BT3_DATA") == NULL) {
        /* A release program started from another folder (a double click, a shortcut): its data, saves and
           settings are next to it. If there is no gamedata here but there is next to the program, work there. */
        char dir[1024], probe[1200];
        char *slash;
        FILE *here = fopen("gamedata/disc/SLUS_216.78", "rb");
        if (here != NULL) {
            fclose(here);
        } else {
            snprintf(dir, sizeof(dir), "%s", exe);
            slash = strrchr(dir, '/');
#ifdef _WIN32
            if (strrchr(dir, '\\') > slash) {
                slash = strrchr(dir, '\\');
            }
#endif
            if (slash != NULL) {
                *slash = '\0';
                snprintf(probe, sizeof(probe), "%s/gamedata/disc/SLUS_216.78", dir);
                here = fopen(probe, "rb");
                if (here != NULL) {
                    fclose(here);
                    if (chdir(dir) != 0) {
                    }
                }
            }
        }
    }
    if (n > 4 && (strcmp(exe + n - 4, ".exe") == 0 || strcmp(exe + n - 4, ".EXE") == 0)) {
        exe[n - 4] = '\0'; /* Name.exe keeps its list in Name.dat */
    }
    snprintf(path, sizeof(path), "%s.dat", exe);
    list = read_file(path, &listSize);
    if (list == NULL) {
        if (gPortDataStripped == 1) {
            data_fail("this program needs the file that came with it", path);
        }
        return; /* no list: the data is linked in */
    }
    if (listSize < 16 || memcmp(list, "BT3D", 4) != 0) {
        data_fail("damaged file", path);
    }
    memcpy(&count, list + 4, 4);
    memcpy(&want, list + 8, 8);
    if (listSize < 16 + (size_t)count * 16) {
        data_fail("damaged file", path);
    }
    snprintf(path, sizeof(path), "%s/disc/SLUS_216.78", root);
    elf = read_file(path, &elfSize);
    if (elf == NULL || elfSize < 0x34 || memcmp(elf, "\177ELF", 4) != 0) {
        data_fail("the game's program was not found", path);
    }
    {   /* the loaded sections as one image from 0x100000 */
        uint32_t shoff, sh[6];
        uint16_t shentsize, shnum;
        memcpy(&shoff, elf + 0x20, 4);
        memcpy(&shentsize, elf + 0x2E, 2);
        memcpy(&shnum, elf + 0x30, 2);
        srcSize[0] = ROM_END - ROM_BASE;
        src[0] = calloc(1, srcSize[0]);
        for (i = 0; i < shnum && (size_t)shoff + (size_t)(i + 1) * shentsize <= elfSize; i++) {
            memcpy(sh, elf + shoff + (size_t)i * shentsize, sizeof(sh)); /* name, type, flags, addr, offset, size */
            if ((sh[2] & 2) && sh[1] != 8 && sh[5] != 0 && sh[3] >= ROM_BASE && sh[3] + sh[5] <= ROM_END &&
                (size_t)sh[4] + sh[5] <= elfSize) {
                memcpy(src[0] + (sh[3] - ROM_BASE), elf + sh[4], sh[5]);
            }
        }
        free(elf);
    }
    snprintf(path, sizeof(path), "%s/disc/BIN/DBZP.BIN", root);
    src[1] = read_file(path, &srcSize[1]);
    if (src[1] == NULL) {
        data_fail("the game's menu program was not found", path);
    }
    for (i = 0; i < count; i++) {
        uint32_t r[4]; /* address in this program, source, offset in it, length */
        const uint8_t *from;
        memcpy(r, list + 16 + (size_t)i * 16, 16);
        if (r[1] > 1 || (size_t)r[2] + r[3] > srcSize[r[1]]) {
            data_fail("damaged file (list of the game's data)", NULL);
        }
        from = src[r[1]] + r[2];
        memcpy(Port_Ptr(r[0]), from, r[3]);
        for (k = 0; k < r[3]; k++) {
            hash = (hash ^ from[k]) * 0x100000001B3ull;
        }
    }
    free(src[0]);
    free(src[1]);
    free(list);
    if (hash != want) {
        data_fail("the game's programs in your game data are not the unmodified USA release (SLUS-21678)", NULL);
    }
}

#if PORT_LOW_REGION
/* (between the GS registers at 0x12000000 and the program at 0x20000000: see the note at STACK_BASE) */
#define LOW_ARENA_BASE 0x13000000u
#define LOW_ARENA_SIZE 0x0C000000u /* address space only: pages are taken as they are first used */
enum { LOW_SIZES = 256 };
PORT_HOST static uint8_t *sLowArena = NULL; /* where the region is: fixed once it is taken, not game state */
static uint8_t *sLowNext;
#ifdef _WIN32
PORT_HOST static uint8_t *sLowCommitted = NULL; /* how far the region's pages are committed (never given back) */
#endif
static struct { uint64_t size; void *head; } sLowList[LOW_SIZES];
static int sLowSizes;
static volatile int sLowLock;
#endif

/* For saving and restoring the game's state (port/src/gs/state.c): the port's own memory that belongs to it. The
   blocks of Port_LowAlloc hold what the game was handed by address (file handles, sound buffers), and the
   allocator's own variables say which of them are in use. Also where the game thread's stack ends. */
uint32_t Port_LowRegionSize(void) {
#if PORT_LOW_REGION
    return LOW_ARENA_SIZE;
#else
    return 0;
#endif
}

void *Port_LowAlloc(size_t size);
void Port_LowFree(void *addr);

/* A large block for the game that does not come out of its own heap (28 MB, sized for the console): the buffer of
   a stage larger than the disc's (battle_load.c). From the port's region, which is below 4 GB (the game's
   pointers are 32 bits) and is part of every saved state. As an address, not a pointer: the game's files are
   compiled with 32-bit pointers and must not be handed a host function's pointer result. 0: not available
   (the 32-bit program, whose blocks are the C library's and cannot be told from the heap's by address). */
unsigned Port_GameBigAlloc(int size) {
#if PORT_LOW_REGION
    return Port_Addr(Port_LowAlloc((size_t)size));
#else
    (void)size;
    return 0;
#endif
}

/* 1 if `addr` was such a block (it is given back); 0: it is the game heap's (Heap_Free goes on). */
int Port_GameBigFree(unsigned addr) {
#if PORT_LOW_REGION
    if (sLowArena != NULL && addr >= Port_Addr(sLowArena) && addr - Port_Addr(sLowArena) < LOW_ARENA_SIZE) {
        Port_LowFree(Port_Ptr(addr));
        return 1;
    }
#else
    (void)addr;
#endif
    return 0;
}

int Port_StateExtra(void **p, size_t *n, int max) {
    int k = 0;
#if PORT_LOW_REGION
    if (max >= 4 && sLowArena != NULL) {
        p[k] = &sLowNext; n[k++] = sizeof(sLowNext);
        p[k] = sLowList; n[k++] = sizeof(sLowList);
        p[k] = &sLowSizes; n[k++] = sizeof(sLowSizes);
        p[k] = sLowArena; n[k++] = (size_t)(sLowNext - sLowArena);
    }
#else
    (void)p; (void)n; (void)max;
#endif
    return k;
}
uint8_t *Port_GameStackTop(void) {
#if PORT_LOW_REGION
    return sGameStackTop;
#else
    return NULL;
#endif
}

/* Where the game's heap is in this process (at its PS2 address unless that was taken) and how large. */
void Port_HeapRegion(uint8_t **base, uint32_t *size) {
    *base = (uint8_t *)Port_Ptr(sHeapEnd - (HEAP_END - HEAP_BASE));
    *size = HEAP_END - HEAP_BASE;
}

#if PORT_LOW_REGION
static void low_region_take(void);
#endif
__attribute__((constructor)) static void Port_MapMemory(void) {
#ifdef _WIN32
    /* The program only works at the address it was linked for (the game's pointers are 4 bytes and absolute). It
       carries no relocation table, so Windows has to load it there; should it be somewhere else all the same (a
       system set to move every program, "Mandatory ASLR", moved builds that still had the table: a crash at the
       first write, before anything was printed), say so instead of crashing. */
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x20000000u) {
        static const char msg[] = "Tenkaichi3Decomp was loaded at a different address than the one it needs (0x20000000) and cannot run.\n"
                                  "If \"Force randomization for images (Mandatory ASLR)\" is switched on in Windows Security > App & browser "
                                  "control > Exploit protection, switch it off for this program.\n";
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        DWORD done;
        if (err != NULL && err != INVALID_HANDLE_VALUE) {
            WriteFile(err, msg, sizeof(msg) - 1, &done, NULL);
        } else {
            MessageBoxA(NULL, msg, "Tenkaichi3Decomp", MB_OK | MB_ICONERROR);
        }
        ExitProcess(4);
    }
#endif
#if defined(__APPLE__)
    window_aligned();
#endif
    gPortStage = "reading the game's programs from gamedata";
    Port_LoadGameData();
    gPortStage = "reserving the game's memory";
#if defined(__APPLE__) || defined(__SWITCH__)
    map(STACK_BASE, STACK_SIZE, "the game thread's stack");
    low_region_take(); /* the region of Port_LowAlloc, now (see there) */
#elif defined(__x86_64__) && !defined(_WIN32)
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_ARENA_MAX, 1);
    map(STACK_BASE, STACK_SIZE, "the game thread's stack");
    low_region_take(); /* the region of Port_LowAlloc, now (see there) */
#endif
#if defined(__x86_64__) && defined(_WIN32)
    low_region_take();
#endif
    map_heap();
    map(0x10000000u, 0x10000, "the hardware registers");
    map(0x12000000u, 0x2000, "the GS registers");
    map(0x70000000u, 0x4000, "the scratchpad");
    gPortStage = "memory ready, before the program's main";
}

/* The game's malloc (include/port_compat.h renames it): a bump allocator over the heap region, 16-byte aligned. */
void *Port_Malloc(uint32_t size) {
    uint32_t p = (sHeapNext + 15) & ~15u;

    if (size > sHeapEnd - p) {
        fprintf(stderr, "bt3: malloc(0x%X) does not fit in the game heap\n", size);
        exit(2);
    }
    sHeapNext = p + size;
    return Port_Ptr(p);
}

/* Memory the port hands to the game by address (file handles, the "second processor's" memory): zeroed, and below
   4 GB in the 64-bit build, where the game keeps such an address in 4 bytes. The C library's malloc is not safe
   for this there: it moves to mappings far above 4 GB whenever the break area cannot grow. */
#if PORT_LOW_REGION
/* Takes the region of Port_LowAlloc. At the program's start (Port_MapMemory), before anything else can sit at its
   address and before any state is saved: a state restored to before the region existed would otherwise take it a
   second time, somewhere else. */
static void low_region_take(void) {
#ifdef _WIN32
        /* address space only (reserved); pages are committed as the region is used, below */
        sLowArena = VirtualAlloc((void *)(uintptr_t)LOW_ARENA_BASE, LOW_ARENA_SIZE, MEM_RESERVE | MEM_WRITE_WATCH, PAGE_READWRITE);
        if (sLowArena == NULL) {
            sLowArena = low_reserve(LOW_ARENA_SIZE);
        }
        if (sLowArena == NULL) {
            fprintf(stderr, "bt3: no room below 2 GB for the port's own blocks\n");
            exit(2);
        }
        sLowCommitted = sLowArena;
#elif defined(__APPLE__) || defined(__SWITCH__)
        sLowArena = map_at(LOW_ARENA_BASE, LOW_ARENA_SIZE); /* (macOS: pages are only taken as they are first used) */
        if (sLowArena == NULL) {
            fprintf(stderr, "bt3: the window has no room for the port's own blocks\n");
            exit(2);
        }
#else
        sLowArena = mmap((void *)(uintptr_t)LOW_ARENA_BASE, LOW_ARENA_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
        if (sLowArena == MAP_FAILED || sLowArena != (uint8_t *)(uintptr_t)LOW_ARENA_BASE) { /* taken: anywhere below 4 GB */
            sLowArena = mmap(NULL, LOW_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_32BIT, -1, 0);
        }
        if (sLowArena == MAP_FAILED) {
            fprintf(stderr, "bt3: no memory below 4 GB for the port's own blocks\n");
            exit(2);
        }
#endif
        sLowNext = sLowArena;
}

#endif

void *Port_LowAlloc(size_t size) {
#if PORT_LOW_REGION
    /* From one region at a fixed address, handed out in order, freed blocks reused by size (last freed first).
       The addresses end up in the game's memory (file handles, sound buffers); with the system choosing them they
       differed from run to run, and so did the game's state. This way they depend only on the order of the
       requests. */
    size_t total = (size + 16 + 255) & ~(size_t)255;
    uint64_t *p = NULL;
    int i;

    if (total > 4096) {
        total = (total + 4095) & ~(size_t)4095;
    }
    while (__sync_lock_test_and_set(&sLowLock, 1)) {
    }
    if (sLowArena == NULL) {
        low_region_take();
    }
    for (i = 0; i < sLowSizes; i++) {
        if (sLowList[i].size == total && sLowList[i].head != NULL) {
            p = sLowList[i].head;
            sLowList[i].head = *(void **)p;
            memset(p, 0, total);
            break;
        }
    }
    if (p == NULL) {
        if ((size_t)(sLowArena + LOW_ARENA_SIZE - sLowNext) < total) {
            fprintf(stderr, "bt3: no memory below 4 GB for %zu bytes\n", size);
            exit(2);
        }
        p = (uint64_t *)sLowNext;
        sLowNext += total;
#ifdef _WIN32
        if (sLowNext > sLowCommitted) { /* commit what is new, a megabyte at a time */
            size_t grow = ((size_t)(sLowNext - sLowCommitted) + 0xFFFFF) & ~(size_t)0xFFFFF;
            if ((size_t)(sLowArena + LOW_ARENA_SIZE - sLowCommitted) < grow) {
                grow = (size_t)(sLowArena + LOW_ARENA_SIZE - sLowCommitted);
            }
            if (VirtualAlloc(sLowCommitted, grow, MEM_COMMIT, PAGE_READWRITE) == NULL) {
                fprintf(stderr, "bt3: no memory for the port's own blocks\n");
                exit(2);
            }
            sLowCommitted += grow;
        }
#endif
        /* cleared: after the game's state was restored to an earlier moment, the memory above the restored end
           of the region still holds the blocks of the frames that were undone (a file handle with its read
           position, which made the re-run frame's read loop spin for ever) */
        memset(p, 0, total);
    }
    p[0] = total;
    __sync_lock_release(&sLowLock);
    return p + 2;
#else
    return calloc(1, size);
#endif
}

void Port_LowFree(void *addr) {
#if PORT_LOW_REGION
    if (addr != NULL) {
        uint64_t *p = (uint64_t *)addr - 2, total = p[0];
        int i;
        while (__sync_lock_test_and_set(&sLowLock, 1)) {
        }
        for (i = 0; i < sLowSizes && sLowList[i].size != total; i++) {
        }
        if (i == sLowSizes && sLowSizes < LOW_SIZES) {
            sLowList[sLowSizes].size = total;
            sLowList[sLowSizes].head = NULL;
            sLowSizes++;
        }
        if (i < sLowSizes) { /* (with the table of sizes full the block is simply not reused) */
            *(void **)p = sLowList[i].head;
            sLowList[i].head = p;
        }
        __sync_lock_release(&sLowLock);
    }
#else
    free(addr);
#endif
}
