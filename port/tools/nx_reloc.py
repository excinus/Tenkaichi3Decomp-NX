#!/usr/bin/env python3
"""The Switch build (BT3_CC=nx64): writes <program>.rel, the list of what the launcher changes when it loads the program
into another 4 GB window than the one it was linked for (docs/port/switch_plan.md, "The launcher").

The program is linked at NX_WINDOW + 0x20000000 (port/tools/link.py) and is moved as a whole with its window, by a
multiple of 4 GB. What does not change then: everything reached PC-relatively (all code: the window pass makes the
game's code find W from the program's own start), and the 4-byte window offsets in data (`g - W0`, the same in any
window). What does change: every 8-byte address of the program stored in its data, which the launcher moves by the
same amount. Those are
  - the targets of R_AARCH64_ABS64 relocations in the program's loaded sections (the link keeps its relocations,
    --emit-relocs), when the address stored there lies in the window (not a size or another number), and
  - the entries of the global offset table, which the linker fills itself without a relocation.
Anything else that would hold an absolute address of the window in an instruction (R_AARCH64_MOVW_UABS_*) stops
this tool: such code could not be moved.

    port/tools/nx_reloc.py [program]   (link.py runs it)  ->  <program>.rel:
        "BT3R", version (u32 1), the window it was linked for (u64), count (u32), then each place as its offset in
        the window (u32), ascending."""
import pathlib, struct, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from toolchain import EXE, NX_WINDOW

GB4 = 1 << 32
R_ABS64 = 257
MOVW_UABS = set(range(263, 271))  # R_AARCH64_MOVW_UABS_G0 .. G3_NC
SHF_ALLOC = 2
SHT_RELA, SHT_NOBITS = 4, 8

def sections(data):
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    out = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        out.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, info=info))
    strtab = out[shstrndx]
    for s in out:
        end = data.index(b"\0", strtab["off"] + s["name"])
        s["name"] = data[strtab["off"] + s["name"]:end].decode()
    return out

def main():
    exe = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else EXE)
    data = exe.read_bytes()
    secs = sections(data)

    def value_at(addr):
        for s in secs:
            if s["flags"] & SHF_ALLOC and s["type"] != SHT_NOBITS and s["addr"] <= addr < s["addr"] + s["size"]:
                return struct.unpack_from("<Q", data, s["off"] + addr - s["addr"])[0]
        return None

    places, bad, relas = set(), [], 0
    for r in secs:
        if r["type"] != SHT_RELA or not (secs[r["info"]]["flags"] & SHF_ALLOC):
            continue
        relas += 1
        for k in range(r["size"] // 24):
            offset, info, addend = struct.unpack_from("<QQq", data, r["off"] + k * 24)
            typ = info & 0xFFFFFFFF
            if typ == R_ABS64:
                v = value_at(offset)
                if v is not None and NX_WINDOW <= v < NX_WINDOW + GB4:
                    places.add(offset)
            elif typ in MOVW_UABS:
                bad.append(f"{offset:#x} ({secs[r['info']]['name']}, type {typ})")
    if relas == 0:
        sys.exit(f"nx_reloc: {exe.name} has no relocations (it must be linked with --emit-relocs)")
    if bad:
        sys.exit("nx_reloc: absolute addresses in instructions, which the launcher cannot move: " + ", ".join(bad[:10]))
    for s in secs:
        if s["name"] in (".got", ".got.plt") and s["type"] != SHT_NOBITS:
            for o in range(0, s["size"] - 7, 8):
                v = struct.unpack_from("<Q", data, s["off"] + o)[0]
                if NX_WINDOW <= v < NX_WINDOW + GB4:
                    places.add(s["addr"] + o)
    offsets = sorted(p - NX_WINDOW for p in places)
    if any(not (0 <= o < GB4) or o % 8 for o in offsets):
        sys.exit("nx_reloc: a place outside the window or not 8-byte aligned")
    out = pathlib.Path(str(exe) + ".rel")
    out.write_bytes(struct.pack("<4sIQI", b"BT3R", 1, NX_WINDOW, len(offsets)) + b"".join(struct.pack("<I", o) for o in offsets))
    print(f"{out.name}: {len(offsets)} addresses to move with the program")

if __name__ == "__main__":
    main()
