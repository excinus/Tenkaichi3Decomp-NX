/*
 * Markers for the PC renderer inside the game's display list. The game builds each frame's list first and the
 * renderer executes it afterwards, so "a native effect goes HERE" has to travel in the list: a one-register GIF
 * packet that writes the effect number to GS register 0x7F, which the real GS does not have. The front end
 * (port/src/gs/gs_core.c) hands it to the GPU back end; the software reference ignores it and keeps drawing the
 * PS2 passes. Called from the game's effect functions under `#ifdef PORT`.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void Dma_AddData(void *src, int size);

void Port_GsMarker(int effect) {
    uint32_t pkt[12] = {
        0x10000000u | 2, 0, 0x10000000u, 0x50000000u | 2, /* DMA CNT, 2 quadwords; VIF FLUSHE, DIRECT 2 */
        0x8000u | 1, 0x10000000u, 0xE, 0,                  /* GIF tag: 1 loop, EOP, one register: A+D */
        (uint32_t)effect, 0, 0x7F, 0,                      /* value, register number */
    };

    Dma_AddData(pkt, sizeof(pkt));
}

/* Widescreen menus (the menu layout "Widescreen", port/src/gs/gs_draw.c). A screen converted for it marks its draw,
   and each 2D piece of it is kept at its true shape, anchored where the screen's design has it:
     PORT_MENU_CENTER  about the middle of the picture (the default of a page)
     PORT_MENU_LEFT    against the left edge (a menu's column)
     PORT_MENU_RIGHT   against the right edge (the characters beside it)
     PORT_MENU_FILL    across the whole width, stretched (backdrops, scrolling scenery)
   Which clip of a screen's movie goes where is the table in Port_MenuClipPart, by the clips' names. A screen that
   marks nothing is stretched as before. The renderer acts on the markers only with that layout chosen. */
enum { PORT_MENU_NONE = -1, PORT_MENU_CENTER, PORT_MENU_FILL, PORT_MENU_LEFT, PORT_MENU_RIGHT };
static int sMenuScreen;              /* PORT_MENU_SCREEN_* of the page being drawn, 0: none */
static int sMenuPart = PORT_MENU_NONE;

/* The part what follows belongs to; returns the one before (to be given back). */
int Port_MenuPart(int part) {
    int prev = sMenuPart;
    if (part != sMenuPart) {
        sMenuPart = part;
        Port_GsMarker(part == PORT_MENU_NONE ? 0x13 : 0x22 + part); /* (0x13: the HUD's "end", no anchor) */
    }
    return prev;
}

/* A converted screen's draw begins (its PORT_MENU_SCREEN_* number) / ends (0). */
void Port_MenuPage(int screen) {
    sMenuScreen = screen;
    /* (the main menu: what is not named in Port_MenuClipPart belongs to its column, on the left) */
    Port_MenuPart(screen == 0 ? PORT_MENU_NONE : screen == 2 ? PORT_MENU_LEFT : PORT_MENU_CENTER);
}

/* Backdrops: on, and back to the page's part */
static int sFillPrev;
void Port_MenuFill(int on) {
    if (on) {
        sFillPrev = Port_MenuPart(PORT_MENU_FILL);
    } else {
        Port_MenuPart(sFillPrev);
    }
}

static int starts(const char *name, const char *prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

/* The part of a clip of the page being drawn (FlashSlot_Draw), PORT_MENU_NONE: as its parent's. */
int Port_MenuClipPart(const char *name) {
    static int log = -1;
    if (log < 0) {
        log = getenv("BT3_CLIP_LOG") != NULL;
    }
    if (sMenuScreen == 0) {
        return PORT_MENU_NONE;
    }
    if (log) { /* (testing: the clips a converted screen draws, each name once) */
        static char seen[512][64];
        static int n;
        int i;
        for (i = 0; i < n && strcmp(seen[i], name) != 0; i++) {
        }
        if (i == n && n < 512) {
            snprintf(seen[n++], 64, "%s", name);
            fprintf(stderr, "bt3: menu clip %d %s\n", sMenuScreen, name);
        }
    }
    if (starts(name, "mc_bg") || starts(name, "mc_kumo_")) {
        return PORT_MENU_FILL;
    }
    if (sMenuScreen == 2) { /* the main menu: the column of plates on the left, the guides on the right */
        if (starts(name, "mc_menu_plate_") || starts(name, "mc_icon") || starts(name, "mc_title") || starts(name, "mc_ch_a_db_") ||
            starts(name, "mc_dragonball") || starts(name, "mc_menu_yajirusi") || starts(name, "mc_chara_db")) {
            return PORT_MENU_LEFT;
        }
        if (starts(name, "mc_guide_") || starts(name, "mc_uide_")) { /* ("mc_uide_ghn_bdl_hair": so named in the movie) */
            return PORT_MENU_RIGHT;
        }
    }
    return PORT_MENU_NONE;
}
