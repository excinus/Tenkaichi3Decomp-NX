/*
 * The Switch only: the options screen's controller page shows a pair of Joy-Con where the game draws a DualShock 2.
 *
 * The screen (src/menu/menu_x_c.c Option_Init) unpacks its pack of sections and hands it here at once. Section 21's
 * texture 0 is the pad's picture, 128 x 128 texels of 8 bits (PSMT8) with a palette of 256 colours: its texels and its
 * palette are written over with port/switch/art's picture (nx_joycon_art.h, made from joycons.svg), in place, so the
 * screen uploads the new picture as it would the old one. The texture's bytes are the GS's 32-bit image that the game
 * uploads, the texels in the GS's PSMT8 order inside it: which byte holds a texel is worked out the way the GS would
 * read it (the tables below, as port/src/gs/gs_core.c and the PS Vita port's tools/bt3tex.py have them). A texture
 * that is not such a picture is left alone, and the log says why. (After the PS Vita port, port/vita/vita_optart.c,
 * which changes the same picture into a Vita.)
 */
#ifdef __SWITCH__
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx_joycon_art.h"

#define GS_BYTES (1024 * 1024) /* the first megabyte of GS memory, which a menu's picture and its upload are counted in */

static const uint8_t kBlock32[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
                                       {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
static const uint8_t kCol32[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15},
                                     {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                     {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
                                     {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
static const uint8_t kCol8First[4][16] = {{0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
                                          {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
                                          {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
                                          {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31}};

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint64_t rd64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

/* For redrawing the game's button icons (2026-10-30): the bytes they are made from, once, beside bt3_log.txt (sent by
   the player; read with the PS Vita port's tools/bt3tex.py). Written only when the file is not there yet. */
static void dump_once(const char *name, const void *p, uint32_t size) {
    FILE *f = fopen(name, "rb");
    if (f != NULL) {
        fclose(f);
        return;
    }
    if (p == NULL || size == 0 || size > (16u << 20) || (f = fopen(name, "wb")) == NULL) {
        return;
    }
    fwrite(p, 1, size, f);
    fclose(f);
    fprintf(stderr, "bt3: art: %s written (%u bytes)\n", name, (unsigned)size);
}

/* The boot file (src/battle/col_c_b.c FontIcon_Init), before its member 8, the button icons of the game's text, is
   bound: that member, kept for redrawing the icons. */
void Port_NxIconArt(void *boot) {
    uint8_t *b = boot;
    uint32_t n, from, to;
    if (b == NULL || (n = rd32(b)) < 8 || n > 64) {
        return;
    }
    from = (rd32(b + 8 * 4) >> 2) << 2;
    to = n > 8 ? (rd32(b + 9 * 4) >> 2) << 2 : from + (256u << 10);
    if (to > from) {
        dump_once("bt3_art_icons.bin", b + from, to - from);
    }
}

/* res: the options screen's pack, unpacked, of `size` bytes. */
void Port_NxOptionArt(void *res, uint32_t size) {
    extern int Port_PageAspectMilli(void); /* plat_stub.c: the shape of the page the menus are shown on */
    uint8_t *pack = res, *sec, *e, *pixels, *palette;
    uint32_t pix, pal, blt, uw, uh, dbw, tbw, x, y, i, k, v = 0;
    uint64_t tex0, reg;
    int32_t *gs, *at;

    dump_once("bt3_art_options.bin", res, size);
    for (i = 1; i < sizeof(kJoyconPage) / sizeof(kJoyconPage[0]); i++) { /* the picture made for the nearest shape */
        if (abs(kJoyconPage[i] - Port_PageAspectMilli()) < abs(kJoyconPage[v] - Port_PageAspectMilli())) {
            v = i;
        }
    }
    if (pack == NULL || rd32(pack) < 38) {
        fprintf(stderr, "bt3: optart: not the options screen's pack, left alone\n");
        return;
    }
    sec = pack + ((rd32(pack + 21 * 4) >> 2) << 2);
    e = sec + rd32(sec + 4) * 4; /* the texture table's first entry */
    pix = rd32(e);
    pal = rd32(e + 4);
    blt = rd32(e + 24);
    tex0 = rd64(e + 48);
    if (1u << ((tex0 >> 26) & 15) != 128 || 1u << ((tex0 >> 30) & 15) != 128 || ((tex0 >> 20) & 63) != 19 || (blt >> 8) != 0) {
        fprintf(stderr, "bt3: optart: the controller picture is not the 128 x 128 PSMT8 texture expected, left alone\n");
        return;
    }
    pixels = sec + pix * 4 + 96;
    palette = sec + pal * 4 + 96;
    reg = rd64(pixels - 48); /* the upload's size (TRXREG) */
    uw = (uint32_t)(reg & 4095);
    uh = (uint32_t)((reg >> 32) & 4095);
    dbw = blt & 63;
    tbw = (uint32_t)((tex0 >> 14) & 63);
    gs = malloc(GS_BYTES * sizeof(*gs));
    at = malloc(128 * 128 * sizeof(*at));
    if (gs == NULL || at == NULL || uw == 0 || uh == 0) {
        free(gs);
        free(at);
        return;
    }
    memset(gs, 0xFF, GS_BYTES * sizeof(*gs)); /* -1: no byte of the upload lands there */
    for (y = 0; y < uh; y++) {    /* where each byte of the upload lands in GS memory (PSMCT32) */
        for (x = 0; x < uw; x++) {
            uint32_t a = ((((y & ~31u) * dbw + ((x >> 1) & ~31u) + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + kCol32[y & 7][x & 7]);
            for (k = 0; k < 4 && a * 4 + k < GS_BYTES; k++) {
                gs[a * 4 + k] = (int32_t)((y * uw + x) * 4 + k);
            }
        }
    }
    for (y = 0; y < 128; y++) {   /* and which of those bytes the GS reads as each texel (PSMT8) */
        for (x = 0; x < 128; x++) {
            uint32_t col = (y & 15) >> 2, sx = x ^ ((col & 1) ? 4 : 0);
            uint32_t a = (((((y >> 1) & ~31u) * (tbw >> 1) + ((x >> 2) & ~31u) + kBlock32[(y >> 4) & 3][(x >> 4) & 7]) << 8) +
                          kCol8First[y & 3][sx & 15] + col * 64);
            at[y * 128 + x] = a < GS_BYTES ? gs[a] : -1;
            if (at[y * 128 + x] < 0) {
                fprintf(stderr, "bt3: optart: texel %u,%u of the controller picture is not in its upload, left alone\n", x, y);
                free(gs);
                free(at);
                return;
            }
        }
    }
    for (i = 0; i < 128 * 128; i++) {
        pixels[at[i]] = kJoyconIdx[v][i];
    }
    for (i = 0; i < 256; i++) { /* the palette in the GS's order of 256-colour palettes (entries 8..15 and 16..23 swapped) */
        uint32_t j = (i & 0xE7) | ((i & 8) << 1) | ((i & 16) >> 1);
        memcpy(palette + j * 4, kJoyconPal[v][i], 4);
    }
    free(gs);
    free(at);
    fprintf(stderr, "bt3: optart: the controller page shows the Joy-Con (drawn for a page of %d.%03d)\n", kJoyconPage[v] / 1000, kJoyconPage[v] % 1000);
}
#endif
