/*
 * The Switch only (docs/port/switch_plan.md): the program's calls to the SD card's file service, counted and watched,
 * with a brake on them; and the C library's file buffers made larger.
 *
 * The game's first starts on a console stayed black, the whole console slow for about a minute, and nothing of the
 * log or of the watch thread's own file reached the card (the watch thread never got to write its first line). The
 * console's own menus use the same file service, and the C library's buffered reads ask it for 1 KB at a time
 * (newlib's buffer, whatever the file): a large file is thousands of requests.
 *
 * Every call of the file system C library (libnx's fsdev, under fopen, fread, stat, mkdir...) goes through the libnx
 * functions below; the link wraps them (--wrap, port/tools/link.py). For the watch thread (plat_mem.c):
 *   - each call is counted by kind, and the path most named is kept (NxFs_Report);
 *   - the calls under way are kept with when they began: one that does not come back shows with its path
 *     (NxFs_Stuck); and the main thread's last call (NxFs_MainLast);
 *   - past BRAKE_AT calls in a tenth of a second, every further call in that tenth waits a millisecond first (the
 *     watch thread's own excepted): the console stays usable and the program goes on, slower.
 * fopen is wrapped too: each file gets a buffer of BUFFER bytes, not the C library's 1 KB.
 */
#ifdef __SWITCH__
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define BRAKE_AT 400 /* calls in a tenth of a second (4000 a second) */
#define PATHS 16
#define FLYING 16
#define BUFFER (32 * 1024)

enum { K_OPEN, K_CREATE, K_DELETE, K_MKDIR, K_RENAME, K_TYPE, K_OPENDIR, K_TIME, K_READ, K_WRITE, K_FLUSH, K_SIZE,
       K_SETSIZE, K_CLOSE, K_DIRREAD, K_DIRCLOSE, K_KINDS };
static const char *const kKind[K_KINDS] = {"open", "create", "delete", "mkdir", "rename", "entry type", "open dir",
                                           "time stamp", "read", "write", "flush", "get size", "set size", "close",
                                           "read dir", "close dir"};

static Mutex sLock;
static unsigned sCount[K_KINDS], sMain, sBraked, sOpenFiles, sOpenDirs;
static unsigned long long sTotal;
static struct { char path[160]; unsigned n; } sPath[PATHS];
static struct { int used, kind, main; u64 tick; char path[120]; } sFly[FLYING];
static struct { int kind; u64 tick; char path[120]; } sMainLast = {-1, 0, ""};
static u64 sBucketTick;
static unsigned sBucket;
static Handle sMainThread, sWatchThread;

void NxFs_SetThreads(Handle mainThread, Handle watchThread) {
    sMainThread = mainThread;
    sWatchThread = watchThread;
}

/* A call begins: counted, braked, kept as under way. Returns its place among those under way (or -1). */
static int note(int kind, const char *path) {
    Handle self = threadGetCurHandle();
    u64 now = armGetSystemTick();
    int brake = 0, slot = -1, i;

    mutexLock(&sLock);
    sCount[kind]++;
    sTotal++;
    if (self == sMainThread) {
        sMain++;
        sMainLast.kind = kind;
        sMainLast.tick = now;
        snprintf(sMainLast.path, sizeof(sMainLast.path), "%s", path != NULL ? path : "");
    }
    if (now - sBucketTick > armGetSystemTickFreq() / 10) {
        sBucketTick = now;
        sBucket = 0;
    }
    if (++sBucket > BRAKE_AT && self != sWatchThread) {
        brake = 1;
        sBraked++;
    }
    if (path != NULL) {
        int low = 0;
        for (i = 0; i < PATHS && sPath[i].n != 0 && strncmp(sPath[i].path, path, sizeof(sPath[i].path) - 1) != 0; i++) {
        }
        if (i == PATHS) { /* (full: the least called one gives its place) */
            for (i = 1; i < PATHS; i++) {
                if (sPath[i].n < sPath[low].n) {
                    low = i;
                }
            }
            i = low;
            sPath[i].n = 0;
        }
        if (sPath[i].n == 0) {
            snprintf(sPath[i].path, sizeof(sPath[i].path), "%s", path);
        }
        sPath[i].n++;
    }
    for (i = 0; i < FLYING && sFly[i].used; i++) {
    }
    if (i < FLYING) {
        slot = i;
        sFly[i].used = 1;
        sFly[i].kind = kind;
        sFly[i].main = self == sMainThread;
        sFly[i].tick = now;
        snprintf(sFly[i].path, sizeof(sFly[i].path), "%s", path != NULL ? path : "");
    }
    mutexUnlock(&sLock);
    if (brake) {
        svcSleepThread(1000000ull);
    }
    return slot;
}

/* The call has come back */
static void done(int slot) {
    if (slot >= 0) {
        __atomic_store_n(&sFly[slot].used, 0, __ATOMIC_RELEASE);
    }
}

static int put(char *out, size_t cap, size_t at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int put(char *out, size_t cap, size_t at, const char *fmt, ...) {
    va_list ap;
    int n;
    if (at >= cap) {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(out + at, cap - at, fmt, ap);
    va_end(ap);
    return n < 0 ? 0 : (size_t)n < cap - at ? n : (int)(cap - at - 1);
}

unsigned long long NxFs_Total(void) {
    unsigned long long t;
    mutexLock(&sLock);
    t = sTotal;
    mutexUnlock(&sLock);
    return t;
}

/* What was counted since the last report, as a line into out (out NULL: only forgotten). The line's length. */
size_t NxFs_Report(char *out, size_t cap, unsigned ms) {
    unsigned count[K_KINDS], main, braked, n = 0, top = 0, i, all = 0;
    unsigned long long total;
    char path[160];
    size_t at = 0;

    mutexLock(&sLock);
    memcpy(count, sCount, sizeof(count));
    memset(sCount, 0, sizeof(sCount));
    main = sMain;
    braked = sBraked;
    total = sTotal;
    sMain = sBraked = 0;
    for (i = 1; i < PATHS; i++) {
        if (sPath[i].n > sPath[top].n) {
            top = i;
        }
    }
    snprintf(path, sizeof(path), "%s", sPath[top].path);
    n = sPath[top].n;
    memset(sPath, 0, sizeof(sPath));
    mutexUnlock(&sLock);
    for (i = 0; i < K_KINDS; i++) {
        all += count[i];
    }
    if (out == NULL) {
        return 0;
    }
    at += put(out, cap, at, "file service: %u calls in %u ms (%u by the main thread, %u braked; %llu since start; open: %u files, %u folders)",
              all, ms, main, braked, total, sOpenFiles, sOpenDirs);
    for (i = 0; i < K_KINDS; i++) {
        if (count[i] != 0) {
            at += put(out, cap, at, " %s %u", kKind[i], count[i]);
        }
    }
    if (n != 0) {
        at += put(out, cap, at, "; most named: %s (%u)", path, n);
    }
    at += put(out, cap, at, "\n");
    return at;
}

/* The calls under way for longer than `ms`, a line each, into out. The length written. */
size_t NxFs_Stuck(char *out, size_t cap, unsigned ms) {
    u64 now = armGetSystemTick(), per = armGetSystemTickFreq() / 1000;
    size_t at = 0;
    int i;

    mutexLock(&sLock);
    for (i = 0; i < FLYING; i++) {
        if (sFly[i].used && (now - sFly[i].tick) / per > ms) {
            at += put(out, cap, at, "         under way for %u ms: %s %s (%s)\n", (unsigned)((now - sFly[i].tick) / per),
                      kKind[sFly[i].kind], sFly[i].path, sFly[i].main ? "the main thread" : "another thread");
        }
    }
    mutexUnlock(&sLock);
    return at;
}

/* The main thread's last call, as a line into out. The length written. */
size_t NxFs_MainLast(char *out, size_t cap) {
    u64 now = armGetSystemTick(), per = armGetSystemTickFreq() / 1000;
    size_t at;

    mutexLock(&sLock);
    at = sMainLast.kind < 0 ? 0 : (size_t)put(out, cap, 0, "         the main thread's last call: %s %s, %u ms ago\n",
                                                 kKind[sMainLast.kind], sMainLast.path, (unsigned)((now - sMainLast.tick) / per));
    mutexUnlock(&sLock);
    return at;
}

#define WRAP(ret, name, params, kind, path, call)                                                                   \
    ret __real_##name params;                                                                                       \
    ret __wrap_##name params {                                                                                      \
        int slot = note(kind, path);                                                                                \
        ret r = __real_##name call;                                                                                 \
        done(slot);                                                                                                 \
        return r;                                                                                                   \
    }

WRAP(Result, fsFsCreateFile, (FsFileSystem *fs, const char *p, s64 size, u32 option), K_CREATE, p, (fs, p, size, option))
WRAP(Result, fsFsDeleteFile, (FsFileSystem *fs, const char *p), K_DELETE, p, (fs, p))
WRAP(Result, fsFsCreateDirectory, (FsFileSystem *fs, const char *p), K_MKDIR, p, (fs, p))
WRAP(Result, fsFsRenameFile, (FsFileSystem *fs, const char *p, const char *q), K_RENAME, p, (fs, p, q))
WRAP(Result, fsFsRenameDirectory, (FsFileSystem *fs, const char *p, const char *q), K_RENAME, p, (fs, p, q))
WRAP(Result, fsFsGetEntryType, (FsFileSystem *fs, const char *p, FsDirEntryType *out), K_TYPE, p, (fs, p, out))
WRAP(Result, fsFsGetFileTimeStampRaw, (FsFileSystem *fs, const char *p, FsTimeStampRaw *out), K_TIME, p, (fs, p, out))
WRAP(Result, fsFileRead, (FsFile *f, s64 off, void *buf, u64 size, u32 option, u64 *got), K_READ, NULL, (f, off, buf, size, option, got))
WRAP(Result, fsFileWrite, (FsFile *f, s64 off, const void *buf, u64 size, u32 option), K_WRITE, NULL, (f, off, buf, size, option))
WRAP(Result, fsFileFlush, (FsFile *f), K_FLUSH, NULL, (f))
WRAP(Result, fsFileGetSize, (FsFile *f, s64 *out), K_SIZE, NULL, (f, out))
WRAP(Result, fsFileSetSize, (FsFile *f, s64 size), K_SETSIZE, NULL, (f, size))
WRAP(Result, fsDirRead, (FsDir *d, s64 *total, size_t max, FsDirectoryEntry *buf), K_DIRREAD, NULL, (d, total, max, buf))

/* (the opens and closes also keep how many are open: a program that never closes runs out of them) */
Result __real_fsFsOpenFile(FsFileSystem *fs, const char *p, u32 mode, FsFile *out);
Result __wrap_fsFsOpenFile(FsFileSystem *fs, const char *p, u32 mode, FsFile *out) {
    int slot = note(K_OPEN, p);
    Result rc = __real_fsFsOpenFile(fs, p, mode, out);
    done(slot);
    if (R_SUCCEEDED(rc)) {
        __atomic_add_fetch(&sOpenFiles, 1, __ATOMIC_RELAXED);
    }
    return rc;
}
Result __real_fsFsOpenDirectory(FsFileSystem *fs, const char *p, u32 mode, FsDir *out);
Result __wrap_fsFsOpenDirectory(FsFileSystem *fs, const char *p, u32 mode, FsDir *out) {
    int slot = note(K_OPENDIR, p);
    Result rc = __real_fsFsOpenDirectory(fs, p, mode, out);
    done(slot);
    if (R_SUCCEEDED(rc)) {
        __atomic_add_fetch(&sOpenDirs, 1, __ATOMIC_RELAXED);
    }
    return rc;
}
void __real_fsFileClose(FsFile *f);
void __wrap_fsFileClose(FsFile *f) {
    int slot = note(K_CLOSE, NULL);
    __real_fsFileClose(f);
    done(slot);
    __atomic_sub_fetch(&sOpenFiles, 1, __ATOMIC_RELAXED);
}
void __real_fsDirClose(FsDir *d);
void __wrap_fsDirClose(FsDir *d) {
    int slot = note(K_DIRCLOSE, NULL);
    __real_fsDirClose(d);
    done(slot);
    __atomic_sub_fetch(&sOpenDirs, 1, __ATOMIC_RELAXED);
}

/* BT3_NX_TRACE builds (port/tools/undefined.py): the renderer's C files call these at each function's entry and
   exit. The game's thread's calls are kept, innermost last, for the watch thread: where that thread stands when the
   game stops drawing. Addresses as offsets from the program's start (__start__), to be looked up in the build's
   bt3_nx.map at 0x4020000000 + offset. */
#define DEPTH 48
static uintptr_t sMainTls;
static void *volatile sCall[DEPTH];
static volatile int sDepth;
static void *volatile sLastLeft;
extern char __start__[];

void NxTrace_SetMain(void) {
    sMainTls = (uintptr_t)armGetTls();
}

void __cyg_profile_func_enter(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *site) {
    (void)site;
    if ((uintptr_t)armGetTls() == sMainTls) {
        int d = sDepth;
        if (d >= 0 && d < DEPTH) {
            sCall[d] = fn;
        }
        sDepth = d + 1;
    }
}

void __cyg_profile_func_exit(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site) {
    (void)site;
    if ((uintptr_t)armGetTls() == sMainTls) {
        sLastLeft = fn;
        sDepth--;
    }
}

/* The game's thread's calls in the renderer, outermost first, as a line into out. The length written. */
size_t NxTrace_Report(char *out, size_t cap) {
    int d = sDepth, i;
    size_t at = 0;

    if (sMainTls == 0) {
        return 0;
    }
    at += put(out, cap, at, "         renderer calls (offsets from __start__; depth %d):", d);
    for (i = 0; i < d && i < DEPTH; i++) {
        at += put(out, cap, at, " %#lx", (unsigned long)((uintptr_t)sCall[i] - (uintptr_t)__start__));
    }
    if (d == 0) {
        at += put(out, cap, at, " none (in the game's code or the port's other files); the last one left: %#lx",
                  (unsigned long)((uintptr_t)sLastLeft - (uintptr_t)__start__));
    }
    at += put(out, cap, at, "\n");
    return at;
}

/* Every file the C library opens: a buffer of BUFFER bytes (its own would be 1 KB: one request to the card per
   kilobyte read). Before any reading or writing, as setvbuf has to be. */
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) {
    FILE *f = __real_fopen(path, mode);
    if (f != NULL) {
        setvbuf(f, NULL, _IOFBF, BUFFER);
    }
    return f;
}
#endif
