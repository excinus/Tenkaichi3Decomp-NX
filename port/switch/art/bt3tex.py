"""PS2 texture files of BT3, as port/src/plat_nxart.c reads them (written after the PS Vita port's tools/bt3tex.py, which
has the same tables): entries, 4- and 8-bit swizzled textures, palettes. For port/switch/art/button_art.py."""
import struct, numpy as np
B32 = [[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],[8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]]
C32 = [[0,1,4,5,8,9,12,13],[2,3,6,7,10,11,14,15],[16,17,20,21,24,25,28,29],[18,19,22,23,26,27,30,31],
       [32,33,36,37,40,41,44,45],[34,35,38,39,42,43,46,47],[48,49,52,53,56,57,60,61],[50,51,54,55,58,59,62,63]]
C8 = [[0,4,16,20,32,36,48,52,2,6,18,22,34,38,50,54],[8,12,24,28,40,44,56,60,10,14,26,30,42,46,58,62],
      [33,37,49,53,1,5,17,21,35,39,51,55,3,7,19,23],[41,45,57,61,9,13,25,29,43,47,59,63,11,15,27,31]]
B4 = [[0,2,8,10],[1,3,9,11],[4,6,12,14],[5,7,13,15],[16,18,24,26],[17,19,25,27],[20,22,28,30],[21,23,29,31]]
C4 = [[0,8,32,40,64,72,96,104,2,10,34,42,66,74,98,106,4,12,36,44,68,76,100,108,6,14,38,46,70,78,102,110],
      [16,24,48,56,80,88,112,120,18,26,50,58,82,90,114,122,20,28,52,60,84,92,116,124,22,30,54,62,86,94,118,126],
      [65,73,97,105,1,9,33,41,67,75,99,107,3,11,35,43,69,77,101,109,5,13,37,45,71,79,103,111,7,15,39,47],
      [81,89,113,121,17,25,49,57,83,91,115,123,19,27,51,59,85,93,117,125,21,29,53,61,87,95,119,127,23,31,55,63]]
u32 = lambda d, a: struct.unpack_from('<I', d, a)[0]
u64 = lambda d, a: struct.unpack_from('<Q', d, a)[0]

def count(d):
    table = u32(d, 4) * 4
    first = u32(d, table) * 4
    n = 0
    while table + (n + 1) * 64 <= len(d) and table + n * 64 < first:
        n += 1
    return n

def entry(d, i):
    e = u32(d, 4) * 4 + i * 64
    return dict(pix=u32(d, e), pal=u32(d, e + 4), blt=u32(d, e + 24), tex0=u64(d, e + 48), at=e)

def texture(d, i):
    en = entry(d, i); tex0 = en['tex0']
    w = 1 << ((tex0 >> 26) & 15); h = 1 << ((tex0 >> 30) & 15); psm = (tex0 >> 20) & 63
    if w > 512 or h > 512 or psm not in (19, 20) or en['blt'] >> 8: return None
    pixels = en['pix'] * 4 + 96; palette = en['pal'] * 4 + 96
    reg = u64(d, pixels - 48); uw = reg & 4095; uh = (reg >> 32) & 4095
    dbw = en['blt'] & 63; tbw = (tex0 >> 14) & 63
    gs = {}
    for y in range(uh):
        for x in range(uw):
            a = ((((y & ~31) * dbw + ((x >> 1) & ~31) + B32[(y >> 3) & 3][(x >> 3) & 7]) << 6) + C32[y & 7][x & 7])
            for k in range(4): gs[a * 4 + k] = (y * uw + x) * 4 + k
    n = 256 if psm == 19 else 16
    cols = []
    for i2 in range(n):
        j = ((i2 & 0xE7) | ((i2 & 8) << 1) | ((i2 & 16) >> 1)) if psm == 19 else i2
        cols.append(d[palette + j * 4: palette + j * 4 + 4])
    cols = np.frombuffer(b''.join(cols), np.uint8).reshape(n, 4)
    idx = np.zeros((h, w), np.uint8); src = np.zeros((h, w, 2), np.int64)
    for y in range(h):
        for x in range(w):
            col = (y & 15) >> 2; sx = x ^ (4 if col & 1 else 0)
            if psm == 19:
                a = (((((y >> 1) & ~31) * (tbw >> 1) + ((x >> 2) & ~31) + B32[(y >> 4) & 3][(x >> 4) & 7]) << 8) + C8[y & 3][sx & 15] + col * 64)
                ad, sh = a, 0
            else:
                a = (((((y >> 2) & ~31) * (tbw >> 1) + ((x >> 2) & ~31) + B4[(y >> 4) & 7][(x >> 5) & 3]) << 9) + C4[y & 3][sx & 31] + col * 128)
                ad, sh = a >> 1, (a & 1) * 4
            g = gs.get(ad)
            if g is None: return None
            idx[y, x] = (d[pixels + g] >> sh) & (n - 1); src[y, x] = (pixels + g, sh)
    return dict(w=w, h=h, psm=psm, idx=idx, cols=cols, src=src, palette=palette, rgba=cols[idx])
