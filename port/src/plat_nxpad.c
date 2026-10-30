/*
 * The Switch only: the console's own Controllers screen (the library applet the system shows for "Change Grip/Order"),
 * for a duel of two players on one console.
 *
 * The game's duel menu (src/menu/menu_g_c.c, "1P vs 2P") asks for two players before it goes on; the settings window's
 * Controls tab has a button for one player and one for two. The screen lets the players hold a Joy-Con each sideways
 * (SL + SR), or take a pair, a Pro Controller or another controller, and gives the first player's to player 1 and the
 * second's to player 2: SDL's driver reads them in that order (port/switch/sdl3, joystick), and the game's player 2 is
 * the second controller (gs_input.c). With one player it also takes the console in the hand. The game's thread waits
 * while the screen is up.
 */
#ifdef __SWITCH__
#include <stdio.h>
#include <switch.h>

/* Returns 1 once `players` controllers are there (2), or one is (1); 0 if the screen was left without them. */
int Port_NxControllers(int players) {
    HidLaControllerSupportArg arg;
    HidLaControllerSupportResultInfo info = {0};
    Result rc;

    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = (s8)players;
    arg.hdr.player_count_max = (s8)players;
    arg.hdr.enable_single_mode = players == 1; /* (one player: the console in the hand allowed too) */
    rc = hidLaShowControllerSupport(&info, &arg);
    fprintf(stderr, "bt3: controllers: the console's screen for %d player%s: %s, %d there\n", players, players == 1 ? "" : "s",
            R_SUCCEEDED(rc) ? "done" : "left", (int)info.player_count);
    return R_SUCCEEDED(rc) && info.player_count >= players;
}

/* The console's sockets, for the automatic update's HTTPS (gs/net_update.c: curl, its TLS the console's own): once,
   when first needed (from the update's thread; the next calls only say how it went). 1 when they are up. */
int Port_NxNetUp(void) {
    static int state = -1;
    if (state < 0) {
        Result rc = socketInitializeDefault();
        state = R_SUCCEEDED(rc);
        fprintf(stderr, "bt3: network: %s (%#x)\n", state ? "up" : "the console's sockets could not start", (unsigned)rc);
    }
    return state;
}
#endif
