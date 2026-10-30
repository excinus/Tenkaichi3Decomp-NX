/*
 * The Switch only: the clocks the game needs, through sys-clk (the homebrew clock manager), when it is installed.
 *
 * At the stock clocks a fight runs at 16 to 20 pictures a second; at CPU 1785 MHz, GPU 614 MHz in the hand and memory
 * 1600 MHz it is a clean 30 (docs/port/switch_plan.md, "Results"). sys-clk's own service (sys:clk, what its overlay
 * uses) takes temporary overrides of the three clocks: they are set when the game starts and taken back (0) when it
 * ends, whichever title it runs as, so nothing is written into sys-clk's configuration and no other game is affected.
 * Docked the GPU is left to the dock's own 768 MHz (614 would be less). The game holds back the system's closing of it
 * (appletLockExit) so that closing it from the HOME menu ends it through exit, and the overrides go too; a crash
 * leaves them until the console restarts or they are changed in sys-clk's overlay.
 * Setting "sysclk_overclock" (the settings window's Video tab), on by default. Without sys-clk nothing happens.
 */
#ifdef __SWITCH__
#include <stdio.h>
#include <stdlib.h>
#ifndef static_assert
#define static_assert _Static_assert /* (libnx's serviceDispatch macros use it; this file is built as gnu99) */
#endif
#include <switch.h>

extern int Port_Setting(const char *name, int def);

/* sys-clk's IPC (its common/include/sysclk/ipc.h): commands and modules */
#define SYSCLK_GET_API_VERSION 0
#define SYSCLK_SET_OVERRIDE 8
#define SYSCLK_CPU 0
#define SYSCLK_GPU 1
#define SYSCLK_MEM 2
#define CPU_HZ 1785000000u
#define GPU_HZ 614400000u
#define MEM_HZ 1600000000u

static Service sSrv;
static int sState = -1; /* -1 not tried, 0 sys-clk not there, 1 connected */
static int sOn, sDocked = -1;

static Result set_override(u32 module, u32 hz) {
    const struct {
        u32 module, hz;
    } in = {module, hz};
    return serviceDispatchIn(&sSrv, SYSCLK_SET_OVERRIDE, in);
}

static void gpu_for_mode(void) {
    int docked = appletGetOperationMode() == AppletOperationMode_Console;
    if (docked != sDocked) {
        sDocked = docked;
        set_override(SYSCLK_GPU, docked ? 0 : GPU_HZ);
    }
}

static void off_at_exit(void) {
    if (sState == 1 && sOn) {
        set_override(SYSCLK_CPU, 0);
        set_override(SYSCLK_GPU, 0);
        set_override(SYSCLK_MEM, 0);
        sOn = 0;
    }
}

/* on: the game's clocks; off: sys-clk's own again. */
void Port_NxClockSet(int on) {
    if (sState < 0) {
        u32 api = 0;
        sState = R_SUCCEEDED(smGetService(&sSrv, "sys:clk")) ? 1 : 0;
        if (sState == 1) {
            serviceDispatchOut(&sSrv, SYSCLK_GET_API_VERSION, api);
            atexit(off_at_exit);
            appletLockExit();
        }
        fprintf(stderr, "bt3: clocks: %s\n", sState == 1 ? "sys-clk is there" : "no sys-clk: the stock clocks");
        (void)api;
    }
    if (sState != 1 || on == sOn) {
        return;
    }
    if (on) {
        Result a = set_override(SYSCLK_CPU, CPU_HZ), b = set_override(SYSCLK_MEM, MEM_HZ);
        sOn = 1;
        sDocked = -1;
        gpu_for_mode();
        fprintf(stderr, "bt3: clocks: CPU 1785 MHz, memory 1600 MHz, GPU %s (sys-clk answered 0x%x 0x%x)\n",
                sDocked ? "the dock's" : "614 MHz", (unsigned)a, (unsigned)b);
    } else {
        off_at_exit();
        fprintf(stderr, "bt3: clocks: sys-clk's own again\n");
    }
}

int Port_NxClockOn(void) {
    return sOn;
}

int Port_NxClockThere(void) {
    return sState == 1;
}

/* From the watch thread (plat_mem.c), twice a second: the first call sets the clocks; then the GPU's follows the dock. */
void Port_NxClockTick(void) {
    if (sState < 0) {
        Port_NxClockSet(Port_Setting("sysclk_overclock", 1) != 0);
    } else if (sOn) {
        gpu_for_mode();
    }
}
#endif
