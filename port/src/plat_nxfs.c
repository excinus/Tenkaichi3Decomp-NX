/*
 * The Switch only (docs/port/switch_plan.md): a count of the program's calls to the SD card's file service, and a
 * brake on them.
 *
 * The game's first starts on a console hung after the language list with the whole console slowed down (the volume
 * buttons and HOME answering after about 10 seconds), and nothing of the log reached the card, not even the lines
 * written before the hang, though the log was flushed to the card twice a second. The console's own menus and the
 * game's log use the same file service: a part of the program that calls it without end (a loop over a file
 * operation that fails on the Switch, say) would do all of that.
 *
 * Every call of the file system C library (libnx's fsdev, under fopen, fread, stat, mkdir...) goes through the libnx
 * functions below; the link wraps them (--wrap, port/tools/link.py). Each is counted, by kind, and those naming a
 * path keep the path most called in the last report's time. When the program makes more than BRAKE_AT calls in a
 * tenth of a second, every further call in that tenth waits a millisecond first (the watch thread's own excepted):
 * the console stays usable, the log reaches the card, and the program goes on, slower.
 *
 * NxFs_Report (from the watch thread, plat_mem.c) writes what was counted since the last report.
 */
#ifdef __SWITCH__
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define BRAKE_AT 400 /* calls in a tenth of a second (4000 a second) */
#define PATHS 16

enum { K_OPEN, K_CREATE, K_DELETE, K_MKDIR, K_RENAME, K_TYPE, K_OPENDIR, K_TIME, K_READ, K_WRITE, K_FLUSH, K_SIZE,
       K_SETSIZE, K_CLOSE, K_DIRREAD, K_DIRCLOSE, K_KINDS };
static const char *const kKind[K_KINDS] = {"open", "create", "delete", "mkdir", "rename", "entry type", "open dir",
                                           "time stamp", "read", "write", "flush", "get size", "set size", "close",
                                           "read dir", "close dir"};

static Mutex sLock;
static unsigned sCount[K_KINDS], sMain, sBraked, sOpenFiles, sOpenDirs;
static unsigned long long sTotal;
static struct { char path[160]; unsigned n; } sPath[PATHS];
static u64 sBucketTick;
static unsigned sBucket;
static Handle sMainThread, sWatchThread;

void NxFs_SetThreads(Handle mainThread, Handle watchThread) {
    sMainThread = mainThread;
    sWatchThread = watchThread;
}

static void note(int kind, const char *path) {
    Handle self = threadGetCurHandle();
    u64 now = armGetSystemTick();
    int brake = 0;

    mutexLock(&sLock);
    sCount[kind]++;
    sTotal++;
    if (self == sMainThread) {
        sMain++;
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
        int i, low = 0;
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
    mutexUnlock(&sLock);
    if (brake) {
        svcSleepThread(1000000ull);
    }
}

unsigned long long NxFs_Total(void) {
    unsigned long long t;
    mutexLock(&sLock);
    t = sTotal;
    mutexUnlock(&sLock);
    return t;
}

/* What was counted since the last report, into f (NULL: only forgotten); 0 if there were no calls. */
int NxFs_Report(FILE *f, unsigned ms) {
    unsigned count[K_KINDS], main, braked, n = 0, top = 0, i;
    unsigned long long total;
    char path[160];

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
    {
        unsigned all = 0;
        for (i = 0; i < K_KINDS; i++) {
            all += count[i];
        }
        if (all == 0 || f == NULL) {
            return all != 0;
        }
        fprintf(f, "file service: %u calls in %u ms (%u by the game's main thread, %u braked; %llu since start; open now: %u files, %u folders):",
                all, ms, main, braked, total, sOpenFiles, sOpenDirs);
    }
    for (i = 0; i < K_KINDS; i++) {
        if (count[i] != 0) {
            fprintf(f, " %s %u", kKind[i], count[i]);
        }
    }
    if (n != 0) {
        fprintf(f, "; the path most named: %s (%u times)", path, n);
    }
    fprintf(f, "\n");
    return 1;
}

#define WRAP(ret, name, params, kind, path, call)                                                                   \
    ret __real_##name params;                                                                                       \
    ret __wrap_##name params {                                                                                      \
        note(kind, path);                                                                                           \
        return __real_##name call;                                                                                  \
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
    Result rc;
    note(K_OPEN, p);
    rc = __real_fsFsOpenFile(fs, p, mode, out);
    if (R_SUCCEEDED(rc)) {
        __atomic_add_fetch(&sOpenFiles, 1, __ATOMIC_RELAXED);
    }
    return rc;
}
Result __real_fsFsOpenDirectory(FsFileSystem *fs, const char *p, u32 mode, FsDir *out);
Result __wrap_fsFsOpenDirectory(FsFileSystem *fs, const char *p, u32 mode, FsDir *out) {
    Result rc;
    note(K_OPENDIR, p);
    rc = __real_fsFsOpenDirectory(fs, p, mode, out);
    if (R_SUCCEEDED(rc)) {
        __atomic_add_fetch(&sOpenDirs, 1, __ATOMIC_RELAXED);
    }
    return rc;
}
void __real_fsFileClose(FsFile *f);
void __wrap_fsFileClose(FsFile *f) {
    note(K_CLOSE, NULL);
    __atomic_sub_fetch(&sOpenFiles, 1, __ATOMIC_RELAXED);
    __real_fsFileClose(f);
}
void __real_fsDirClose(FsDir *d);
void __wrap_fsDirClose(FsDir *d) {
    note(K_DIRCLOSE, NULL);
    __atomic_sub_fetch(&sOpenDirs, 1, __ATOMIC_RELAXED);
    __real_fsDirClose(d);
}
#endif
