/*
 * The Switch only: the game's pictures of a PlayStation 2 pad and its buttons, said of the Switch.
 *
 *   - The options screen's controller page (src/menu/menu_x_c.c Option_Init hands its pack here as soon as it is
 *     unpacked): section 21's texture 0, the DualShock 2, becomes a pair of Joy-Con (nx_joycon_art.h, made by
 *     port/switch/art/joycon_art.py from joycons.svg: the whole texture and its palette, in the page's shape); its
 *     textures 2 and 3, the page's buttons plain and highlighted, take the Switch's names.
 *   - The button icons of the game's text (the boot file's member 8, bound by src/battle/col_c_b.c FontIcon_Init: the
 *     move lists, the hints, the tutorials) take the Switch's names too.
 * The names: the PlayStation's symbols become the letters of the Switch's buttons in the same places (Triangle X,
 * Circle A, Square Y, Cross B), L1 / L2 / R1 / R2 L / ZL / R / ZR, L3 / R3 LS / RS, SELECT and START - and +
 * (nx_button_art.h, made by port/switch/art/button_art.py): only the texels that change, and for each texture a hash
 * of its texels as the USA disc has them, checked first (another disc's is left alone, and the log says so).
 *
 * A texture's bytes are the GS's 32-bit image that the game uploads, its texels in the GS's 8-bit (PSMT8) or 4-bit
 * (PSMT4) order inside it: which byte holds a texel is worked out the way the GS would read it (the tables below, as
 * port/src/gs/gs_core.c and the PS Vita port's tools/bt3tex.py have them). After the PS Vita port (its
 * port/vita/vita_optart.c changes the same picture into a Vita, and L3 / R3 into plates).
 */
#ifdef __SWITCH__
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int entry, w, h;
    uint32_t hash;
    int first, count;
} NxArtTex;

#include "nx_joycon_art.h"
#include "nx_button_art.h"

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
static const uint8_t kBlock4[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                      {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
static const uint8_t kCol4[4][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63}};

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

/* One texture of a texture file, as this file reads and writes it: where each texel is (a byte of the file and,
   4-bit, which half of it), and its palette. */
typedef struct {
    uint8_t *file, *palette;
    int w, h, psm;
    int32_t *at;   /* per texel, row by row: its byte, counted from the file's start */
    uint8_t *half; /* per texel: 0 the low half of the byte (and every 8-bit one), 4 the high one */
} Tex;

static void tex_free(Tex *t) {
    free(t->at);
    free(t->half);
    t->at = NULL;
    t->half = NULL;
}

static uint32_t tex_get(const Tex *t, int i) {
    return t->psm == 19 ? t->file[t->at[i]] : (uint32_t)(t->file[t->at[i]] >> t->half[i]) & 15;
}

static void tex_put(const Tex *t, int i, uint32_t v) {
    uint8_t *b = &t->file[t->at[i]];
    if (t->psm == 19) {
        *b = (uint8_t)v;
    } else {
        *b = (uint8_t)((*b & ~(15 << t->half[i])) | ((v & 15) << t->half[i]));
    }
}

/* Texture `entry` of the texture file at `file`: 1 and *t filled, or 0 (not an 8- or 4-bit texture this reads). */
static int tex_open(uint8_t *file, int entry, Tex *t) {
    uint8_t *e = file + rd32(file + 4) * 4 + entry * 64;
    uint32_t pix = rd32(e), pal = rd32(e + 4), blt = rd32(e + 24), uw, uh, dbw, tbw, x, y, k, words = 0, n;
    uint64_t tex0 = rd64(e + 48), reg;
    int32_t *gs;

    memset(t, 0, sizeof(*t));
    t->file = file;
    t->w = 1 << ((tex0 >> 26) & 15);
    t->h = 1 << ((tex0 >> 30) & 15);
    t->psm = (int)((tex0 >> 20) & 63);
    if (t->w > 512 || t->h > 512 || (t->psm != 19 && t->psm != 20) || (blt >> 8) != 0) {
        return 0;
    }
    t->palette = file + pal * 4 + 96;
    reg = rd64(file + pix * 4 + 96 - 48); /* the upload's size (TRXREG) */
    uw = (uint32_t)(reg & 4095);
    uh = (uint32_t)((reg >> 32) & 4095);
    dbw = blt & 63;
    tbw = (uint32_t)((tex0 >> 14) & 63);
    if (uw == 0 || uh == 0) {
        return 0;
    }
    for (y = 0; y < uh; y++) { /* (how much of GS memory the upload spans) */
        for (x = 0; x < uw; x++) {
            uint32_t a = ((((y & ~31u) * dbw + ((x >> 1) & ~31u) + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + kCol32[y & 7][x & 7]);
            words = a + 1 > words ? a + 1 : words;
        }
    }
    n = (uint32_t)(t->w * t->h);
    gs = malloc((size_t)words * 4 * sizeof(*gs));
    t->at = malloc(n * sizeof(*t->at));
    t->half = malloc(n);
    if (gs == NULL || t->at == NULL || t->half == NULL) {
        free(gs);
        tex_free(t);
        return 0;
    }
    memset(gs, 0xFF, (size_t)words * 4 * sizeof(*gs)); /* -1: no byte of the upload lands there */
    for (y = 0; y < uh; y++) { /* where each byte of the upload lands in GS memory (PSMCT32) */
        for (x = 0; x < uw; x++) {
            uint32_t a = ((((y & ~31u) * dbw + ((x >> 1) & ~31u) + kBlock32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + kCol32[y & 7][x & 7]);
            for (k = 0; k < 4; k++) {
                gs[a * 4 + k] = (int32_t)(pix * 4 + 96 + (y * uw + x) * 4 + k);
            }
        }
    }
    for (y = 0; y < (uint32_t)t->h; y++) { /* and which of those bytes the GS reads as each texel */
        for (x = 0; x < (uint32_t)t->w; x++) {
            uint32_t col = (y & 15) >> 2, sx = x ^ ((col & 1) ? 4 : 0), a, at, i = y * (uint32_t)t->w + x;
            if (t->psm == 19) {
                a = (((((y >> 1) & ~31u) * (tbw >> 1) + ((x >> 2) & ~31u) + kBlock32[(y >> 4) & 3][(x >> 4) & 7]) << 8) +
                     kCol8First[y & 3][sx & 15] + col * 64);
                at = a;
                t->half[i] = 0;
            } else {
                a = (((((y >> 2) & ~31u) * (tbw >> 1) + ((x >> 2) & ~31u) + kBlock4[(y >> 4) & 7][(x >> 5) & 3]) << 9) +
                     kCol4[y & 3][sx & 31] + col * 128);
                at = a >> 1;
                t->half[i] = (uint8_t)((a & 1) * 4);
            }
            t->at[i] = at < words * 4 ? gs[at] : -1;
            if (t->at[i] < 0) {
                free(gs);
                tex_free(t);
                return 0;
            }
        }
    }
    free(gs);
    return 1;
}

static uint32_t tex_hash(const Tex *t) {
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < t->w * t->h; i++) {
        h = (h ^ tex_get(t, i)) * 16777619u;
    }
    return h;
}

/* The changes of a table (nx_button_art.h) to the texture file at `file`: returns how many textures were changed. */
static int apply(uint8_t *file, const NxArtTex *list, int n, const unsigned short *changes, const char *what) {
    int k, done = 0;
    for (k = 0; k < n; k++) {
        Tex t;
        int c;
        if (!tex_open(file, list[k].entry, &t)) {
            fprintf(stderr, "bt3: art: %s texture %d is not one this reads, left alone\n", what, list[k].entry);
            continue;
        }
        if (t.w != list[k].w || t.h != list[k].h || tex_hash(&t) != list[k].hash) {
            fprintf(stderr, "bt3: art: %s texture %d is not the USA disc's, left alone\n", what, list[k].entry);
            tex_free(&t);
            continue;
        }
        for (c = 0; c < list[k].count; c++) {
            tex_put(&t, changes[list[k].first + c * 2], changes[list[k].first + c * 2 + 1]);
        }
        tex_free(&t);
        done++;
    }
    return done;
}

/* For redrawing the pictures again (port/switch/art/button_art.py): the bytes they are made from, written once beside
   bt3_log.txt when BT3_ART_DUMP is set. */
static void dump_once(const char *name, const void *p, uint32_t size) {
    FILE *f;
    if (getenv("BT3_ART_DUMP") == NULL) {
        return;
    }
    if ((f = fopen(name, "rb")) != NULL) {
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
   bound: the icons with the Switch's names. */
void Port_NxIconArt(void *boot) {
    uint8_t *b = boot;
    uint32_t n, from, to;
    if (b == NULL || (n = rd32(b)) < 8 || n > 64) {
        return;
    }
    from = (rd32(b + 8 * 4) >> 2) << 2;
    to = n > 8 ? (rd32(b + 9 * 4) >> 2) << 2 : from + (256u << 10);
    dump_once("bt3_art_icons.bin", b + from, to > from ? to - from : 0);
    fprintf(stderr, "bt3: art: %d of %d button icons with the Switch's names\n",
            apply(b + from, kIconTex, (int)(sizeof(kIconTex) / sizeof(kIconTex[0])), kIconChanges, "icon"),
            (int)(sizeof(kIconTex) / sizeof(kIconTex[0])));
}

/* res: the options screen's pack, unpacked, of `size` bytes. */
void Port_NxOptionArt(void *res, uint32_t size) {
    extern int Port_PageAspectMilli(void); /* plat_stub.c: the shape of the page the menus are shown on */
    uint8_t *pack = res, *sec;
    uint32_t i, v = 0;
    Tex t;

    if (pack == NULL || rd32(pack) < 38) {
        fprintf(stderr, "bt3: optart: not the options screen's pack, left alone\n");
        return;
    }
    dump_once("bt3_art_options.bin", res, size);
    sec = pack + ((rd32(pack + 21 * 4) >> 2) << 2);
    for (i = 1; i < sizeof(kJoyconPage) / sizeof(kJoyconPage[0]); i++) { /* the picture made for the nearest shape */
        if (abs(kJoyconPage[i] - Port_PageAspectMilli()) < abs(kJoyconPage[v] - Port_PageAspectMilli())) {
            v = i;
        }
    }
    if (!tex_open(sec, 0, &t) || t.w != 128 || t.h != 128 || t.psm != 19) {
        fprintf(stderr, "bt3: optart: the controller picture is not the 128 x 128 PSMT8 texture expected, left alone\n");
    } else {
        for (i = 0; i < 128 * 128; i++) {
            tex_put(&t, (int)i, kJoyconIdx[v][i]);
        }
        for (i = 0; i < 256; i++) { /* the palette in the GS's order of 256-colour palettes (8..15 and 16..23 swapped) */
            uint32_t j = (i & 0xE7) | ((i & 8) << 1) | ((i & 16) >> 1);
            memcpy(t.palette + j * 4, kJoyconPal[v][i], 4);
        }
        fprintf(stderr, "bt3: optart: the controller page shows the Joy-Con (drawn for a page of %d.%03d)\n", kJoyconPage[v] / 1000,
                kJoyconPage[v] % 1000);
    }
    tex_free(&t);
    fprintf(stderr, "bt3: optart: %d of %d of the page's button sheets with the Switch's names\n",
            apply(sec, kOptTex, (int)(sizeof(kOptTex) / sizeof(kOptTex[0])), kOptChanges, "options"),
            (int)(sizeof(kOptTex) / sizeof(kOptTex[0])));
}
#endif
