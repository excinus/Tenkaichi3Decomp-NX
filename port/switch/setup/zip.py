#!/usr/bin/env python3
"""A Switch release's zip for one system (docs/port/switch_plan.md, "The setup"): the setup for it, a README, and the
Switch's files (atmosphere/ and switch/ of port/build/Tenkaichi3Decomp-switch.zip, BT3_CC=nx64 port/tools/package.py),
which the setup copies to the SD card from beside itself.

    python3 port/switch/setup/zip.py win|linux|mac <setup program>
        -> port/build/Tenkaichi3Decomp-switch-windows.zip / -linux.zip / -macos.zip

The game's automatic update takes the Windows one (gs/net_update.c: its atmosphere/contents/<title>/ part)."""
import pathlib, sys, zipfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
FILES = ROOT / "port/build/Tenkaichi3Decomp-switch.zip"
NAMES = {"win": ("windows", "Tenkaichi3Decomp-switch-setup.exe"), "linux": ("linux", "Tenkaichi3Decomp-switch-setup"),
         "mac": ("macos", "Tenkaichi3Decomp-switch-setup")}

README = """Tenkaichi3Decomp for the Nintendo Switch

1. Unpack this whole zip into a folder on your PC.
2. Start {setup} (in that folder).{extra}
3. Choose where the game's data comes from: your disc image of the USA release (.iso), or your PC version of
   Tenkaichi3Decomp. Then choose your SD card, and press Install.
4. On the Switch, open the homebrew menu THROUGH A GAME (hold R while starting any game) and start Tenkaichi3Decomp.
   Recommended: put it on the HOME menu with a forwarder made by sphaira.

You need Atmosphere and sys-clk on the Switch (the HATS pack has both): the game sets the clocks it needs by itself.
In the game, + and - together open the settings; new releases install themselves from there (Update now).
More: https://github.com/excinus/Tenkaichi3Decomp-NX
"""
EXTRA = {"win": "", "linux": "",
         "mac": "\n   macOS may refuse it the first time (an unsigned program): right-click it, Open, then Open again."}


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in NAMES:
        sys.exit(__doc__)
    kind, setup = NAMES[sys.argv[1]]
    program = pathlib.Path(sys.argv[2])
    if not FILES.exists():
        sys.exit(f"missing {FILES.relative_to(ROOT)}: BT3_CC=nx64 port/tools/package.py first")
    out = ROOT / f"port/build/Tenkaichi3Decomp-switch-{kind}.zip"
    text = README.format(setup=setup, extra=EXTRA[sys.argv[1]])
    with zipfile.ZipFile(FILES) as src, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        info = zipfile.ZipInfo.from_file(program, setup)
        info.external_attr = (0o100755 << 16)  # (runnable once unpacked, on Linux and macOS)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, program.read_bytes())
        z.writestr("README.txt", text.replace("\n", "\r\n") if sys.argv[1] == "win" else text, compress_type=zipfile.ZIP_DEFLATED)
        for item in src.infolist():
            if item.filename.startswith(("atmosphere/", "switch/")):
                z.writestr(item, src.read(item), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    print(f"{out.relative_to(ROOT)}: {out.stat().st_size / 1e6:.1f} MB")


main()
