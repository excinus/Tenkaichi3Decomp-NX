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
     PORT_MENU_CENTER_ALL, _LEFT_ALL, _RIGHT_ALL  as CENTER, LEFT, RIGHT, and pieces as wide as the 4:3 screen too
                       (in the others they fill the width): a bar whose cells hold other pieces
   Which clip of a screen's movie goes where is the table in Port_MenuClipPart, by the clips' names. A screen that
   marks nothing is stretched as before. The renderer acts on the markers only with that layout chosen. */
enum { PORT_MENU_NONE = -1, PORT_MENU_CENTER, PORT_MENU_FILL, PORT_MENU_LEFT, PORT_MENU_RIGHT, PORT_MENU_CENTER_ALL, PORT_MENU_LEFT_ALL,
       PORT_MENU_RIGHT_ALL };
static int sMenuPart = PORT_MENU_NONE;
static int sMenuMovie;               /* which of the screen's movies is being drawn (Port_MenuMovie) */
static int sMenuScreen;              /* PORT_MENU_SCREEN_* of the page being drawn, 0: none */

int Port_MenuPart(int part);

/* The screen's movie `index` is drawn next. Character select: movie 1 is the first player's roster and 3 that
   player's window of customisations, 2 and 4 the other player's: each against that player's edge. */
void Port_MenuMovie(int index) {
    sMenuMovie = index;
    if (sMenuScreen == 3) {
        Port_MenuPart(index == 0 ? PORT_MENU_CENTER_ALL : index & 1 ? PORT_MENU_LEFT_ALL : PORT_MENU_RIGHT_ALL);
    }
}

/* The part what follows belongs to; returns the one before (to be given back). */
int Port_MenuPart(int part) {
    int prev = sMenuPart;
    if (part != sMenuPart) {
        sMenuPart = part;
        Port_GsMarker(part == PORT_MENU_NONE ? 0x13 : 0x22 + part); /* (0x13: the HUD's "end", no anchor) */
    }
    return prev;
}

/* The part being drawn now (PORT_MENU_NONE outside a converted screen) */
int Port_MenuPartNow(void) {
    return sMenuPart;
}

/* A converted screen's draw begins (its number) / ends (0). A screen's draw can call another's (a backdrop drawn by
   a function of its own): the end of the inner one goes back to the outer one. */
static int sPageStack[8], sPageDepth;
void Port_MenuPage(int screen) {
    if (screen != 0) {
        if (sPageDepth < 8) {
            sPageStack[sPageDepth++] = sMenuScreen;
        }
    } else {
        screen = sPageDepth > 0 ? sPageStack[--sPageDepth] : 0;
    }
    sMenuScreen = screen;
    /* (the main menu: what is not named in Port_MenuClipPart belongs to its column, on the left) */
    /* (character select: all of it at its true shape, its bars too, which are pieces as wide as the 4:3 screen, so
       that the bars' cells and the characters in them stay together; only the stage pictures fill the width) */
    /* (what a page draws itself, not in a clip, stays in the middle: on most screens its windows and plates. On
       Dragon History, 13, it is the scenery, which fills the width. Character Reference's shelves are such pieces
       too, but so is its window of rows: in the middle, the shelves meet the backdrop at the 4:3 page's edges.) */
    Port_MenuPart(screen == 0 ? PORT_MENU_NONE : screen == 2 ? PORT_MENU_LEFT : screen == 3 ? PORT_MENU_CENTER_ALL : screen == 13 ? PORT_MENU_FILL
                                                                                                                   : PORT_MENU_CENTER);
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

/* The side of a measured clip that lies from x0 to x1 on the 4:3 screen: one touching its left edge (a guide on the
   left, a column of plates out of it) against the left edge, its right edge the right edge, otherwise by the third
   its middle is in (the rows of a window in the middle stay with it). One as wide as the screen, or with nothing to draw, is left to its pieces. */
int Port_MenuClipPlace(float x0, float x1) {
    float c = (x0 + x1) * 0.5f;
    if (x1 < x0 || (x0 <= 1.0f && x1 >= 511.0f)) {
        return PORT_MENU_NONE;
    }
    if (x0 < 16.0f && x1 < 496.0f) {
        return PORT_MENU_LEFT;
    }
    if (x1 > 496.0f && x0 > 16.0f) {
        return PORT_MENU_RIGHT;
    }
    return c < 512.0f / 3.0f ? PORT_MENU_LEFT : c > 1024.0f / 3.0f ? PORT_MENU_RIGHT : PORT_MENU_CENTER;
}

/* The part of a clip of the page being drawn (FlashSlot_Draw), PORT_MENU_NONE: as its parent's. `x`: where the clip's
   origin is on the 4:3 screen (0..512); `top`: a clip of the movie itself, not inside another. */
int Port_MenuClipPart(const char *name, float x, int top) {
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
        char key[64];
        snprintf(key, sizeof(key), "%d %d %s", sMenuScreen, sMenuMovie, name);
        for (i = 0; i < n && strcmp(seen[i], key) != 0; i++) {
        }
        if (i == n && n < 512) {
            snprintf(seen[n++], 64, "%s", key);
            fprintf(stderr, "bt3: menu clip %s%s x %.0f\n", key, top ? " (top)" : "", x);
        }
    }
    if (starts(name, "mc_bg_") || starts(name, "mc_kumo_")) { /* (not "mc_bgm_now": the music's plate) */
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
    if (sMenuScreen == 3) {
        /* character select: each player's name plates, windows and their text against that player's edge (the
           text is queued and drawn after the screen, Font_Flush, in the part it was queued in). The big pictures
           (mc_single_chara_*, their masks) are as wide as half of the 4:3 screen and keep to the line between the
           two players, as there (the user's choice). */
        size_t n = strlen(name);
        if (n > 2 && name[n - 2] == '_' && (name[n - 1] == 'l' || name[n - 1] == 'r') && !starts(name, "mc_single_chara_") &&
            !starts(name, "mc_face_mask_")) {
            return name[n - 1] == 'l' ? PORT_MENU_LEFT : PORT_MENU_RIGHT;
        }
    }
    if (sMenuScreen >= 10 && top && (starts(name, "mc_title") || strcmp(name, "mc_icon") == 0)) {
        return PORT_MENU_LEFT; /* (the title row against the left edge, as on the main menu) */
    }
    if (sMenuScreen == 13 && (starts(name, "mc_yuragi_") || starts(name, "mc_taki") || starts(name, "mc_sakana"))) {
        return PORT_MENU_FILL; /* (Dragon History: its scenery, the waterfall, its shimmer, the fish, with the backdrop) */
    }
    if ((sMenuScreen == 17 || sMenuScreen == 45) && top && !starts(name, "mc_guide_") && strcmp(name, "mc_ch_gohan") != 0) {
        /* (Character Reference, the fighter catalog: a window of rows in the middle, its highlight and rows with it;
           only the title row and the guides go to their sides: Chi-Chi, and Gohan reading, mc_ch_gohan) */
        return PORT_MENU_CENTER;
    }
    if (sMenuScreen >= 10 && top) {
        /* the other screens, all of one kind (a title, plates, a guide or two, a message across the bottom): each
           of a movie's own clips (what it holds goes with it) against the side of the 4:3 screen where it is. Its
           place is measured first (-2: FlashSlot_Draw, then Port_MenuClipPlace) */
        return -2;
    }
    return PORT_MENU_NONE;
}
