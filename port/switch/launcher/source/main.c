/*
 * Tenkaichi3Decomp's launcher for the Nintendo Switch (docs/port/switch_plan.md, "The launcher").
 *
 * The game's pointers are 4 bytes wide: the program and everything it points at lie in one 4 GB window at a fixed
 * place in the address space (port/src/port_addr.h), and the program is linked to run at window + 0x20000000
 * (port/tools/link.py, link_nx). Horizon loads a title's program where it chooses, so this small program loads the
 * game instead, in the manner of nx-hbloader (the homebrew loader, whose way of handing a program its environment is
 * followed here):
 *
 *   1. finds the game's folder: next to this launcher, or the title's folder sdmc:/atmosphere/contents/<title>/;
 *   2. chooses a free 4 GB window: the one the program was linked for, or another when the system has put one of its
 *      regions there this time (it places them anew at every start);
 *   3. maps the program's segments (an ELF: `Tenkaichi3Decomp`) with svcMapProcessCodeMemory and
 *      svcSetProcessMemoryPermission, from memory of this process's heap, moved with their window as a whole: the
 *      8-byte addresses <program>.rel lists are moved with them (port/tools/nx_reloc.py), the rest needs nothing;
 *   4. gives the system's services back (the game opens its own), starts the program with the homebrew ABI (its
 *      heap: the rest of this process's heap; the process handle; the allowed calls; argv[0], its own path, which
 *      makes the game's folder its working folder), and when it ends takes the services again and shows how it ended
 *      with the end of its log (bt3_log.txt).
 *
 * The game maps its own memory regions inside the window (port/src/plat_mem.c). Built as an .nro to start from the
 * homebrew menu (port/switch/launcher/build.sh); later the same program is the NSP's (phase 5 of the plan).
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define TITLE_FOLDER "sdmc:/atmosphere/contents/0100B73DEC000000"
#define PROGRAM "Tenkaichi3Decomp"
#define WINDOW 0x4000000000ull /* the window programs are linked for (port/tools/toolchain.py NX_WINDOW): tried first */
#define GB4 0x100000000ull
#define LAUNCHER_HEAP 0x1000000ull  /* this program's own C heap (the console's picture is the most of it) */
#define PATH_SIZE (FS_MAX_PATH + 64) /* a folder of FS_MAX_PATH and a name in it */
#define MIN_GAME_HEAP 0x10000000ull /* what the game needs at least besides its program (its regions: 240 MB) */

/* ---- memory ------------------------------------------------------------------------------------------------ */

/* The process's heap: the first LAUNCHER_HEAP are this program's C heap, the rest is for the game (its program's
   segments are copied there and mapped, then what follows is the game's heap). Under the homebrew menu that is the
   heap the homebrew loader hands over; as a title's program (the NSP), the whole heap the process may have. */
static uint8_t *sBig;
static uint64_t sBigSize;

void __libnx_initheap(void) {
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    void *addr = NULL;
    uint64_t size = 0, total = 0, used = 0;

    if (envHasHeapOverride()) {
        addr = envGetHeapOverrideAddr();
        size = envGetHeapOverrideSize();
    } else {
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        if (total > used + 0x200000) {
            size = (total - used - 0x200000) & ~0x1FFFFFull;
        }
        if (R_FAILED(svcSetHeapSize(&addr, size))) {
            diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
        }
    }
    fake_heap_start = (char *)addr;
    fake_heap_end = (char *)addr + LAUNCHER_HEAP;
    sBig = (uint8_t *)addr + LAUNCHER_HEAP;
    sBigSize = size > LAUNCHER_HEAP ? size - LAUNCHER_HEAP : 0;
}

/* The process's own handle, which svcMapProcessCodeMemory needs. The homebrew loader hands it over; a title's program
   asks itself for it by sending it to itself over a session, as nx-hbloader does. */
static Handle sProcess = INVALID_HANDLE;

static void receive_handle(void *arg) {
    Handle session = (Handle)(uintptr_t)arg;
    void *base = armGetTls();
    s32 idx = 0;

    hipcMakeRequestInline(base);
    if (R_SUCCEEDED(svcReplyAndReceive(&idx, &session, 1, INVALID_HANDLE, UINT64_MAX))) {
        HipcParsedRequest r = hipcParseRequest(base);
        if (r.meta.num_copy_handles == 1) {
            sProcess = r.data.copy_handles[0];
        }
    }
    svcCloseHandle(session);
}

static Handle own_process(void) {
    Handle server, client;
    Thread t;

    if (envGetOwnProcessHandle() != INVALID_HANDLE) {
        return envGetOwnProcessHandle();
    }
    if (sProcess == INVALID_HANDLE && R_SUCCEEDED(svcCreateSession(&server, &client, 0, 0))) {
        if (R_SUCCEEDED(threadCreate(&t, receive_handle, (void *)(uintptr_t)server, NULL, 0x1000, 0x20, -2))) {
            threadStart(&t);
            hipcMakeRequestInline(armGetTls(), .num_copy_handles = 1).copy_handles[0] = CUR_PROCESS_HANDLE;
            svcSendSyncRequest(client);
            threadWaitForExit(&t);
            threadClose(&t);
        }
        svcCloseHandle(client);
    }
    return sProcess;
}

/* ---- the screen ------------------------------------------------------------------------------------------- */

static PadState sPad;

static void screen_open(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&sPad);
}

/* Waits for + (then the launcher ends: back to the homebrew menu or the HOME menu). */
static void wait_plus(void) {
    printf("\nPress + to exit.\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        padUpdate(&sPad);
        if (padGetButtonsDown(&sPad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
}

static int fail(const char *fmt, ...) {
    va_list ap;

    printf("\n" CONSOLE_RED "Cannot start the game." CONSOLE_RESET "\n");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    wait_plus();
    return 1;
}

/* The last lines of the game's log (the program's output: port/src/plat_mem.c, nx_start). */
static void show_log_tail(const char *folder, int lines) {
    char path[PATH_SIZE], buf[4097];
    FILE *f;
    long n;
    char *p;

    snprintf(path, sizeof(path), "%s/bt3_log.txt", folder);
    f = fopen(path, "rb");
    if (f == NULL) {
        printf("(no bt3_log.txt)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, n > 4096 ? n - 4096 : 0, SEEK_SET);
    n = (long)fread(buf, 1, 4096, f);
    fclose(f);
    buf[n] = '\0';
    p = buf + n;
    while (p > buf && lines > 0) {
        p--;
        if (*p == '\n' && p + 1 < buf + n) {
            lines--;
        }
    }
    printf("--- the end of %s ---\n%s\n", path, p == buf ? p : p + 1);
}

/* ---- the game's program ------------------------------------------------------------------------------------- */

typedef struct {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ElfHeader;

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} ElfSegment;

typedef struct {
    uint64_t dst, src, size;
} Mapped;

static Mapped sSegs[8];
static int sSegCount;
static uint64_t sGameHeap, sGameHeapSize;
uint64_t gLauncherEntry; /* while the program runs: its start, where exceptions are sent (trampoline.s) */

/* Whether the 4 GB window at w is free from its start to its end: nothing mapped there, and none of the regions the
   system keeps for the heap, the alias mappings and the stacks (it refuses code mappings in those even where
   nothing is mapped). */
static int clear_of(uint64_t w, uint32_t idAddr, uint32_t idSize) {
    uint64_t a = 0, n = 0;
    svcGetInfo(&a, idAddr, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&n, idSize, CUR_PROCESS_HANDLE, 0);
    return n == 0 || w + GB4 <= a || w >= a + n;
}

static int window_free(uint64_t w) {
    MemoryInfo mi;
    u32 pi;

    if (!clear_of(w, InfoType_HeapRegionAddress, InfoType_HeapRegionSize) ||
        !clear_of(w, InfoType_AliasRegionAddress, InfoType_AliasRegionSize) ||
        !clear_of(w, InfoType_StackRegionAddress, InfoType_StackRegionSize)) {
        return 0;
    }
    if (R_FAILED(svcQueryMemory(&mi, &pi, w))) {
        return 0;
    }
    return mi.type == MemType_Unmapped && mi.addr + mi.size >= w + GB4;
}

/* The window the program goes into: the one it was linked for if it is free (the system places some of its regions
   anew at every start, and may have put one there), else the first free one in the address space. The program and
   its window are moved together by a multiple of 4 GB (the list of what moves with it: <program>.rel). 0: none. */
static uint64_t choose_window(void) {
    uint64_t start = 0, size = 0, w;

    if (window_free(WINDOW)) {
        return WINDOW;
    }
    svcGetInfo(&start, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&size, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
    for (w = (start + GB4 - 1) & ~(GB4 - 1); w + GB4 <= start + size; w += GB4) {
        if (w != 0 && window_free(w)) {
            return w;
        }
    }
    return 0;
}

/* The address space as the system has laid it out this time (when no window is free) */
static void show_layout(void) {
    static const struct { const char *what; uint32_t a, n; } kRegions[] = {
        {"address space", InfoType_AslrRegionAddress, InfoType_AslrRegionSize},
        {"heap region", InfoType_HeapRegionAddress, InfoType_HeapRegionSize},
        {"alias region", InfoType_AliasRegionAddress, InfoType_AliasRegionSize},
        {"stack region", InfoType_StackRegionAddress, InfoType_StackRegionSize},
    };
    MemoryInfo mi;
    u32 pi;
    unsigned i;

    for (i = 0; i < sizeof(kRegions) / sizeof(kRegions[0]); i++) {
        uint64_t a = 0, n = 0;
        svcGetInfo(&a, kRegions[i].a, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&n, kRegions[i].n, CUR_PROCESS_HANDLE, 0);
        printf("  %-14s %#" PRIx64 " + %#" PRIx64 "\n", kRegions[i].what, a, n);
    }
    if (R_SUCCEEDED(svcQueryMemory(&mi, &pi, WINDOW))) {
        printf("  at %#llx: type %#x, %#" PRIx64 " + %#" PRIx64 "\n", (unsigned long long)WINDOW, mi.type, mi.addr, mi.size);
    }
}

/* <program>.rel (port/tools/nx_reloc.py): the window the program was linked for, and the offsets in it of the 8-byte
   addresses that move with the program. */
static uint64_t sLinkedWindow;
static uint32_t *sRel;
static uint32_t sRelCount;

static const char *read_rel(const char *path) {
    char name[PATH_SIZE + 8];
    FILE *f;
    struct {
        char magic[4];
        uint32_t version;
        uint64_t window;
        uint32_t count;
    } __attribute__((packed)) h;

    snprintf(name, sizeof(name), "%s.rel", path);
    f = fopen(name, "rb");
    if (f == NULL) {
        return "its list Tenkaichi3Decomp.rel is missing (copy all the files of the game)";
    }
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "BT3R", 4) != 0 || h.version != 1 || h.count > 0x1000000 ||
        (sRel = malloc((size_t)h.count * 4 + 4)) == NULL || fread(sRel, 4, h.count, f) != h.count) {
        fclose(f);
        return "its list Tenkaichi3Decomp.rel is damaged";
    }
    fclose(f);
    sLinkedWindow = h.window;
    sRelCount = h.count;
    return NULL;
}

/* Maps the program's segments into the window `w`, moved from the window it was linked for. 0, or what went wrong. */
static const char *load(const char *path, Handle self, uint64_t w, uint64_t *entry) {
    FILE *f;
    ElfHeader h;
    ElfSegment seg[16];
    uint8_t *cursor = sBig;
    uint64_t end = 0, delta;
    const char *why = read_rel(path);
    int i;

    if (why != NULL) {
        return why;
    }
    delta = w - sLinkedWindow;
    f = fopen(path, "rb");
    if (f == NULL) {
        return "it cannot be opened";
    }
    /* (in pieces of 1 MB: the C library's own buffer is 1 KB, which made reading the program some 13,000 requests
       to the SD card, slowing the whole console while they lasted) */
    setvbuf(f, NULL, _IOFBF, 1 << 20);
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.ident, "\177ELF\2\1", 6) != 0 || h.machine != 183 /* AArch64 */ ||
        h.type != 2 /* executable */ || h.phentsize != sizeof(ElfSegment) || h.phnum == 0 || h.phnum > 16 ||
        fseek(f, (long)h.phoff, SEEK_SET) != 0 || fread(seg, sizeof(ElfSegment), h.phnum, f) != h.phnum) {
        fclose(f);
        return "it is not the Switch's program of Tenkaichi3Decomp (an ELF for AArch64)";
    }
    for (i = 0; i < h.phnum; i++) {
        uint64_t link, dst, pad, size, perm;
        uint32_t k;
        Result rc;

        if (seg[i].type != 1 /* PT_LOAD */ || seg[i].memsz == 0) {
            continue;
        }
        link = seg[i].vaddr & ~0xFFFull; /* where the link put it */
        pad = seg[i].vaddr - link;
        size = (pad + seg[i].memsz + 0xFFF) & ~0xFFFull;
        dst = link + delta;
        if (seg[i].filesz > seg[i].memsz || link < sLinkedWindow + 0x20000000ull || link + size > sLinkedWindow + GB4 ||
            dst < end || sSegCount == (int)(sizeof(sSegs) / sizeof(sSegs[0]))) {
            fclose(f);
            return "its segments are not where its list says (are the program and Tenkaichi3Decomp.rel of one release?)";
        }
        if ((uint64_t)(cursor - sBig) + size + MIN_GAME_HEAP > sBigSize) {
            fclose(f);
            return "there is not enough memory (start it through a game, holding R, not from the album)";
        }
        memset(cursor, 0, size);
        if (fseek(f, (long)seg[i].offset, SEEK_SET) != 0 || fread(cursor + pad, 1, seg[i].filesz, f) != seg[i].filesz) {
            fclose(f);
            return "it could not be read whole";
        }
        for (k = 0; k < sRelCount; k++) { /* its addresses into the window it goes to */
            uint64_t at = sLinkedWindow + sRel[k];
            if (at >= link && at + 8 <= link + size) {
                uint64_t v;
                memcpy(&v, cursor + (at - link), 8);
                v += delta;
                memcpy(cursor + (at - link), &v, 8);
            }
        }
        armDCacheFlush(cursor, size);
        perm = (seg[i].flags & 1) ? Perm_Rx : (seg[i].flags & 2) ? Perm_Rw : Perm_R;
        rc = svcMapProcessCodeMemory(self, dst, (uint64_t)(uintptr_t)cursor, size);
        if (R_SUCCEEDED(rc)) {
            sSegs[sSegCount].dst = dst;
            sSegs[sSegCount].src = (uint64_t)(uintptr_t)cursor;
            sSegs[sSegCount].size = size;
            sSegCount++;
            rc = svcSetProcessMemoryPermission(self, dst, size, perm);
        }
        if (R_FAILED(rc)) {
            static char msg[96];
            fclose(f);
            snprintf(msg, sizeof(msg), "a segment could not be mapped at %#" PRIx64 " (result %#x)", dst, rc);
            return msg;
        }
        if (perm == Perm_Rx) {
            armICacheInvalidate((void *)(uintptr_t)dst, size);
        }
        cursor += size;
        end = dst + size;
    }
    fclose(f);
    if (sSegCount == 0 || h.entry + delta != sSegs[0].dst) {
        return "it has no start where its code begins";
    }
    *entry = h.entry + delta;
    sGameHeap = (uint64_t)(uintptr_t)cursor;
    sGameHeapSize = sBigSize - (uint64_t)(cursor - sBig);
    return NULL;
}

/* ---- starting it -------------------------------------------------------------------------------------------- */

extern int Launcher_Call(const ConfigEntry *entries, uint64_t handle, uint64_t entry); /* trampoline.s */
extern u32 __nx_applet_type;
void __appInit(void);
void __appExit(void);

static char sArgv[PATH_SIZE + 4];
static char sNextPath[512], sNextArgv[2048];
static AccountUid sUserId;
static const char sLoaderInfo[] = "Tenkaichi3Decomp launcher";
static u8 sSavedTls[0x100];

static int run(Handle self, uint64_t entry) {
    ConfigEntry e[16];
    int k = 0, r;
    unsigned svc;
    uint64_t hints[3] = {0, 0, 0}, type = __nx_applet_type, flags = 0;

    for (svc = 0; svc < 0xC0; svc++) {
        if (envIsSyscallHinted(svc)) {
            hints[svc / 64] |= 1ull << (svc % 64);
        }
    }
    if (type == AppletType_Application) { /* (the program's libnx turns this back into Application) */
        type = AppletType_SystemApplication;
        flags = EnvAppletFlags_ApplicationOverride;
    }
    memset(e, 0, sizeof(e));
    e[k++] = (ConfigEntry){EntryType_MainThreadHandle, 0, {envGetMainThreadHandle(), 0}};
    e[k++] = (ConfigEntry){EntryType_ProcessHandle, 0, {self, 0}};
    e[k++] = (ConfigEntry){EntryType_AppletType, 0, {type, flags}};
    e[k++] = (ConfigEntry){EntryType_OverrideHeap, EntryFlag_IsMandatory, {sGameHeap, sGameHeapSize}};
    e[k++] = (ConfigEntry){EntryType_Argv, 0, {0, (uint64_t)(uintptr_t)sArgv}};
    e[k++] = (ConfigEntry){EntryType_NextLoadPath, 0, {(uint64_t)(uintptr_t)sNextPath, (uint64_t)(uintptr_t)sNextArgv}};
    e[k++] = (ConfigEntry){EntryType_LastLoadResult, 0, {0, 0}};
    e[k++] = (ConfigEntry){EntryType_SyscallAvailableHint, 0, {hints[0], hints[1]}};
    e[k++] = (ConfigEntry){EntryType_SyscallAvailableHint2, 0, {hints[2], 0}};
    e[k++] = (ConfigEntry){EntryType_RandomSeed, 0, {randomGet64(), randomGet64()}};
    e[k++] = (ConfigEntry){EntryType_UserIdStorage, 0, {(uint64_t)(uintptr_t)&sUserId, 0}};
    e[k++] = (ConfigEntry){EntryType_HosVersion, 0, {hosversionGet(), hosversionIsAtmosphere() ? 0x41544d4f53504852ull : 0}};
    e[k++] = (ConfigEntry){EntryType_EndOfList, 0, {(uint64_t)(uintptr_t)sLoaderInfo, sizeof(sLoaderInfo)}};

    /* The system's services are the program's while it runs (an application may hold its applet proxy only once):
       this program's are closed, its thread data kept aside (the program's libnx writes its own there), and both
       come back afterwards. */
    memcpy(sSavedTls, (u8 *)armGetTls() + 0x100, sizeof(sSavedTls));
    consoleExit(NULL);
    __appExit();
    gLauncherEntry = entry;
    r = Launcher_Call(e, (uint64_t)-1, entry);
    gLauncherEntry = 0;
    memcpy((u8 *)armGetTls() + 0x100, sSavedTls, sizeof(sSavedTls));
    __appInit();
    screen_open();
    return r;
}

/* The game's folder: next to this launcher if the program is there, else the title's folder. */
static int find_game(int argc, char **argv, char *folder, size_t size) {
    char path[PATH_SIZE];
    const char *slash = argc > 0 && argv[0] != NULL ? strrchr(argv[0], '/') : NULL;
    FILE *f;

    if (slash != NULL) {
        snprintf(folder, size, "%.*s", (int)(slash - argv[0]), argv[0]);
        snprintf(path, sizeof(path), "%s/" PROGRAM, folder);
        if ((f = fopen(path, "rb")) != NULL) {
            fclose(f);
            return 1;
        }
    }
    snprintf(folder, size, "%s", TITLE_FOLDER);
    snprintf(path, sizeof(path), "%s/" PROGRAM, folder);
    if ((f = fopen(path, "rb")) != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* A test run (bt3_env.txt sets the demo fight without a picture, phase 3's test kit): its end is shown */
static int test_run(const char *folder) {
    char path[PATH_SIZE], line[256];
    FILE *f;
    int test = 0;

    snprintf(path, sizeof(path), "%s/bt3_env.txt", folder);
    f = fopen(path, "r");
    while (f != NULL && fgets(line, sizeof(line), f) != NULL) {
        test |= strncmp(line, "BT3_GS=none", 11) == 0 || strncmp(line, "BT3_DEMO=", 9) == 0;
    }
    if (f != NULL) {
        fclose(f);
    }
    return test;
}

/* A crash ends the whole process (the game's report, port/src/plat_crash.c, then the system), so it is told at the
   next start: the report's first lines, then A starts the game anyway (the report kept as bt3_crash_prev.txt). */
static int crash_notice(const char *folder) {
    char path[PATH_SIZE], prev[PATH_SIZE], line[160];
    FILE *f;
    int n = 0;

    snprintf(path, sizeof(path), "%s/bt3_crash.txt", folder);
    f = fopen(path, "r");
    if (f == NULL) {
        return 1;
    }
    printf(CONSOLE_RED "The game crashed the last time." CONSOLE_RESET " Its report (%s):\n\n", path);
    while (n++ < 12 && fgets(line, sizeof(line), f) != NULL) {
        printf("  %s", line);
    }
    fclose(f);
    printf("\nPlease send that file. A: start the game anyway, +: exit.\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
        u64 down;
        padUpdate(&sPad);
        down = padGetButtonsDown(&sPad);
        if (down & HidNpadButton_Plus) {
            consoleExit(NULL);
            return 0;
        }
        if (down & HidNpadButton_A) {
            break;
        }
        consoleUpdate(NULL);
    }
    snprintf(prev, sizeof(prev), "%s/bt3_crash_prev.txt", folder);
    remove(prev);
    rename(path, prev);
    printf("\n");
    return 1;
}

int main(int argc, char **argv) {
    char folder[FS_MAX_PATH], path[PATH_SIZE];
    const char *why;
    uint64_t entry = 0, window, t0;
    Handle self;
    int r;

    screen_open();
    printf(CONSOLE_YELLOW "Tenkaichi3Decomp" CONSOLE_RESET " - launcher\n\n");
    consoleUpdate(NULL);
    if (!find_game(argc, argv, folder, sizeof(folder))) {
        return fail("The game's program was not found. It is looked for:\n"
                    "  next to this launcher, as " PROGRAM ",\n  and as " TITLE_FOLDER "/" PROGRAM "\n"
                    "Copy the game's folder to the SD card (docs/port/switch_plan.md).\n");
    }
    if (!crash_notice(folder)) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/" PROGRAM, folder);
    printf("Game: %s\n", path);
    self = own_process();
    if (self == INVALID_HANDLE || !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x73)) {
        return fail("This launcher may not map memory here (no process handle or system calls).\n"
                    "Start it from the homebrew menu of Atmosphere.\n");
    }
    window = choose_window();
    if (window == 0) {
        printf("\n" CONSOLE_RED "Cannot start the game." CONSOLE_RESET "\n"
               "No 4 GB window of the address space is free for the game's memory. The address space:\n");
        show_layout();
        printf("(A game with a small address space, 64 GB, cannot hold it: start the homebrew menu through\n"
               "another game, holding R. Please send a photo of this screen.)\n");
        wait_plus();
        return 1;
    }
    printf("Window: %#" PRIx64 "%s\n", window, window == WINDOW ? "" : " (the usual one was in use this time)");
    printf("Loading the game...\n");
    consoleUpdate(NULL);
    t0 = armGetSystemTick();
    virtmemLock();
    virtmemAddReservation((void *)window, GB4);
    virtmemUnlock();
    why = load(path, self, window, &entry);
    if (why != NULL) {
        return fail("%s: %s.\n", path, why);
    }
    snprintf(sArgv, sizeof(sArgv), "\"%s\"", path);
    printf("Loaded at %#" PRIx64 " (%d segments) in %" PRIu64 " ms; %" PRIu64 " MB for the game.\n", entry, sSegCount,
           (uint64_t)(armTicksToNs(armGetSystemTick() - t0) / 1000000u), sGameHeapSize >> 20);
    printf("\nThe game runs now. Its output goes to bt3_log.txt in its folder, and what it is doing to\n"
           "bt3_watch.txt.\n");
    consoleUpdate(NULL);

    t0 = armGetSystemTick();
    r = run(self, entry);
    if (r == 0 && !test_run(folder)) {
        consoleExit(NULL); /* (played and left with Exit game: straight back to the menu) */
        return 0;
    }
    printf(CONSOLE_YELLOW "Tenkaichi3Decomp" CONSOLE_RESET " - launcher\n\nThe game ended after %" PRIu64 " s, result %d.\n",
           (uint64_t)(armTicksToNs(armGetSystemTick() - t0) / 1000000000u), r);
    show_log_tail(folder, 24);
    /* The program's segments stay mapped: a thread of it may still be there until this process ends, now. */
    wait_plus();
    return 0;
}
