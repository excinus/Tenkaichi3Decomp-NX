#!/usr/bin/env python3
"""Makes the release folder and zip: the finished port without any game data, plus the setup window.

    BT3_CC=clang64 python3 port/tools/undefined.py && BT3_CC=clang64 python3 port/tools/link.py
    port/setup/build.sh
    python3 port/tools/package.py          -> port/build/release/ and port/build/Tenkaichi3Decomp-linux-x64.zip
    BT3_CC=win64 python3 port/tools/package.py   (after the win64 build and `port/setup/build.sh win`)
                                           -> port/build/release-win/ and port/build/Tenkaichi3Decomp-windows-x64.zip
    BT3_CC=nx64 python3 port/tools/package.py    (after the Switch build; docs/port/switch_plan.md)
                                           -> port/build/release-switch/ and port/build/Tenkaichi3Decomp-switch.zip
    BT3_CC=mac64 python3 port/tools/package.py   (on a Mac, after the mac64 build and `port/setup/build.sh`; EXPERIMENTAL)
                                           -> port/build/release-mac/ and port/build/Tenkaichi3Decomp-macos-arm64.zip

Contents:  Tenkaichi3Decomp, Tenkaichi3Decomp.dat   the game without the data tables of the original programs (strip_data.py)
           Tenkaichi3Decomp-setup       the setup window: reads the user's own disc image and unpacks it into gamedata/
           lib/            SDL3, so that it does not have to be installed
           a11y_<code>.txt the screen reader's lines in other languages (port/lang; its own labels, no game text)

           fonts/          the fonts of F1 > UI with their licences (port/third_party/fonts as it is)
           translations/labels/   menu and battle language catalogs
           translations/ui/       optional bundled game UI catalogs
           translations/port/     text added by the PC port
           tbo/ui/         the pictures of the Tenkaichi Budokai Online screens (port/tbo/ui), their credits in licenses/
           README.txt, licenses/
The user needs nothing but their disc image. Nothing from the disc is in the zip."""
import os, pathlib, re, shutil, subprocess, sys, zipfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

ROOT = pathlib.Path(__file__).resolve().parents[2]
NAME = "Tenkaichi3Decomp"  # the program's name in a release: NAME, NAME.dat, NAME-setup
OUT = ROOT / "port/build/release"
ZIP = ROOT / "port/build/Tenkaichi3Decomp-linux-x64.zip"

README = """Dragon Ball Z: Budokai Tenkaichi 3 - PC port (Linux, 64-bit)

1. Start  Tenkaichi3Decomp-setup
2. Choose your own disc image of the USA release (SLUS-21678), an .iso file.
   The setup checks it and unpacks the game's data next to this file. About 4 GB are needed.
   Optional: a disc image of the European release (SLES-54945) as well, for Spanish, German, French and
   Italian (menus, battle screen and text as that release has them). It can also be added later: start the
   setup again and press "Add languages...".
3. Press Play. Later, start  Tenkaichi3Decomp  itself (or the setup again, and press Play).

In the game, F1 opens the settings: resolution, widescreen, controls and sound.
With the European languages added, the first launch shows a language list in the game window before loading game or memory card data. Change it later with F1 > UI > Language.
Saves are kept in saves/. A file placed in gamedata/mods/<same path as the original> replaces the original.
Texture packs (PCSX2 naming, .dds or .png) go into textures/, stages and music from outside the disc into
stages/ and songs/: see the note in each folder.
F1 > UI has three font settings, all starting on "Original": "Menu font" draws menu labels, "Battle font" draws
announcements and HUD text, and "Dialogue / UI font" draws the game's ordinary text. Their fonts and licences are
in fonts/. To add a .ttf, put it there and list it in fonts.tsv. The third column says where it can be used:
1 = Menu, 2 = Dialogue / UI, 4 = Battle, added up for several (3 = Menu and Dialogue / UI, 7 = all three).
The game makes the menu and battle label list from your disc data on its first start (gamedata/game-text/, in the
background). Menu and Battle font choices appear when the list is ready.

Screen reader: with Prism's library (prism.dll, libprism.so; release 0.18.3) placed next to the program, the menus
and the fight's gauges are spoken and shown in braille. a11y_es.txt is its Spanish; another language is a file
a11y_<code>.txt of the same kind, and lines of your own go into a11y_<code>_own.txt, which no update replaces.

Updates: once per start the game asks GitHub whether there is a newer release (F1 > Misc > "Check for updates"
turns that off). If there is, the main menu says so and F1 offers Update now: the new release is downloaded in the
background and its signature checked, and Restart now installs it over this folder and starts the new version (if anything goes
wrong, every file is put back). Your saves, settings (bt3_settings.txt), game data (gamedata/, mods in it), texture
packs, songs and stages are never touched. Updating by hand (the new files copied over the old ones) works as before.

This package contains no game data. The game's data comes from your disc and stays on your computer.

The port's own code is under the GNU AGPL, version 3 (licenses/tenkaichi3decomp-agpl.txt; code from before the
change of licence also under MIT, licenses/tenkaichi3decomp-mit.txt). Its source, this release's tag included:
https://github.com/z3xox/Tenkaichi3Decomp
"""

WIN_README = README.replace("(Linux, 64-bit)", "(Windows, 64-bit)").replace("Start  Tenkaichi3Decomp-setup\n", "Start  Tenkaichi3Decomp-setup.exe\n") \
    .replace("start  Tenkaichi3Decomp  itself", "start  Tenkaichi3Decomp.exe  itself")

MAC_README = README.replace("(Linux, 64-bit)", "(macOS, Apple Silicon) - EXPERIMENTAL") \
    .replace("1. Start  Tenkaichi3Decomp-setup\n", """0. macOS blocks programs downloaded from the internet that are not signed by Apple, and this one is not.
   Start the setup once with right-click > Open, then "Open" (or: System Settings > Privacy & Security > Open
   Anyway); it then lets the rest of this folder through itself. If something is still refused, once in Terminal
   (drag this folder onto the window after the command to fill in its place):
       xattr -dr com.apple.quarantine <this folder>
1. Start  Tenkaichi3Decomp-setup  (double-click; it opens a Terminal window beside the setup window)
""").replace("libprism.so", "libprism.dylib") \
    .replace("Updates: once per start the game asks GitHub", "Updates: the Mac version does not install updates itself yet: download the new release by hand.\nOn Linux and Windows, once per start the game asks GitHub")

TEXTURES_NOTE = """Texture packs go here.

Put a pack's replacement textures into this folder, in subfolders or not: the game looks through all of it when it
starts. The files have to be named the way the PCSX2 emulator names texture replacements for this game
(for example 1dd4c76113969303-56e3d4469d2ad392-00005e54.dds), so a pack made for PCSX2 can be copied in as it is.
Read: .dds files (DXT1, DXT3, DXT5 or plain 32-bit) and .png files.

In the game, F1 > Video has a switch for the pack.
"""

STAGES_NOTE = """Stages from outside the disc.

Put a stage's model file (.unk, the kind made for this game's mods) into this folder. It gets one more cell at the
end of the stage select, named after the file ("My_Map.unk" is shown as "My Map"). Up to 26 stages.

Optional: a file maps.txt here, one line per stage, "file.unk|Name In The Menu", gives other names and an order.

Added stages are not written to your save, and they are not offered in an online match (Tenkaichi Budokai Online).
"""

SONGS_NOTE = """Music from outside the disc.

Put a song as an .mp3, .wav, .flac or .adx file (ADX is the game's own music format) into this folder. It is added
to the music choice of the stage select, before "Random", named after the file. Up to 40 songs. An .mp3, .wav or
.flac is played as it is (miniaudio decodes it; there is no conversion pass and no .adx written next to it), so a
new song is in the list from the next start. An .adx is played by the game's own decoder. A file that cannot be
read stays in the list and is silent (the log says why). An .adx that an earlier version wrote next to an .mp3 is
not listed while the .mp3 is there (it can be deleted). Anything else (ogg, m4a...) has to be converted first
(ffmpeg can write the formats above, or ADX: ffmpeg -i song.ogg -ar 24000 -ac 2 -c:a adpcm_adx song.adx).

Optional: a file songs.txt here, one line per song, "file|Name In The Menu", gives other names and an order.

Added songs are not written to your save, and they are not offered in an online match (Tenkaichi Budokai Online).
"""


def take_program(exe, out):
    """The program without the game's data, and its list, into the release folder. A program built from the blank
    tables (port/data) is that already; one built with the tables' values has them taken out by strip_data.py."""
    import toolchain
    out.mkdir(parents=True)
    name = NAME + ".exe" if exe.suffix == ".exe" else NAME
    # where the game's own variables lie in the program (make_state.py, at link time): the program reads it from
    # beside itself to save and restore its state, which an online session is built on
    mem = pathlib.Path(str(exe) + ".mem")
    if not mem.exists():
        sys.exit(f"missing {mem.name}: link.py makes it")
    shutil.copy2(mem, out / (name + ".mem"))
    if (toolchain.DATA / "SKELETON").exists():
        dat = pathlib.Path(str(exe)[:-4] + ".dat" if exe.suffix == ".exe" else str(exe) + ".dat")
        if not dat.exists():
            sys.exit(f"missing {dat.name}: link.py makes it")
        shutil.copy2(exe, out / name)
        shutil.copy2(dat, out / (NAME + ".dat"))
        print("program built from the blank data tables (no disc involved)")
        return
    r = subprocess.run([sys.executable, str(ROOT / "port/tools/strip_data.py"), "--exe", str(exe), "--out", str(out)], capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode or "NOT matched" in r.stdout:
        sys.exit("strip_data.py failed or left game data in the program: not packaging\n" + r.stderr)
    (out / ("bt3.exe" if exe.suffix == ".exe" else "bt3")).rename(out / name)  # strip_data.py writes bt3 and bt3.dat
    (out / "bt3.dat").rename(out / (NAME + ".dat"))

def take_lang(out):
    """The screen reader's lines in other languages (port/lang/a11y_<code>.txt, read by port/src/gs/a11y.c from beside
    the program). They hold the reader's own labels only, none of the game's text."""
    for p in sorted((ROOT / "port/lang").glob("a11y_*.txt")):
        if not p.name.endswith("_own.txt"):
            shutil.copy2(p, out / p.name)

def take_translations(out):
    """Bundled label, UI, and port-text catalogs."""
    target = out / "translations"
    for kind in ("labels", "ui", "port"):
        (target / kind).mkdir(parents=True, exist_ok=True)
        for source in sorted((ROOT / "port/translations" / kind).glob("*.json")):
            if re.fullmatch(r"[a-z]{2,3}(?:-[A-Za-z]{2,3})?", source.stem):
                shutil.copy2(source, target / kind / source.name)

def take_fonts(out, lic):
    """The fonts the settings offer, and their licences next to them and in licenses/. Only what is in the
    repository: the list of menu labels (gamedata/game-text) is made from the disc, by the game at its first start
    on the player's machine (port/src/gs/menu_labels.c), and never packaged."""
    src = ROOT / "port/third_party/fonts"
    for p in sorted(src.rglob("*")):
        if p.is_file():
            (out / "fonts" / p.relative_to(src)).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, out / "fonts" / p.relative_to(src))
            if p.name == "LICENSE":
                shutil.copy2(p, lic / f"{p.parent.name}-fonts.txt")

def take_tbo_art(out, lic):
    """The pictures of the Tenkaichi Budokai Online screens, and who made them (port/tbo/ui/CREDITS.md)."""
    src = ROOT / "port/tbo/ui"
    (out / "tbo/ui").mkdir(parents=True, exist_ok=True)
    for p in sorted(src.glob("*.png")):
        shutil.copy2(p, out / "tbo/ui" / p.name)
    shutil.copy2(src / "CREDITS.md", lic / "tbo-ui-credits.txt")

def bundle_sdl_dependencies(dll, out, lic, prefix):
    """MSYS2 SDL can import libiconv; the official SDL archive is self-contained.
    Collect vendor DLLs from the SDL bin folder, never Windows system DLLs."""
    todo, seen = [dll], set()
    while todo:
        current = todo.pop()
        if current.name.lower() in seen:
            continue
        seen.add(current.name.lower())
        result = subprocess.run([prefix + "objdump", "-p", str(current)], capture_output=True, text=True, check=True)
        for name in re.findall(r"DLL Name:\s*(\S+)", result.stdout):
            source = dll.parent / name
            if source.exists() and name.lower() not in seen:
                shutil.copy2(source, out / name)
                todo.append(source)
            elif name.lower().startswith("lib") and not source.exists():
                sys.exit("missing SDL runtime dependency: " + name)
    if "libiconv-2.dll" in seen:
        notices = dll.parent.parent / "share/licenses/libiconv"
        shutil.copy2(notices / "COPYING.LIB", lic / "libiconv-lgpl.txt")
        shutil.copy2(notices / "README", lic / "libiconv-readme.txt")
        (lic / "libiconv-source.txt").write_text(
            "libiconv source: https://ftp.gnu.org/pub/gnu/libiconv/\n"
            "MSYS2 build recipe: https://github.com/msys2/MINGW-packages/tree/master/mingw-w64-libiconv\n")

def main_win():
    """BT3_CC=win64: bt3.exe (cross-built or built in MSYS2), Tenkaichi3Decomp-setup.exe (port/setup/build.sh win), SDL3.dll."""
    import toolchain
    out, zpath = ROOT / "port/build/release-win", ROOT / "port/build/Tenkaichi3Decomp-windows-x64.zip"
    exe, setup = ROOT / "port/build/bt3.exe", ROOT / "port/build/Tenkaichi3Decomp-setup.exe"
    dll = next((p for p in [toolchain.sdl3() / "bin/SDL3.dll"] if p.exists()), None) if toolchain.sdl3() else None
    if dll is None:
        dll = next((pathlib.Path(d) / "SDL3.dll" for d in os.environ.get("PATH", "").split(os.pathsep) if (pathlib.Path(d) / "SDL3.dll").exists()), None)
    for need in (exe, setup, pathlib.Path(str(exe) + ".map")):
        if not need.exists():
            sys.exit(f"missing {need.relative_to(ROOT)}: build first (BT3_CC=win64 undefined.py and link.py; port/setup/build.sh win)")
    if dll is None:
        sys.exit("SDL3.dll not found")
    if out.exists():
        shutil.rmtree(out)
    take_program(exe, out)
    take_lang(out)
    take_translations(out)
    shutil.copy2(setup, out / "Tenkaichi3Decomp-setup.exe")
    shutil.copy2(dll, out / "SDL3.dll")
    (out / "README.txt").write_text(WIN_README.replace("\n", "\r\n"))
    lic = out / "licenses"
    lic.mkdir()
    bundle_sdl_dependencies(dll, out, lic, toolchain.PREFIX)
    shutil.copy2(ROOT / "LICENSE", lic / "tenkaichi3decomp-agpl.txt")
    shutil.copy2(ROOT / "LICENSE-MIT", lic / "tenkaichi3decomp-mit.txt")
    shutil.copy2(ROOT / "port/third_party/imgui/LICENSE.txt", lic / "dear-imgui.txt")
    shutil.copy2(ROOT / "port/third_party/newlib_libm/COPYING.NEWLIB", lic / "newlib.txt")
    shutil.copy2(ROOT / "port/third_party/xxhash/LICENSE", lic / "xxhash.txt")
    shutil.copy2(ROOT / "port/third_party/miniaudio/LICENSE", lic / "miniaudio.txt")
    shutil.copy2(ROOT / "port/third_party/miniz/LICENSE", lic / "miniz.txt")
    shutil.copy2(ROOT / "port/third_party/monocypher/LICENCE.md", lic / "monocypher.txt")  # (the update's signature check)

    take_fonts(out, lic)
    take_tbo_art(out, lic)
    (out / "textures").mkdir()
    (out / "textures/README.txt").write_text(TEXTURES_NOTE.replace("\n", "\r\n"))
    for kind, note in (("stages", STAGES_NOTE), ("songs", SONGS_NOTE)):
        (out / kind).mkdir()
        (out / kind / "README.txt").write_text(note.replace("\n", "\r\n"))
    (lic / "sdl3.txt").write_text("SDL3 (SDL3.dll) is distributed under the zlib license: https://www.libsdl.org/license.php\n")
    for f in (out / "Tenkaichi3Decomp.exe", out / "Tenkaichi3Decomp-setup.exe"):
        subprocess.run([toolchain.PREFIX + "strip", "--strip-debug", str(f)], check=False)
    if zpath.exists():
        zpath.unlink()
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                z.write(p, "Tenkaichi3Decomp/" + str(p.relative_to(out)))
    print(f"{zpath.relative_to(ROOT)}: {zpath.stat().st_size / 1e6:.1f} MB")
    for p in sorted(out.rglob("*")):
        if p.is_file():
            print(f"  {p.stat().st_size:>10,}  {p.relative_to(out)}")

def main_mac():
    """BT3_CC=mac64, on a Mac: bt3_mac (port/tools/link.py), Tenkaichi3Decomp-setup (port/setup/build.sh), and Homebrew's
    SDL3 into lib/. Both programs are made to look for SDL3 only in lib/ (not in Homebrew's folder, whose SDL3 may be
    another version or not there), and signed again ad hoc (a changed Mach-O file has to be, on Apple Silicon)."""
    out, zpath = ROOT / "port/build/release-mac", ROOT / "port/build/Tenkaichi3Decomp-macos-arm64.zip"
    exe, setup = ROOT / "port/build/bt3_mac", ROOT / "Tenkaichi3Decomp-setup"
    sdl = pathlib.Path(os.environ.get("BT3_SDL3", "/opt/homebrew/opt/sdl3")) / "lib"
    dylib = sdl / "libSDL3.0.dylib"
    for need in (exe, setup, pathlib.Path(str(exe) + ".map"), dylib):
        if not need.exists():
            sys.exit(f"missing {need}: build first (BT3_CC=mac64 undefined.py and link.py; port/setup/build.sh; brew install sdl3)")
    if out.exists():
        shutil.rmtree(out)
    take_program(exe, out)
    take_lang(out)
    take_translations(out)
    shutil.copy2(setup, out / "Tenkaichi3Decomp-setup")
    (out / "lib").mkdir()
    shutil.copy2(dylib.resolve(), out / "lib/libSDL3.0.dylib")
    (out / "lib/libSDL3.0.dylib").chmod(0o755)

    def tool(*cmd):
        r = subprocess.run(list(cmd), capture_output=True, text=True)
        if r.returncode:
            sys.exit(" ".join(cmd) + ":\n" + r.stdout + r.stderr)
        return r.stdout

    tool("install_name_tool", "-id", "@rpath/libSDL3.0.dylib", str(out / "lib/libSDL3.0.dylib"))
    for f in (out / NAME, out / "Tenkaichi3Decomp-setup"):
        for line in tool("otool", "-L", str(f)).splitlines()[1:]:
            dep = line.strip().split(" (")[0]
            if "libSDL3" in dep and not dep.startswith("@rpath/"):
                tool("install_name_tool", "-change", dep, "@rpath/libSDL3.0.dylib", str(f))
        rpaths = re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)", tool("otool", "-l", str(f)))
        for r in rpaths:
            if r != "@executable_path/lib":
                tool("install_name_tool", "-delete_rpath", r, str(f))
        if "@executable_path/lib" not in rpaths:
            tool("install_name_tool", "-add_rpath", "@executable_path/lib", str(f))
    for f in (out / "lib/libSDL3.0.dylib", out / NAME, out / "Tenkaichi3Decomp-setup"):
        tool("codesign", "--force", "--sign", "-", str(f))
    # macOS on Apple Silicon kills a program whose page zero is missing before it starts (no message): 4 GB, as linked
    seg = re.search(r"segname __PAGEZERO\n\s+vmaddr \S+\n\s+vmsize (\S+)", tool("otool", "-l", str(out / NAME)))
    if seg is None or int(seg.group(1), 16) != 0x100000000:
        sys.exit(f"{NAME}: its __PAGEZERO is {seg.group(1) if seg else 'missing'}, not 4 GB: macOS would kill it at start")
    # nothing may still point into Homebrew: a player has no Homebrew
    left = [f"{f.name}: {line.strip()}" for f in (out / NAME, out / "Tenkaichi3Decomp-setup", out / "lib/libSDL3.0.dylib")
            for line in tool("otool", "-L", str(f)).splitlines()[1:] if "/opt/homebrew" in line or "/usr/local/" in line]
    if left:
        sys.exit("still linked against Homebrew:\n  " + "\n  ".join(left))

    (out / "README.txt").write_text(MAC_README)
    lic = out / "licenses"
    lic.mkdir()
    shutil.copy2(ROOT / "LICENSE", lic / "tenkaichi3decomp-agpl.txt")
    shutil.copy2(ROOT / "LICENSE-MIT", lic / "tenkaichi3decomp-mit.txt")
    shutil.copy2(ROOT / "port/third_party/imgui/LICENSE.txt", lic / "dear-imgui.txt")
    shutil.copy2(ROOT / "port/third_party/newlib_libm/COPYING.NEWLIB", lic / "newlib.txt")
    shutil.copy2(ROOT / "port/third_party/xxhash/LICENSE", lic / "xxhash.txt")
    shutil.copy2(ROOT / "port/third_party/miniaudio/LICENSE", lic / "miniaudio.txt")
    shutil.copy2(ROOT / "port/third_party/miniz/LICENSE", lic / "miniz.txt")
    shutil.copy2(ROOT / "port/third_party/monocypher/LICENCE.md", lic / "monocypher.txt")  # (the update's signature check)

    take_fonts(out, lic)
    take_tbo_art(out, lic)
    (out / "textures").mkdir()
    (out / "textures/README.txt").write_text(TEXTURES_NOTE)
    for kind, note in (("stages", STAGES_NOTE), ("songs", SONGS_NOTE)):
        (out / kind).mkdir()
        (out / kind / "README.txt").write_text(note)
    (lic / "sdl3.txt").write_text("SDL3 (lib/libSDL3.0.dylib) is distributed under the zlib license: https://www.libsdl.org/license.php\n")
    if zpath.exists():
        zpath.unlink()
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(out.rglob("*")):
            if p.is_file():
                info = zipfile.ZipInfo.from_file(p, "Tenkaichi3Decomp/" + str(p.relative_to(out)))  # (keeps the programs executable)
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, p.read_bytes(), compresslevel=9)
    print(f"{zpath.relative_to(ROOT)}: {zpath.stat().st_size / 1e6:.1f} MB")
    for p in sorted(out.rglob("*")):
        if p.is_file():
            print(f"  {p.stat().st_size:>10,}  {p.relative_to(out)}")
    for f in (NAME, "Tenkaichi3Decomp-setup", "lib/libSDL3.0.dylib"):  # the oldest macOS each one starts on
        minos = re.search(r"minos (\S+)", tool("otool", "-l", str(out / f)))
        print(f"  {f}: macOS {minos.group(1) if minos else '?'} or later")

def main():
    exe = ROOT / "port/build/bt3_64"
    setup = ROOT / "Tenkaichi3Decomp-setup"
    for need in (exe, setup, pathlib.Path(str(exe) + ".map")):
        if not need.exists():
            sys.exit(f"missing {need.relative_to(ROOT)}: build first (see the top of this file)")
    if OUT.exists():
        shutil.rmtree(OUT)
    take_program(exe, OUT)
    take_lang(OUT)
    take_translations(OUT)
    shutil.copy2(setup, OUT / "Tenkaichi3Decomp-setup")
    (OUT / "lib").mkdir()
    lib = next((p for d in ("/usr/local/lib", "/usr/lib", "/usr/lib64", "/usr/lib/x86_64-linux-gnu") for p in sorted(pathlib.Path(d).glob("libSDL3.so.0"))), None)
    if lib is None:
        sys.exit("libSDL3.so.0 not found")
    shutil.copy2(lib.resolve(), OUT / "lib/libSDL3.so.0")
    (OUT / "README.txt").write_text(README)
    lic = OUT / "licenses"
    lic.mkdir()
    shutil.copy2(ROOT / "LICENSE", lic / "tenkaichi3decomp-agpl.txt")
    shutil.copy2(ROOT / "LICENSE-MIT", lic / "tenkaichi3decomp-mit.txt")
    shutil.copy2(ROOT / "port/third_party/imgui/LICENSE.txt", lic / "dear-imgui.txt")
    shutil.copy2(ROOT / "port/third_party/newlib_libm/COPYING.NEWLIB", lic / "newlib.txt")
    shutil.copy2(ROOT / "port/third_party/xxhash/LICENSE", lic / "xxhash.txt")
    shutil.copy2(ROOT / "port/third_party/miniaudio/LICENSE", lic / "miniaudio.txt")
    shutil.copy2(ROOT / "port/third_party/miniz/LICENSE", lic / "miniz.txt")
    shutil.copy2(ROOT / "port/third_party/monocypher/LICENCE.md", lic / "monocypher.txt")  # (the update's signature check)

    take_fonts(OUT, lic)
    take_tbo_art(OUT, lic)
    (OUT / "textures").mkdir()
    (OUT / "textures/README.txt").write_text(TEXTURES_NOTE)
    for kind, note in (("stages", STAGES_NOTE), ("songs", SONGS_NOTE)):
        (OUT / kind).mkdir()
        (OUT / kind / "README.txt").write_text(note)
    (lic / "sdl3.txt").write_text("SDL3 (lib/libSDL3.so.0) is distributed under the zlib license: https://www.libsdl.org/license.php\n")
    for f in (OUT / "Tenkaichi3Decomp", OUT / "Tenkaichi3Decomp-setup"):
        subprocess.run(["strip", "--strip-debug", str(f)], check=False)  # the symbol names stay (crash reports use them)
    if ZIP.exists():
        ZIP.unlink()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(OUT.rglob("*")):
            if p.is_file():
                info = zipfile.ZipInfo.from_file(p, "Tenkaichi3Decomp/" + str(p.relative_to(OUT)))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, p.read_bytes(), compresslevel=9)
    print(f"{ZIP.relative_to(ROOT)}: {ZIP.stat().st_size / 1e6:.1f} MB")
    for p in sorted(OUT.rglob("*")):
        if p.is_file():
            print(f"  {p.stat().st_size:>10,}  {p.relative_to(OUT)}")
    portable(OUT)

def portable(out):
    """Says whether the release would run on other people's machines: no CPU level above the baseline demanded
    or used, and no newer C library than Ubuntu 22.04's (2.35). Build with port/release/build_linux.sh to pass."""
    bad = []
    for f in ("Tenkaichi3Decomp", "Tenkaichi3Decomp-setup", "lib/libSDL3.so.0"):
        note = subprocess.run(["readelf", "-n", str(out / f)], capture_output=True, text=True).stdout
        need = next((l.split(":", 1)[1].strip() for l in note.splitlines() if "ISA needed" in l), "")
        if any(v in need for v in ("x86-64-v2", "x86-64-v3", "x86-64-v4")):
            bad.append(f"{f}: stamped as needing {need}")
        dis = subprocess.run(["objdump", "-d", str(out / f)], capture_output=True, text=True).stdout
        if f != "lib/libSDL3.so.0" and ("%ymm" in dis or "%zmm" in dis):
            bad.append(f"{f}: contains AVX instructions")
        if "%zmm" in dis:
            bad.append(f"{f}: contains AVX-512 instructions")
        syms = subprocess.run(["objdump", "-T", str(out / f)], capture_output=True, text=True).stdout
        vers = sorted({tuple(int(x) for x in v.split(".")) for v in __import__("re").findall(r"GLIBC_(\d+\.\d+)", syms)})
        if vers and vers[-1] > (2, 35):
            bad.append(f"{f}: needs glibc {vers[-1][0]}.{vers[-1][1]}")
    print("portable: yes (baseline x86-64, glibc <= 2.35)" if not bad else "NOT PORTABLE:\n  " + "\n  ".join(bad))

NX_OUT = ROOT / "port/build/release-switch"
NX_ZIP = ROOT / "port/build/Tenkaichi3Decomp-switch.zip"
NX_SD_ZIP = ROOT / "port/build/Tenkaichi3Decomp-switch-SD.zip"

NX_README = """Dragon Ball Z: Budokai Tenkaichi 3 - port for the Nintendo Switch (Atmosphere homebrew).

This folder is the game's folder on the SD card:  atmosphere/contents/0100B73DEC000000/  (laid out as the PC
release's). The game's data comes from your own disc: copy the gamedata folder of your PC installation of
Tenkaichi3Decomp here (its setup made it from your disc). Start the game with Tenkaichi3Decomp from the homebrew menu
(sdmc:/switch/Tenkaichi3Decomp.nro), opened THROUGH A GAME (hold R while starting one). INSTALL.txt in the release's
SD card zip says all of it step by step.

sys-clk is REQUIRED for full speed: with it installed the game sets the clocks it needs by itself while it runs and
gives them back when it is not in front (F1 window > Video: "Overclock while playing").

Controllers: in fights by position, as a PS2 pad (B is Cross, A is Circle, Y is Square, X is Triangle; L/R and ZL/ZR
are L1/R1 and L2/R2); in the menus A chooses and B goes back (Controls: "Nintendo layout in the menus"). + and -
pressed together open the settings window (the PC's F1): the D-pad moves, L/R change the tab, the touch screen works
as a mouse; "Exit game" ends the game. 1P vs 2P opens the console's Controllers screen: a Joy-Con each, held
sideways, or two controllers.
Saves are kept in saves/, as on the PC: they can be copied between the two. A file placed in gamedata/mods/<same
path as the original> replaces the original. Texture packs (PCSX2 naming) go into textures/, stages and music from
outside the disc into stages/ and songs/: see the note in each folder.
Updates: the game says when a new release is out (F1 window > Update now, then Restart now); your saves, settings,
game data and mods are not touched. launcher.nro is the launcher's copy that it takes from here by itself.

This package contains no game data.

The port's own code is under the GNU AGPL, version 3 (licenses/tenkaichi3decomp-agpl.txt; code from before the
change of licence also under MIT, licenses/tenkaichi3decomp-mit.txt). The libraries it is built with, and their
licences: licenses/. Its source, this release's tag included: https://github.com/excinus/Tenkaichi3Decomp-NX (the
Switch's), from https://github.com/z3xox/Tenkaichi3Decomp (the port's).
"""

NX_INSTALL = """Tenkaichi3Decomp for the Nintendo Switch - installing

What you need:
  - a Switch with Atmosphere (custom firmware) and the homebrew menu;
  - sys-clk (the homebrew clock manager): REQUIRED. The game sets the clocks it needs by itself while it runs
    (CPU 1785 MHz, memory 1600 MHz, GPU 921 MHz docked or charging, 614 MHz on the battery) and gives them back when
    it is not in front. Without it fights run at 16 to 20 pictures a second, unevenly;
  - your PC installation of Tenkaichi3Decomp, made by its setup from YOUR OWN disc: its gamedata folder. This
    package contains no game data;
  - Python 3 on the PC (the PC setup's is fine), for step 3.

1. Copy the folders of this zip to the root of the SD card: atmosphere/ and switch/ (they merge with what is there).
2. Copy the gamedata folder of your PC installation into  atmosphere/contents/0100B73DEC000000/  on the card,
   next to the file Tenkaichi3Decomp (so that atmosphere/contents/0100B73DEC000000/gamedata/disc/SLUS_216.78 exists).
   Its files are large: the card needs exFAT if any file is over 4 GB.
3. Once, with the card in the PC: pack the folder of 65,000 files (gamedata/pzs3us2) into one file. The Switch takes
   about 0.4 s to open a file in that folder: without the pack the menus and fights stop at every spoken line. In a
   command prompt in this zip's folder (F: is the card here):
       python pack_gamedata.py F:\\atmosphere\\contents\\0100B73DEC000000\\gamedata --delete
   It writes gamedata/pzs3us2.afs (the same data in one file, checked), then removes the folder. A few minutes.
   (Do not run it with --delete on the PC's own gamedata: the PC needs the folder.) It can also write the pack from
   the PC's gamedata straight onto the card, which then needs no copy of that folder:
       python pack_gamedata.py C:\\path\\to\\Tenkaichi3Decomp\\gamedata --out F:\\atmosphere\\contents\\0100B73DEC000000\\gamedata
4. On the Switch, open the homebrew menu THROUGH A GAME (hold R while starting any game; from the album there is not
   enough memory), then start Tenkaichi3Decomp.

Updates come by themselves: the game says when a new release is out (+ and - together > Update now, then Restart
now, then start the game again). Your saves and settings are not touched. To update by hand instead, copy a newer
release's zip over the card in the same way (steps 2 and 3 are not needed again).

If something goes wrong, the game's folder has bt3_log.txt (and bt3_crash.txt after a crash): send those with the
report. Saves are in saves/ in the game's folder, as on the PC: copy them between PC and Switch.
"""

NX_LIBRARIES = """The Switch program (Tenkaichi3Decomp) is linked with these libraries besides the port's own (see the
other files here for those):

- SDL 3.4.12, zlib licence: https://www.libsdl.org/license.php, source https://github.com/libsdl-org/SDL
  It is built with the Switch platform files of this port (port/switch/sdl3 in the port's source), which are the
  port's own code under its licence and not part of SDL.
- libnx (switchbrew), ISC licence: https://github.com/switchbrew/libnx/blob/master/LICENSE.md
- Mesa (devkitPro's build for the Switch: EGL, OpenGL, the nouveau driver), MIT licence:
  https://docs.mesa3d.org/license.html, source https://github.com/devkitPro/mesa
- devkitA64's newlib (C library) and the GCC runtime libraries (GPL with the runtime library exception):
  https://devkitpro.org
"""

def main_nx():
    """BT3_CC=nx64: the game's folder for the Switch (docs/port/switch_plan.md), laid out as the PC release folder: the
    program (an ELF the launcher loads) with its lists (.dat, .mem, .rel), the screen reader's lines, translations,
    fonts, the online screens' pictures, the notes of textures/, songs/, stages/, README.txt, licenses/. It goes to
    atmosphere/contents/0100B73DEC000000/ on the SD card; the zip is what the automatic update takes. With the
    launcher built (port/switch/launcher/build.sh, before this), its copy launcher.nro is in it (the launcher takes it
    from there), and a second zip has the SD card's whole layout for a first install (Tenkaichi3Decomp-switch-SD.zip:
    atmosphere/, switch/Tenkaichi3Decomp.nro, pack_gamedata.py, INSTALL.txt). BT3_NX_LAUNCHER=1: an error without it
    (a release)."""
    import toolchain
    exe = toolchain.EXE
    for need in (exe, pathlib.Path(str(exe) + ".rel")):
        if not need.exists():
            sys.exit(f"missing {need.relative_to(ROOT)}: build first (BT3_CC=nx64 undefined.py and link.py)")
    if NX_OUT.exists():
        shutil.rmtree(NX_OUT)
    take_program(exe, NX_OUT)
    shutil.copy2(str(exe) + ".rel", NX_OUT / (NAME + ".rel"))
    # the symbols too (crash reports give addresses: the build's bt3_nx.map has the names)
    subprocess.run([toolchain.dkp("strip"), "--strip-all", str(NX_OUT / NAME)], check=True)
    take_lang(NX_OUT)
    take_translations(NX_OUT)
    (NX_OUT / "README.txt").write_text(NX_README)
    launcher = ROOT / "port/build/switch/Tenkaichi3Decomp.nro"
    if launcher.exists():
        shutil.copy2(launcher, NX_OUT / "launcher.nro")
    elif os.environ.get("BT3_NX_LAUNCHER") == "1":
        sys.exit("missing port/build/switch/Tenkaichi3Decomp.nro: build the launcher first (port/switch/launcher/build.sh)")
    lic = NX_OUT / "licenses"
    lic.mkdir()
    shutil.copy2(ROOT / "LICENSE", lic / "tenkaichi3decomp-agpl.txt")
    shutil.copy2(ROOT / "LICENSE-MIT", lic / "tenkaichi3decomp-mit.txt")
    shutil.copy2(ROOT / "port/third_party/imgui/LICENSE.txt", lic / "dear-imgui.txt")
    shutil.copy2(ROOT / "port/third_party/newlib_libm/COPYING.NEWLIB", lic / "newlib.txt")
    shutil.copy2(ROOT / "port/third_party/xxhash/LICENSE", lic / "xxhash.txt")
    shutil.copy2(ROOT / "port/third_party/miniaudio/LICENSE", lic / "miniaudio.txt")
    shutil.copy2(ROOT / "port/third_party/miniz/LICENSE", lic / "miniz.txt")
    shutil.copy2(ROOT / "port/third_party/monocypher/LICENCE.md", lic / "monocypher.txt")
    (lic / "switch-libraries.txt").write_text(NX_LIBRARIES)
    take_fonts(NX_OUT, lic)
    take_tbo_art(NX_OUT, lic)
    (NX_OUT / "textures").mkdir()
    (NX_OUT / "textures/README.txt").write_text(TEXTURES_NOTE)
    for kind, note in (("stages", STAGES_NOTE), ("songs", SONGS_NOTE)):
        (NX_OUT / kind).mkdir()
        (NX_OUT / kind / "README.txt").write_text(note)
    if NX_ZIP.exists():
        NX_ZIP.unlink()
    with zipfile.ZipFile(NX_ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(NX_OUT.rglob("*")):
            if p.is_file():
                info = zipfile.ZipInfo.from_file(p, "Tenkaichi3Decomp/" + str(p.relative_to(NX_OUT)))
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, p.read_bytes(), compresslevel=9)
    print(f"{NX_ZIP.relative_to(ROOT)}: {NX_ZIP.stat().st_size / 1e6:.1f} MB")
    if NX_SD_ZIP.exists():
        NX_SD_ZIP.unlink()
    if launcher.exists():
        with zipfile.ZipFile(NX_SD_ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in sorted(NX_OUT.rglob("*")):
                if p.is_file():
                    z.writestr(zipfile.ZipInfo.from_file(p, "atmosphere/contents/0100B73DEC000000/" + str(p.relative_to(NX_OUT))),
                               p.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            z.writestr(zipfile.ZipInfo.from_file(launcher, "switch/Tenkaichi3Decomp.nro"), launcher.read_bytes(),
                       compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
            pack = ROOT / "port/switch/pack_gamedata.py"
            z.writestr(zipfile.ZipInfo.from_file(pack, "pack_gamedata.py"), pack.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
            z.writestr("INSTALL.txt", NX_INSTALL.replace("\n", "\r\n"), compress_type=zipfile.ZIP_DEFLATED)
        print(f"{NX_SD_ZIP.relative_to(ROOT)}: {NX_SD_ZIP.stat().st_size / 1e6:.1f} MB (the SD card's layout, for a first install)")

if os.environ.get("BT3_CC") == "win64":
    main_win()
elif os.environ.get("BT3_CC") == "nx64":
    main_nx()
elif os.environ.get("BT3_CC") == "mac64":
    main_mac()
else:
    main()
