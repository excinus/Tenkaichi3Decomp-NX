"""The game's button icons with the Switch's names: the PlayStation's symbols on its buttons become the letters of the
Switch's in the same places (Triangle X, Circle A, Square Y, Cross B), L1 / L2 / R1 / R2 become L / ZL / R / ZR, L3 / R3
LS / RS, SELECT and START - and +. The game's own buttons are kept (their shapes, shading, rims, the yellow of the
highlighted ones); only the symbol on each is taken off (filled with the button's face around it) and a white letter
drawn in its place, in the texture's own colours.

    .venv/bin/python port/switch/art/button_art.py <bt3_art_icons.bin> <bt3_art_options.bin> [preview.png]

The two files are what the game writes beside its log on a console (port/src/plat_nxart.c): the boot file's member 8
(the icons of the game's text: move lists, hints, tutorials) and the options screen's pack (section 21's textures 2 and
3: the controller page's buttons, plain and highlighted). They are the disc's art and are not kept in the repository;
neither is it in what this writes, port/src/nx_button_art.h: for each texture only the texels that change, with their
new colour index, and a hash (FNV-1a) of the texture's own texels, which plat_nxart.c checks before it changes one."""
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bt3tex import count, texture, u32

HERE = Path(__file__).resolve().parent
OUT = HERE.parent.parent / "src" / "nx_button_art.h"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SS = 8

def lum(c):
    return 0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]

def sat(c):
    c = c[..., :3].astype(int)
    return c.max(-1) - c.min(-1)

def hue_name(c):
    r, g, b = (int(v) for v in c[:3])
    if r > 150 and g > 150 and b < 110:
        return None                    # yellow: the highlight, the hold arrow
    if g > r + 30 and g > b:
        return "triangle"
    if r > g + 40 and b > g + 30:
        return "square"
    if r > g + 40 and r > b + 40:
        return "circle"
    if b > g + 20 and b >= r - 30:
        return "cross"
    return None

LETTER = {"triangle": "X", "circle": "A", "square": "Y", "cross": "B"}

def glyph(text, height, stroke=0):
    """coverage of the text (and of its outline) at `height` pixels of ink: two float arrays"""
    f = ImageFont.truetype(FONT, height * SS * 2)
    im = Image.new("L", (height * SS * 6, height * SS * 4), 0)
    ol = Image.new("L", im.size, 0)
    d = ImageDraw.Draw(im)
    d.text((height * SS, height * SS), text, font=f, fill=255)
    if stroke:
        ImageDraw.Draw(ol).text((height * SS, height * SS), text, font=f, fill=255, stroke_width=stroke * SS * 2, stroke_fill=255)
    box = (ol if stroke else im).getbbox()
    im, ol = im.crop(box), ol.crop(box)
    scale = height / ((im.getbbox()[3] - im.getbbox()[1]) / 1.0) if not stroke else height / ol.height
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    a = np.array(im.resize((w, h), Image.BOX), float) / 255
    b = np.array(ol.resize((w, h), Image.BOX), float) / 255
    return a, b

class Tex:
    def __init__(self, data, i):
        self.t = texture(data, i)
        self.idx = self.t["idx"].copy()
        self.cols = self.t["cols"].astype(int)            # alpha 0..128
        self.orig = self.t["idx"].copy()

    def rgba(self):
        return self.cols[self.idx]

    def nearest(self, rgb, opaque=True):
        c = self.cols
        ok = (c[:, 3] >= 100) if opaque else (c[:, 3] >= 0)
        d = ((c[:, :3] - np.array(rgb)) ** 2).sum(1) + np.where(ok, 0, 10 ** 9)
        return int(np.argmin(d))

    def inpaint_rows(self, erase, source):
        """erased texels take the index of the nearest texel of `source` in the same row (the buttons' faces are shaded
        from top to bottom, so a row keeps its shade); what a row cannot fill is left to inpaint"""
        h, w = self.idx.shape
        left = erase.copy()
        for y, x in zip(*np.nonzero(erase)):
            best = None
            for dx in range(1, w):
                for xx in (x - dx, x + dx):
                    if 0 <= xx < w and source[y, xx] and not erase[y, xx]:
                        best = xx
                        break
                if best is not None:
                    break
            if best is not None:
                self.idx[y, x] = self.idx[y, best]
                left[y, x] = False
        self.inpaint(left, source | (erase & ~left))

    def inpaint(self, erase, source):
        """erased texels take the index of the nearest texel of `source` (a mask), spreading out from it"""
        idx = self.idx
        known = source & ~erase
        todo = erase.copy()
        h, w = idx.shape
        while todo.any():
            grown = False
            new = idx.copy()
            nk = known.copy()
            for y, x in zip(*np.nonzero(todo)):
                for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (-1, -1), (1, -1), (-1, 1)):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h and 0 <= xx < w and known[yy, xx]:
                        new[y, x] = idx[yy, xx]
                        nk[y, x] = True
                        grown = True
                        break
            idx[:], known[:] = new, nk
            todo &= ~known
            if not grown:
                break

    def draw(self, text, cy, cx, height, stroke=0):
        a, o = glyph(text, height, stroke)
        h, w = a.shape
        y0, x0 = int(round(cy - h / 2)), int(round(cx - w / 2))
        rgba = self.rgba()
        for y in range(h):
            for x in range(w):
                yy, xx = y0 + y, x0 + x
                if not (0 <= yy < self.idx.shape[0] and 0 <= xx < self.idx.shape[1]):
                    continue
                cov, out = a[y, x], o[y, x]
                if cov < 0.08 and out < 0.25:
                    continue
                base = rgba[yy, xx, :3].astype(float)
                if stroke and out >= 0.25:
                    base = base * (1 - out) + np.array([8, 8, 8]) * out if rgba[yy, xx, 3] > 60 else np.array([8, 8, 8])
                col = base * (1 - cov) + 255 * cov
                self.idx[yy, xx] = self.nearest(col)

    def components(self, mask):
        """8-connected parts of a mask: list of (ys, xs)"""
        seen = np.zeros_like(mask)
        out = []
        h, w = mask.shape
        for y, x in zip(*np.nonzero(mask)):
            if seen[y, x]:
                continue
            stack, ys, xs = [(y, x)], [], []
            seen[y, x] = True
            while stack:
                cy, cx = stack.pop()
                ys.append(cy)
                xs.append(cx)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        yy, xx = cy + dy, cx + dx
                        if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not seen[yy, xx]:
                            seen[yy, xx] = True
                            stack.append((yy, xx))
            out.append((np.array(ys), np.array(xs)))
        return out

def face_buttons(t, region):
    """each PlayStation symbol inside `region` (a mask): off, and the Switch's letter in its place"""
    rgba = t.rgba()
    coloured = region & (rgba[..., 3] > 40) & (sat(rgba) > 70)
    named = np.zeros(coloured.shape, object)
    for y, x in zip(*np.nonzero(coloured)):
        named[y, x] = hue_name(rgba[y, x])
    marks = coloured & (named != None)
    done = []
    for ys, xs in t.components(marks):
        if len(ys) < 4:
            continue
        kinds = [named[y, x] for y, x in zip(ys, xs)]
        kind = max(set(kinds), key=kinds.count)
        y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
        box = np.zeros(region.shape, bool)
        box[max(0, y0 - 1):y1 + 2, max(0, x0 - 1):x1 + 2] = True
        erase = box & region & (sat(rgba) > 25) & (rgba[..., 3] > 40) & np.vectorize(lambda v: v is not None or True)(named)
        erase &= ~((rgba[..., 0] > 150) & (rgba[..., 1] > 150) & (rgba[..., 2] < 110))     # (not the yellow)
        face = region & (rgba[..., 3] > 100) & (lum(rgba) < 110) & ~erase
        t.inpaint_rows(erase, face)
        size = y1 - y0 + 1
        t.draw(LETTER[kind], (y0 + y1) / 2, (x0 + x1) / 2, max(5, int(round(size * 0.92))))
        done.append(kind)
    return done

def plate_label(t, region, new):
    """the light lettering inside a dark plate (L1, R2...) off, `new` in its place"""
    rgba = t.rgba()
    dark = region & (rgba[..., 3] > 100) & (lum(rgba) < 100)
    ys, xs = np.nonzero(dark)
    y0, y1, x0, x1 = ys.min() + 2, ys.max() - 2, xs.min() + 2, xs.max() - 2
    inside = np.zeros(region.shape, bool)
    inside[y0:y1 + 1, x0:x1 + 1] = True
    text = inside & region & (lum(rgba) > 120) & (sat(rgba) < 60)
    inside[y0 - 1:y1 + 2, x0 - 1:x1 + 2] = True                  # (the lettering's own edge may reach a row further)
    ty, tx = np.nonzero(text)
    erase = np.zeros(region.shape, bool)
    erase[max(0, ty.min() - 1):ty.max() + 2, max(0, tx.min() - 1):tx.max() + 2] = True
    erase &= inside & region                                     # (the whole lettering: its grey edges too)
    t.inpaint_rows(erase, inside & region & ~erase)              # (the plate's own shading, its lighter band included)
    t.draw(new, (ty.min() + ty.max()) / 2, (x0 + x1) / 2, ty.max() - ty.min() + 1)

def stick_label(t, region, new):
    """the white label with its black outline on a stick (L3, R3) off, `new` (with an outline) in its place"""
    rgba = t.rgba()
    clear = rgba[..., 3] < 20
    near = np.zeros_like(clear)                                  # within 2 texels of the clear outside: the icon's outline
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            near |= np.roll(np.roll(clear, dy, 0), dx, 1)
    ry, rx = np.nonzero(region)
    lower = np.zeros_like(clear)
    lower[(ry.min() + ry.max()) // 2:ry.max() + 1, rx.min():rx.max() + 1] = True
    white = region & lower & ~near & (lum(rgba) > 200) & (rgba[..., 3] > 100)
    ys, xs = np.nonzero(white)
    box = np.zeros(region.shape, bool)
    box[max(0, ys.min() - 1):ys.max() + 2, max(0, xs.min() - 1):xs.max() + 2] = True
    erase = box & region & ((lum(rgba) > 200) | (lum(rgba) < 30))
    t.inpaint(erase, region & ~erase)
    t.draw(new, (ys.min() + ys.max()) / 2, (xs.min() + xs.max()) / 2, ys.max() - ys.min() + 3, stroke=1)

def round_button(t, sign):
    """SELECT's bar or START's triangle: a round button of the face buttons' kind, with - or + on it"""
    h, w = t.idx.shape
    y, x = np.mgrid[0:h, 0:w]
    cy, cx, r = h / 2 - 0.5, w / 2 - 0.5, min(h, w) * 0.34
    dist = np.hypot(y - cy, x - cx)
    clear = int(np.argmin(t.cols[:, 3]))
    t.idx[:] = clear
    for yy in range(h):
        for xx in range(w):
            d = dist[yy, xx]
            if d <= r + 1.2:
                if d > r - 0.2:
                    col = (235, 235, 235)                                     # the light rim
                else:
                    shade = 70 - 40 * (yy - (cy - r)) / (2 * r)                 # the face, lighter at the top
                    col = (shade, shade, shade + 4)
                t.idx[yy, xx] = t.nearest(col)
    if sign == "+":
        t.draw("+", cy, cx, max(5, int(round(r * 1.0))))
    else:
        half = max(2, int(round(r * 0.5)))
        white = t.nearest((255, 255, 255))
        t.idx[int(cy) - 0:int(cy) + 2, int(round(cx)) - half:int(round(cx)) + half] = white

def icons(data):
    out = {}
    for i in range(count(data)):
        t = Tex(data, i)
        if t.t is None:
            continue
        h, w = t.idx.shape
        full = np.ones((h, w), bool)
        tiles = [full] if (h, w) == (32, 32) else []
        if not tiles:                                         # the 64 x 64 ones: a button a quarter
            for ty in range(0, h, 32):
                for tx in range(0, w, 32):
                    m = np.zeros((h, w), bool)
                    m[ty:ty + 32, tx:tx + 32] = True
                    if (t.rgba()[ty:ty + 32, tx:tx + 32, 3] > 40).any():
                        tiles.append(m)
        if i in (10, 11, 12, 13, 20, 21, 22, 23, 24, 25):
            for m in tiles:
                face_buttons(t, m)
        elif i in (14, 15, 17, 18):
            plate_label(t, full, {14: "L", 15: "ZL", 17: "R", 18: "ZR"}[i])
        elif i in (16, 19, 26, 27):
            for m in tiles:
                stick_label(t, m, "LS" if i in (16, 26) else "RS")
        elif i in (8, 9):
            round_button(t, "-" if i == 8 else "+")
        else:
            continue
        out[i] = t
    return out

def options(pack):
    a = (u32(pack, 21 * 4) >> 2) << 2
    sec = pack[a:]
    out = {}
    for i in (2, 3):
        t = Tex(sec, i)
        h, w = t.idx.shape
        rgba = t.rgba()
        bodies = [p for p in t.components((rgba[..., 3] > 100) & (lum(rgba) < 100)) if len(p[0]) > 40]
        plates = []
        for ys, xs in bodies:                                   # each button's dark body, a little round it
            m = np.zeros((h, w), bool)
            m[max(0, ys.min() - 3):ys.max() + 4, max(0, xs.min() - 3):xs.max() + 4] = True
            if face_buttons(t, m):
                continue
            plates.append((ys.min(), xs.min(), m))
        plates.sort(key=lambda p: (p[0] // 8, p[1]))           # by row, then left to right: L2 R2 / L1 R1
        for (y, x, m), new in zip(plates, ("ZL", "ZR", "L", "R")):
            plate_label(t, m, new)
        out[i] = t
    return out

def fnv(values):
    h = 2166136261
    for v in values:
        h = ((h ^ int(v)) * 16777619) & 0xFFFFFFFF
    return h

def emit(name, texs):
    lines = ["static const unsigned short k%sChanges[] = { /* per texture: its changed texels, (index, new colour) */\n" % name]
    table = []
    at = 0
    for i, t in sorted(texs.items()):
        ch = np.nonzero(t.idx.reshape(-1) != t.orig.reshape(-1))[0]
        flat = t.idx.reshape(-1)
        pairs = []
        for k in ch:
            pairs += [int(k), int(flat[k])]
        lines.append("    " + ",".join(str(v) for v in pairs) + ",\n" if pairs else "")
        table.append((i, t.idx.shape[1], t.idx.shape[0], fnv(t.orig.reshape(-1)), at, len(ch)))
        at += len(pairs)
    lines.append("};\nstatic const NxArtTex k%sTex[] = { /* entry, w, h, the hash of its texels, first change, changes */\n" % name)
    for row in table:
        lines.append("    {%d, %d, %d, 0x%08Xu, %d, %d},\n" % row)
    lines.append("};\n")
    return lines

def main():
    ic = icons(open(sys.argv[1], "rb").read())
    op = options(open(sys.argv[2], "rb").read())
    lines = ["/* Generated by port/switch/art/button_art.py: the button icons with the Switch's names (port/src/plat_nxart.c).\n",
             "   Only the texels that change, and a hash of each texture as the USA disc has it. */\n"]
    lines += emit("Icon", ic) + emit("Opt", op)
    OUT.write_text("".join(lines))
    print("wrote", OUT, "-", len(ic), "icons,", len(op), "sheets")
    if len(sys.argv) > 3:
        ims = []
        for t in list(ic.values()) + list(op.values()):
            for src in (t.orig, t.idx):
                c = t.cols[src].astype(int)
                c[..., 3] = np.minimum(255, c[..., 3] * 2)
                ims.append(Image.fromarray(c.astype(np.uint8), "RGBA"))
        cell = 140
        W = 8
        sheet = Image.new("RGBA", (W * cell, ((len(ims) + W - 1) // W) * cell), (60, 90, 150, 255))
        for k, im in enumerate(ims):
            sheet.alpha_composite(im, ((k % W) * cell, (k // W) * cell))
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(sys.argv[3])

if __name__ == "__main__":
    main()
