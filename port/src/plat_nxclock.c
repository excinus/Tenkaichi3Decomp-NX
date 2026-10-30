/*
 * The Switch only: the clocks the game needs, through sys-clk (the homebrew clock manager), when it is installed.
 *
 * At the stock clocks a fight runs at 16 to 20 pictures a second; overclocked it is a clean 30 (docs/port/switch_plan.md,
 * "Results"). sys-clk's own service (sys:clk, what its overlay uses) takes temporary overrides of the three clocks, set
 * here whichever title the game runs as, so nothing is written into sys-clk's configuration and no other game is
 * affected:
 *   - CPU 1785 MHz and memory 1600 MHz always;
 *   - GPU 921 MHz docked or on a charger (any: the official one, another USB one), 614 MHz in the hand on the battery;
 *     followed as the console goes in and out of its dock or is plugged in (the watch thread, twice a second).
 * sys-clk holds the GPU below what is asked where it judges it unsafe for the battery (its ClockManager::
 * GetMaxAllowedHz, which an override passes through too): on the battery 460.8 MHz on a V1 (Erista) and 614.4 on the
 * others (Mariko), 768 on a charger that is not the official one; no limit docked or on the official charger. The
 * Lite (Mariko, 12 W for the whole board against the others' 18) is never asked more than 614.4: what the OLED plays
 * at a clean 30 in the hand, so nothing is lost. The console's model is in the log.
 * They are taken back (0: sys-clk's own) as soon as the game is not in front (the HOME menu, sleep: an applet hook), so
 * that closing it from the HOME menu leaves nothing behind, and set again when it is back. (Holding back the system's
 * closing instead, appletLockExit, left the console waiting for the game's end and then saying it had crashed: the
 * player's report, 2026-10-30.) A crash leaves them until the console restarts or they are changed in sys-clk's overlay.
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

/* sys-clk's IPC (its common/include/sysclk/ipc.h): the command and the modules */
#define SYSCLK_SET_OVERRIDE 8
#define SYSCLK_CPU 0
#define SYSCLK_GPU 1
#define SYSCLK_MEM 2
#define CPU_HZ 1785000000u
#define MEM_HZ 1600000000u
#define GPU_HAND_HZ 614400000u  /* in the hand, on the battery */
#define GPU_POWER_HZ 921600000u /* docked, or on a charger */

static Service sSrv;
static int sState = -1; /* -1 not tried, 0 sys-clk not there, 1 connected */
static int sOn, sAway, sGpuWas = -1;
static SetSysProductModel sModel = SetSysProductModel_Invalid;
static AppletHookCookie sHook;

static Result set_override(u32 module, u32 hz) {
    const struct {
        u32 module, hz;
    } in = {module, hz};
    return serviceDispatchIn(&sSrv, SYSCLK_SET_OVERRIDE, in);
}

/* the GPU's clock for now: docked or on a charger, or in the hand on the battery */
static void gpu_for_power(int force) {
    PsmChargerType charger = PsmChargerType_Unconnected;
    int power;
    psmGetChargerType(&charger);
    power = appletGetOperationMode() == AppletOperationMode_Console || charger != PsmChargerType_Unconnected;
    power = power && sModel != SetSysProductModel_Hoag; /* (the Lite: 614.4 always, see above) */
    if (power != sGpuWas || force) {
        sGpuWas = power;
        set_override(SYSCLK_GPU, power ? GPU_POWER_HZ : GPU_HAND_HZ);
    }
}

static void clocks_on(void) {
    set_override(SYSCLK_CPU, CPU_HZ);
    set_override(SYSCLK_MEM, MEM_HZ);
    gpu_for_power(1);
}

static void clocks_off(void) {
    set_override(SYSCLK_CPU, 0);
    set_override(SYSCLK_GPU, 0);
    set_override(SYSCLK_MEM, 0);
}

static void off_at_exit(void) {
    if (sState == 1 && sOn) {
        clocks_off();
        sOn = 0;
    }
}

/* the game in front or not (the HOME menu, sleep): the clocks follow (from the applet loop, in SDL's event pump) */
static void on_applet(AppletHookType type, void *param) {
    (void)param;
    if (type == AppletHookType_OnFocusState && sState == 1 && sOn) {
        int away = appletGetFocusState() != AppletFocusState_InFocus;
        if (away != sAway) {
            sAway = away;
            if (away) {
                clocks_off();
            } else {
                clocks_on();
            }
        }
    }
}

/* on: the game's clocks; off: sys-clk's own again. */
void Port_NxClockSet(int on) {
    if (sState < 0) {
        sState = R_SUCCEEDED(smGetService(&sSrv, "sys:clk")) ? 1 : 0;
        if (R_SUCCEEDED(setsysInitialize())) {
            setsysGetProductModel(&sModel);
            setsysExit();
        }
        fprintf(stderr, "bt3: console: %s\n",
                sModel == SetSysProductModel_Nx ? "Switch V1 (Erista): sys-clk allows the GPU 460 MHz on the battery" :
                sModel == SetSysProductModel_Iowa ? "Switch V2 (Mariko)" :
                sModel == SetSysProductModel_Hoag ? "Switch Lite (Mariko): the GPU at 614 MHz at most" :
                sModel == SetSysProductModel_Aula ? "Switch OLED (Mariko)" : "another model");
        if (sState == 1) {
            psmInitialize();
            atexit(off_at_exit);
            appletHook(&sHook, on_applet, NULL);
        }
        fprintf(stderr, "bt3: clocks: %s\n", sState == 1 ? "sys-clk is there" : "no sys-clk: the stock clocks");
    }
    if (sState != 1 || on == sOn) {
        return;
    }
    sOn = on;
    if (on) {
        clocks_on();
        fprintf(stderr, "bt3: clocks: CPU 1785 MHz, memory 1600 MHz, GPU %s\n",
                sGpuWas ? "921 MHz (docked or charging; 768 on a charger that is not the official one)" :
                sModel == SetSysProductModel_Hoag ? "614 MHz (the Lite)" : "614 MHz (on the battery)");
    } else {
        clocks_off();
        fprintf(stderr, "bt3: clocks: sys-clk's own again\n");
    }
}

int Port_NxClockThere(void) {
    return sState == 1;
}

/* From the watch thread (plat_mem.c), twice a second: the first call sets the clocks; then the GPU's follows the dock
   and the charger. */
void Port_NxClockTick(void) {
    if (sState < 0) {
        Port_NxClockSet(Port_Setting("sysclk_overclock", 1) != 0);
    } else if (sOn && !sAway) {
        gpu_for_power(0);
    }
}
#endif
