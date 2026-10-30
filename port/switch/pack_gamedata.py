#!/usr/bin/env python3
"""Packs the game data's large folders into one file each, for the Nintendo Switch (docs/port/switch_plan.md,
"The packs").

    python pack_gamedata.py <gamedata folder> [--delete] [--out <folder>]

On the Switch's SD card, opening a file of a folder with tens of thousands of files takes about 0.4 s: the console
reads the folder from its start to find the name. gamedata/pzs3us2 (the disc's archive PZS3US2.AFS, which the PC
setup unpacks, port/tools/extract_disc.py) is about 65,000 files, and the game opens one for every spoken line: in
the menus every move of the cursor stopped for a second or more, in a fight the same at every line. This writes each
such folder (pzs3..., 10,000 files or more, named <number>.bin) as <folder>.afs next to it: an archive of the disc's
own kind, the same data in one file. The Switch build reads a packed folder's files from its pack and does not look
at the folder (port/src/plat_nxfs.c). The PC build does not read the packs: run this on the SD card's copy.

  --delete        then remove the folder (the pack holds the same files; checked before). Without it the folder is
                  kept and can be deleted by hand; the Switch reads the pack either way.
  --out <folder>  write the packs into another gamedata folder (e.g. from the PC installation onto the card),
                  which then needs no copy of the large folders.

Python 3 only, nothing to install. Nothing it writes may be redistributed (it is the disc's data)."""
import os, re, shutil, struct, sys

MIN_FILES = 10000
ALIGN = 2048  # (as the disc's archives)
NAME = re.compile(r"^(\d{1,6})\.bin$")


def numbered(folder):
    """{index: path} of the folder's <number>.bin files."""
    out = {}
    with os.scandir(folder) as it:
        for e in it:
            m = NAME.match(e.name)
            if m and e.is_file():
                out[int(m.group(1))] = e.path
    return out


def pack(folder, files, dest):
    count = max(files) + 1
    table = [(0, 0)] * count
    tmp = dest + ".part"
    at = (8 + count * 8 + ALIGN - 1) // ALIGN * ALIGN
    done = 0
    with open(tmp, "wb") as out:
        out.write(b"\0" * at)
        for i in range(count):
            p = files.get(i)
            if p is None:
                continue
            with open(p, "rb") as f:
                data = f.read()
            if at + len(data) >= 1 << 32:
                out.close()
                os.remove(tmp)
                raise SystemExit(f"{folder}: over 4 GB, more than an archive of this kind holds")
            out.seek(at)
            out.write(data)
            table[i] = (at, len(data))
            at = (at + len(data) + ALIGN - 1) // ALIGN * ALIGN
            done += 1
            if done % 5000 == 0:
                print(f"  {done:,} of {len(files):,} files", flush=True)
        out.truncate(at)
        out.seek(0)
        out.write(b"AFS\0" + struct.pack("<I", count))
        out.write(b"".join(struct.pack("<II", o, s) for o, s in table))
    check(folder, files, tmp)
    os.replace(tmp, dest)
    return at


def check(folder, files, path):
    """The pack against the folder: every file's size, and every 250th file's bytes."""
    with open(path, "rb") as f:
        magic, count = struct.unpack("<4sI", f.read(8))
        table = struct.unpack(f"<{count * 2}I", f.read(count * 8))
        for n, (i, p) in enumerate(sorted(files.items())):
            off, size = table[i * 2], table[i * 2 + 1]
            if magic != b"AFS\0" or off == 0 or size != os.path.getsize(p):
                raise SystemExit(f"{path}: {p} is not in it as it should be (nothing was deleted)")
            if n % 250 == 0:
                f.seek(off)
                with open(p, "rb") as g:
                    if f.read(size) != g.read():
                        raise SystemExit(f"{path}: {p} differs from the folder's (nothing was deleted)")


def main():
    args = sys.argv[1:]
    delete = "--delete" in args
    out = None
    if "--out" in args:
        k = args.index("--out")
        if k + 1 >= len(args):
            raise SystemExit(__doc__)
        out = args[k + 1]
        del args[k:k + 2]
    args = [a for a in args if a != "--delete"]
    if len(args) != 1 or not os.path.isdir(args[0]):
        raise SystemExit(__doc__)
    data = args[0]
    if not os.path.isdir(os.path.join(data, "disc")) and not any(n.startswith("pzs3") for n in os.listdir(data)):
        raise SystemExit(f"{data}: not a gamedata folder (no disc/ or pzs3... folders in it)")
    if out is not None:
        os.makedirs(out, exist_ok=True)
    found = 0
    for name in sorted(os.listdir(data)):
        folder = os.path.join(data, name)
        if not name.startswith("pzs3") or not os.path.isdir(folder):
            continue
        files = numbered(folder)
        if len(files) < MIN_FILES:
            continue
        found += 1
        dest = os.path.join(out if out is not None else data, name + ".afs")
        print(f"{folder}: {len(files):,} files -> {dest}", flush=True)
        size = pack(folder, files, dest)
        print(f"  done, {size / (1 << 20):,.0f} MB, checked", flush=True)
        if delete and out is None:
            print(f"  removing {folder} (the pack has its files)", flush=True)
            shutil.rmtree(folder)
    if found == 0:
        print(f"{data}: no folder of {MIN_FILES:,} files or more: nothing to pack")


main()
