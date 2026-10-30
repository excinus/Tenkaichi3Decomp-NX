# Nintendo Switch (Atmosphère) port: plan

The Switch port is IN PROGRESS: the game builds for the Switch, but nothing of it runs there yet. This file is the design, the plan and the
record of what was checked; update it as phases land, as docs/port/macos_plan.md is for the Mac.

Status 2026-10-29: phase 4 under way: the Switch's drivers for SDL3 (video with OpenGL through Mesa, sound,
controllers) are written and the game links with them. On a console it now runs to the main menu, with picture,
sound and controllers (after a black screen found to be SDL asking libnx's sockets service for the number of
processors, see "Results") and into fights: with the CPU at 1785 MHz (sys-clk) a fight at a clean 30 pictures a
second, the HUD, effects and pause menu shown, the menus without stops (the data folder of 65,000 files read from
one pack, `pack_gamedata.py`), the intro movie at 30 pictures a second. Open: speed at the stock clock (the render
thread about 43 ms per drawn fight frame there, OpenGL's draws 23 of it).
Phase 3 done: the game's demo fight ran to its end on a Switch, its fight state identical to the PC's on all 4242
blanks of fighting (see "Results").
- Phase 1: `port/switch/spike`, a homebrew program that checks on a console what the port depends on (memory,
  speed, OpenGL), passed everything in three runs on a console (see "Results"): the 4 GB window maps at the fixed
  place every time, code runs at W + 0x20000000, every OpenGL program of the renderer compiles, and the game's
  software float is 3.4 to 4.2 times slower than on the build machine's Xeon at the stock 1020 MHz (about 2.2 times
  at 1785 MHz).
- Phase 2: `BT3_CC=nx64` compiles the whole game and links it with libnx into `port/build/bt3_nx`, an ELF at the
  window + 0x20000000 (see "The build"), in the `switch` workflow's `game` job. The game's own stubs are the same
  as Linux's.
- Phase 3 so far: the launcher (`port/switch/launcher`, an .nro for now; "The launcher"); SDL3 itself built for the
  Switch (`port/switch/sdl3`, "The platform layer"), so every SDL call of the port is SDL's own code, with dummy video
  and sound until phase 4; the game's start on the Switch (its log in bt3_log.txt, test settings from bt3_env.txt);
  `port/switch/test_kit.sh`. Left as stubs: `eglGetProcAddress` (Mesa, phase 4) and `execv`, `execvp`, `waitpid`
  (the Switch has no processes), none of them on the demo fight's way.

## Ground rule

As for macOS: players on Linux and Windows see no change. Everything for the Switch sits behind `__SWITCH__` or the
`nx64` build variant, and the Switch build must give the same fight state as Linux (`BT3_HASH`,
`port/tools/compare_hash.py`), so that replays and online matches against Linux and Windows work.

## What the player gets

The idea: the Switch version works like the PC one, automatic update included, and the folder on the SD card looks
like the PC release folder, so that nothing has to be done differently when a new release comes out. An `.nsp`
puts the game on the HOME menu once and is never updated; every update changes only the folder.

1. **The Switch package** (a release asset, beside the Linux and Windows ones): `Tenkaichi3Decomp-setup.exe` for the
   PC (Linux: `Tenkaichi3Decomp-setup`), and the Switch programs it puts on the card.
2. **The setup**, on the PC: choose the disc image (the same unmodified USA release as on PC) and the SD card (or any
   folder, copied to the card later). It checks and unpacks the disc as the PC setup does and writes:

   ```
   <SD>/atmosphere/contents/0100B73DEC000000/      the game's folder: the PC release folder, for the Switch
       Tenkaichi3Decomp          the game, an ELF                 (PC: Tenkaichi3Decomp / .exe)
       Tenkaichi3Decomp.dat      its data-table list              (PC: the same)
       gamedata/                 unpacked from your disc; mods in gamedata/mods/
       saves/                    memory card saves (the PC's own format: copy them between PC and Switch)
       bt3_settings.txt, online_name.txt, ...
       fonts/ translations/ tbo/ textures/ songs/ stages/ licenses/ README.txt
   <SD>/Tenkaichi3Decomp.nsp     (made once, see "The NSP"; install it and delete it)
   ```

   No self-test (the program is for the Switch, not the PC). Adding the European languages later works as on PC.
3. **Install the `.nsp` once** with a title installer (DB Installer, Goldleaf, Tinfoil, ...). The HOME menu has
   *Tenkaichi3Decomp* with its icon from then on.
4. **Play**: the HOME menu's icon starts the launcher in the NSP, which loads `Tenkaichi3Decomp` from the
   folder (below). F1 is + and - together (the PC's START + SELECT), as on a PC controller.
5. **Updates**: the game asks GitHub for the newest release as on PC; the asset is `Tenkaichi3Decomp-switch.zip`
   (with its `.sig`), whose contents are the folder above without the player's files. The same rules as on PC: the
   signature is checked, the player's files (`saves/`, `gamedata/`, `mods/`, settings, packs, songs, stages, the
   player's font lines) are never written, and a failed install puts everything back. Restart now returns to the
   launcher, which loads the new program; the console is not restarted and the NSP is not touched.

Why the folder is under `atmosphere/contents/<title ID>`: it is the title's own place on the card, which Atmosphère
looks at when the title starts. Atmosphère reads only `exefs/`, `exefs.nsp`, `romfs/`, `cheats/` and `flags/`
there (to verify on the console: that nothing else is read or refused); the PC layout has none of these names. It
also gives an escape hatch that needs no new NSP: a launcher placed at `exefs/main` + `exefs/main.npdm` in that
folder replaces the one in the NSP (LayeredFS; unless L is held at start, Atmosphère's default).

The title ID `0100B73DEC000000` ("BT3 DEC") is in the application range (ends in `000`). It has not yet been
checked against a list of published titles: do that before the first Switch release, because from then on it is
fixed (the folder, the NSP and the updater all depend on it).

## How it runs

### The launcher (the NSP's program)

Horizon loads a title's program (`main`, an NSO) where it chooses, with ASLR. The game cannot run from there: its
pointers are 4 bytes wide, and everything it points at, its own globals included, must lie in one 4 GB window at
the PS2's addresses (window + address), with the program at window + 0x20000000. That is the memory model of the
macOS arm64 build (docs/port/macos_plan.md: the `macwin` LLVM pass, `Port_Window`), which the Switch build shares;
Horizon also has no `mmap` at a chosen address.

The program is linked for the window W0 = 0x4000000000 (256 GB; `NX_WINDOW` in port/tools/toolchain.py) at
W0 + 0x20000000, and can be moved with its window as a whole into any other free 4 GB window (see "The build":
nothing in its code depends on where it is, and `<program>.rel` lists the 8-byte addresses in its data, which move
with it). The window it runs in is `Port_Window`, the program's first byte less 0x20000000 (`PORT_WINDOW` in
port/src/port_addr.h). So the NSP holds a small launcher in the manner of nx-hbloader, which rarely changes:

1. finds the game's folder: next to the launcher if `Tenkaichi3Decomp` is there, else
   `sdmc:/atmosphere/contents/0100B73DEC000000/` (a message with what to do when it is missing);
2. chooses a free window W .. W + 4 GB (svcQueryMemory, clear of the heap, alias and stack regions, which ASLR
   places anew at each start): W0 when it is free, else the first free 4 GB-aligned one, and reserves it. (W0 was
   free in the spike's three starts but not in the first test of the launcher, 2026-10-29: the system's regions
   take about a sixth of the address space, and land on a fixed window now and then. Hence the moving.) When no
   window is free it shows the address space's layout;
3. loads the program `Tenkaichi3Decomp` (an ELF: its three segments) at W + 0x20000000, with
   svcMapProcessCodeMemory and svcSetProcessMemoryPermission (code read-execute, read-only data, data read-write),
   from the process's heap: its first 16 MB are the launcher's own C heap, then the segments, and the rest is the
   game's heap. When W is not W0, each address `Tenkaichi3Decomp.rel` lists is moved by W - W0 in the copy before it
   is mapped;
4. closes its own system services (an application holds its applet proxy only once; the game's libnx opens its
   own), keeps its thread data aside, and starts the program with the homebrew ABI as nx-hbloader does (the heap,
   the process handle, the allowed calls, the applet type, `argv[0]` = the program's full path, which makes the
   game's folder its working folder). When the program ends it comes back (`port/switch/launcher/source/
   trampoline.s` keeps the registers a C caller keeps, which the program's exit does not), takes its services
   again, and shows the result and the end of `bt3_log.txt`. While the program runs, an exception reaches the
   launcher's start first and is passed to the program's (its crash report, plat_crash.c), as nx-hbloader does.

As built (phase 3) it is an .nro for the homebrew menu (`port/switch/launcher/build.sh` ->
`port/build/switch/Tenkaichi3Decomp.nro`). When the game ends, its segments stay mapped and the launcher only shows
the result and exits: the game's threads are not ended by its exit, and one could still run in its code. Restart now
after an update (phase 5) is therefore planned as `appletRestartProgram` (the whole process anew) for the NSP, not
as a second load in the same process.

The program then maps the game's regions itself, at W + their PS2 addresses, as it does on every system
(port/src/plat_mem.c, its Switch branch, the same calls): the heap 0x003BE000-0x02000000, its stack at 0x02000000,
the register blocks at 0x10000000 and 0x12000000, the low arena at 0x13000000 and the scratchpad at 0x70000000.
Horizon has no memory taken page by page as used, so the low arena (192 MB of address space on PC) is backed in
full; the console gives an application about 3.2 GB.

The NPDM gives the launcher what that needs: the application's memory, a 39-bit address space, and the calls
svcMapProcessCodeMemory (0x77), svcUnmapProcessCodeMemory (0x78), svcSetProcessMemoryPermission (0x73) and
svcQueryMemory.

### The build (variant `nx64`), as built

```
python3 port/switch/sdl3/build.py                            # once: SDL3 for the Switch ("The platform layer")
BT3_CC=nx64 BT3_SKELETON=1 python3 port/tools/undefined.py   # compile
BT3_CC=nx64 BT3_SKELETON=1 python3 port/tools/link.py        # -> port/build/bt3_nx, .dat, .mem, .map
BT3_CC=nx64 python3 port/tools/package.py                    # -> port/build/release-switch, Tenkaichi3Decomp-switch.zip
port/switch/launcher/build.sh                                # -> port/build/switch/Tenkaichi3Decomp.nro
port/switch/test_kit.sh [demo]                               # -> port/build/switch/test-kit
```

Needs devkitPro's devkitA64 with libnx and the Switch portlibs (`DEVKITPRO`, default /opt/devkitpro; the
`devkitpro/devkita64` container has them), LLVM 22 with its development files (`BT3_LLVM` names its bin/ folder;
default /usr/lib/llvm-22/bin, apt.llvm.org's) and clang's Python bindings for `ptr32.py`. The `switch` workflow's
`game` job does exactly this in the container. It is `mac64` (docs/port/macos_plan.md, "Native arm64: how the build
works") with ELF instead of Mach-O:

- **Game code** (and the port's software-float files): preprocess, `eeconst.py`, `ptr32.py` as everywhere, then
  clang 22 `--target=aarch64-none-elf` (newlib's headers from devkitA64) to LLVM IR, the `macwin` window pass in its
  two steps, `default<O2>`, and `llc -mtriple=aarch64-none-elf -mcpu=cortex-a57 -mattr=-fp-armv8,-neon
  -relocation-model=static`. LLVM 22 keeps the game's pointers 4 bytes wide on this target too (its datalayout has
  `p270:32:32-p271:32:32`). The pass takes `-bt3-window-base=0x4000000000`: in data, a 4-byte pointer is the
  window offset `g - 0x4000000000` (ELF cannot hold the difference of two symbols of different sections, which
  Mach-O's subtractor relocations can), the same in any window; everywhere else, code included, W is libnx's
  `__start__` (the program's first byte) less 0x20000000, reached PC-relatively, as `_mh_execute_header` is on
  macOS. Code that makes host pointers loads `Port_Window`, as on macOS.
- **The renderer, the settings window, ImGui** (hardware float): devkitA64's GCC 15 with libnx's flags (`-mtp=soft`),
  SDL3 for the Switch (linked as `libSDL3.a`). ImGui's OpenGL loader has a Switch branch (`eglGetProcAddress`; there are no shared libraries).
  `gs_gpu.c` gets placeholder SPIR-V: the Switch draws only through OpenGL.
- **Data tables** (`port/data/*.s`): `macho.elf_asm`, a pointer `.long g` becomes `.long g - 0x4000000000` (GNU ld
  resolves it to the window offset). The game's `_ctype_` (the PS2 C library's character table, read by the
  password screen) is named `bt3_ps2_ctype_` in the tables and in the game's objects (`llvm-objcopy`), because the
  Switch's C library has its own (`macho.NX_RENAME`).
- **Link** (`link_nx`): devkitA64's GCC with `port/switch/nx.specs` (libnx's `switch.specs` without `-pie`),
  `--defsym=__start__=0x4020000000` (where libnx's linker script starts the program), `--wrap=main` (plat_mem.c runs
  the game's main() on its stack in the window), libnx. libnx programs are position-independent at 0 and its
  start-up applies their relocations; this one has none, and `port/switch/nx_dynamic.s` gives it the empty dynamic
  section the start-up looks for. Checked in the result: one load address, `0x4020000000`; no relocations; a table's
  pointer holds the window offset (`gBattleReplay + 12296` in `D_002C3398`); game code reads W from `Port_Window`.
- **After the link**: `make_state.py` and `make_dat.py` read GNU ld's map as on Linux and write game addresses
  (window offsets), as on macOS. `nx_reloc.py` writes `bt3_nx.rel` from the relocations the link keeps
  (`--emit-relocs`): the 8-byte slots holding an address in the window (R_AARCH64_ABS64 targets, and the global
  offset table's entries, which the linker fills without one), 3703 of them; it stops the build if an instruction
  holds an absolute address of the window (R_AARCH64_MOVW_UABS_*). Checked: the whole game built a second time for
  another window (`BT3_NX_WINDOW=0x5300000000`) is, byte for byte apart from its build id, the first build with its
  .rel's addresses moved by the difference.
- **One compiler helper of its own**: without its float unit, AArch64 code converts an unsigned 32-bit integer to
  float through `__floatundisf` (x86 calls `__floatunsisf`); `softfloat_ps2.c` defines it for the Switch, exact as
  `__floatunsisf` for every 32-bit value. Every other float helper the objects call is the port's own (checked in
  the map).

The Linux build was compiled before and after these changes in the same place: its game objects are the same
instruction for instruction.

### The platform layer

The port uses SDL3 throughout, and SDL3 has no public Switch port (the official one is for Nintendo developers
only, under their agreement: https://wiki.libsdl.org/SDL3/README-switch). What is used: about 150 functions outside
the GPU back end (counted 2026-10-29: windows and the GL context, gamepads and a virtual joystick, audio streams,
threads, mutexes, conditions, atomics, timers, files and folders, properties, processes for the PC's helpers), and
SDL_GPU only in `gs_gpu.c`, which the Switch does not use.

As built (phase 3): **SDL3 itself, built for the Switch** (`port/switch/sdl3/build.py`: SDL 3.4.12's own source,
the version of the PC release, with devkitPro's CMake toolchain, as a static library). So everything the port uses
that is not tied to the hardware is SDL's own code, the same as on PC: audio streams and their resampling,
surfaces and PNG loading, properties, file streams, events, hints, the gamepad layer and its virtual joystick.
The Switch is a platform block like SDL's 3DS one (`port/switch/sdl3/switch.cmake`, put into SDL's tree and named
in its CMakeLists.txt at build time): SDL's POSIX back ends for threads, timers, time and file operations (libnx
gives newlib those calls), a file-system back end of the port's (`src/filesystem/switch`: the base path is the
program's folder, from `argv[0]`), and no-op POSIX scheduling calls and `pthread_sigmask` that newlib lacks here
(`src/core/switch`; threads keep their default priority). SDL's dynamic API is switched off (no shared libraries).
The Switch's drivers (phase 4, `port/switch/sdl3/src/{video,audio,joystick}/switch`; `build.py` adds each to SDL's
list of drivers, before the 3DS's):
- **video** (`SDL_switchvideo.c`): one window, the screen's size (1920x1080 docked, 1280x720 in the hand; it
  follows when the console is docked or taken out), on libnx's default window; OpenGL through SDL's own EGL code
  and devkitPro's Mesa, EGL called directly (`SDL_VIDEO_STATIC_ANGLE`, no shared libraries) with Khronos' types as
  plain pointers (`EGL_NO_PLATFORM_SPECIFIC_TYPES`). Every `SDL_PumpEvents` runs libnx's applet loop: the system's
  request to close is SDL's quit, focus changes are window events. The touch screen is the mouse (left button), for
  the settings window. Text input opens the software keyboard; its text arrives as typed text and a Return.
- **audio** (`SDL_switchaudio.c`): audout, 48000 Hz, two channels, 16-bit, three buffers in turn; the mixing
  thread on core 2.
- **joystick** (`SDL_sysjoystick.c`): four players through libnx's pad API (player 1 also the console in the hand),
  each an SDL gamepad by position (B south, A east, Y west, X north; + start, - back; ZL, ZR the triggers); rumble.
The game links Mesa (`-lEGL -lglapi -ldrm_nouveau`) and draws through OpenGL by default on the Switch (gs_draw.c).
(The first plan, the SDL3 calls written over devkitPro's SDL2, is dropped: the two libraries' functions have the
same names.) Mesa's SPIR-V path (`glSpecializeShader`) needs functions devkitPro's Mesa leaves out: they are
link stubs, which the renderer never reaches.

These files are the port's, not SDL's, and are not meant to be offered to SDL: SDL does not accept code written with
AI tools (its CLAUDE.md), and they were. Kept in this repository and applied at build time only.

- **Renderer**: the OpenGL 3.3 back end (`gs_gl.c`) on devkitPro's Mesa (nouveau). The settings window (ImGui) has
  its OpenGL back end already. 1280x720 handheld, 1920x1080 docked; internal resolution 1x or 2x.
- **Input**: the controllers as PS2 pads (Joy-Con pair, Pro Controller, handheld), two players. F1 = + and - together.
- **Sound**: the port mixes itself (miniaudio's mixer) and needs one output stream.
- **Files**: every path is relative to the game's folder, as on PC.
- **Online**: libnx's BSD sockets (UDP for matches, the TURN relay as on PC), HTTPS to the matchmaking service and
  GitHub through devkitPro's curl. The same `NET_VERSION` as the PC release: Switch and PC play each other.
- **Not on the Switch**: the screen reader (Prism has no Switch library), the PC helper programs (the MP3 import
  of songs converts on the PC; `.adx` songs work), opening web pages (the release page is shown as its address).

### The NSP

**Dropped (2026-10-30, the player's call):** the game stays an `.nro`. A forwarder (for example the one included
in sphaira, github.com/NaGaa95/sphaira) puts any `.nro` on the HOME menu and starts it from there, as players do for
the other homebrew; an NSP of the project's own would need its own installing and updating (DBI), for nothing a
forwarder does not already give. What follows was the plan, kept for the record.

An NSP is encrypted with the console's keys, which the project cannot ship. The setup makes it on the player's PC
from the player's own `prod.keys` (dumped from their console with Lockpick_RCM), as the usual forwarder makers do:
a program NCA (the launcher's NSO and NPDM) and a control NCA (name, icon, NACP: the application's memory, no save
data of the console's own), as an NSP. Installing a homebrew NSP needs the installer's signature-check bypass or
signature patches, as for every homebrew NSP; the setup's page says so. Without an NSP, the launcher also runs as
an `.nro` from the homebrew menu through a game (hold R): the same folder, no HOME icon.

## Phases

| Phase | Done when |
|---|---|
| 0. Design, spike | This file; `port/switch/spike` builds in CI (`switch` workflow, artifact `bt3-switch-spike`). **Done.** |
| 1. Spike on a console | Its report (`sdmc:/switch/bt3_spike.txt`) is in this file: the window maps, code runs at W + 0x20000000, the speed is known, every GL program compiles. **Done** (2026-10-29): three runs, two at the stock clock, all passed. |
| 2. The game builds for the Switch | `BT3_CC=nx64` compiles and links the whole game with libnx (`BT3_SKELETON=1`), in CI. **Done** (2026-10-29): `port/build/bt3_nx`; stubs for SDL3 and four system calls. |
| 3. The demo fight on a console | The launcher loads the game; the attract-demo fight runs without a picture and its `BT3_HASH` lines (written to the card) equal Linux's (`compare_hash.py`). **Done** (2026-10-29): "battle finished", the fight's values the same as the PC's on all 4242 blanks of fighting. |
| 4. Playable | The platform layer: picture (GL), sound, controllers; the game plays from boot to a fight and saves. **Done** (2026-10-30), with the CPU overclocked to 1785 MHz (sys-clk), which is a requirement: there everything checked works (menus, fights at a clean 30, HUD and effects, movies, saving, added songs and stages); at the stock 1020 MHz fights reach 16 to 20 pictures a second, unevenly paced (see "Results"). |
| 5. The package | This fork's own releases (not the original repository's, and no Switch mode in its setup): `Tenkaichi3Decomp-switch.zip` in this fork's release workflow, signed with this fork's key; the updater's Switch asset from this fork's releases and its restart through the launcher; a workflow that follows the original repository's releases (merges them, builds, publishes the same version for the Switch). The `.nro` is the program to start (from the homebrew menu, or the HOME menu through a forwarder such as sphaira's); no NSP of its own ("The NSP"). |
| 6. Online | A lobby between a Switch and a PC, by code and by address. |

Biggest risk: **speed**. The game's arithmetic is exact software float (the README asks for a recent mid-range PC
processor); the Switch has four Cortex-A57 cores at 1020 MHz (1785 MHz is common with overclocking). The spike times
the same routines on the console; on this project's build machine (Xeon at 2.1 GHz, 2026-10-29) they take 5.7-6.9 ns
per operation and the memory walk 2.8 ns per step; on the console at 1785 MHz 13.5 ns and 3.9 ns (see "Results"),
so about 2.2 times the PC's time for the game's arithmetic; at the stock 1020 MHz 23.7 ns and 6.9 ns, 3.4 to 4.2
times. Whether that is full speed is known only when the game's own demo fight runs (phase 3): its time per
field on Linux, multiplied by that, against the 16.7 ms of a field. If the console is too slow for full speed even with the renderer
on another core, the options are the hot paths in faster exact code (the vector library in integer NEON),
or a mode that skips drawing frames.

## The game on a console (phase 4): how to run it

The `switch` workflow's artifact `switch-test-kit` (or `port/switch/test_kit.sh` after a build and `package.py`):
the game's folder as `package.py` makes it (the PC release's layout: the program and its lists, fonts,
translations, the online screens' pictures, licences, the notes of textures/, songs/, stages/), a `bt3_env.txt` with
comments only, and the launcher. Its README.txt has the steps. The launcher ends at once after a game left with
Exit game; after a crash it shows the report's first lines at its next start; a test run (`bt3_env.txt` with
`BT3_DEMO` or `BT3_GS=none`) ends on its result screen.

## The demo fight on a console (phase 3): how to run it

`port/switch/test_kit.sh demo` after a build: the game's program,
its lists (`.dat`, `.mem`, `.rel`), a `bt3_env.txt` with the PC self-test's settings and `BT3_HASH=bt3_hash.txt`, and the
launcher. Its README.txt has the steps: the kit's folders to the SD card, the PC installation's `gamedata` into the
title's folder, the launcher from the homebrew menu through a game, and the same fight on the PC for comparison
(`compare_hash.py bt3_hash_pc.txt bt3_hash.txt --fight`).

## The spike (phase 1): how to run it

1. Download `bt3-switch-spike` from the newest run of the `switch` workflow (Actions tab), or build it:
   `port/switch/spike/build.sh` (docker) -> `port/build/switch/spike.nro`.
2. Copy `spike.nro` to `sdmc:/switch/` on a console with Atmosphère.
3. Start the homebrew menu **through a game** (hold R while starting any game) so that it runs with the
   application's memory; from the album it has less memory (it still runs, and says which it was).
4. It shows its results and writes them to `sdmc:/switch/bt3_spike.txt`; + exits. The screen turns orange for a
   moment during the OpenGL test.
5. On a PC, the same speed test for comparison (see the top of `port/switch/spike/source/bench.c`).

Please run it twice (the window's place changes from start to start) and add both reports here.

What it does (`port/switch/spike/source/main.c`):
- **Memory window**: prints the address space and its regions, finds a 4 GB-aligned free window (the fixed 256 GB
  first), maps `plat_mem.c`'s regions there (the low arena with 16 of its 192 MB) with svcMapProcessCodeMemory +
  svcSetProcessMemoryPermission, writes and reads both ends of each, checks that the low 32 bits of each address are
  the PS2 address, maps two instructions at W + 0x20000000 read-execute and calls them, then unmaps everything.
- **Processor**: the CPU clock (clkrst), then `bench.c`: the game's own soft-float routines
  (`port/src/softfloat_ps2.c`) in a dependent chain, and a 16 MB random memory walk.
- **OpenGL**: an EGL window and an OpenGL 3.3 core context (what `gs_gl.c` asks for), the version, renderer and
  whether clip control is there, then every program of `gs_gl.c` compiled and linked from the port's shaders with
  `gs_gl.c`'s translation (repeated in the spike; keep the two the same), and one frame to the screen.

## Results

### 2026-10-29, first run (a player's console)

Running as an application through a game (hold R), system 22.5.0, Atmosphère; CPU at 1785 MHz (overclocked: the
stock clock is 1020 MHz). The whole report, as written to `sdmc:/switch/bt3_spike.txt`:

```
[1] Memory window
  address space 0x8000000 + 0x7ff8000000
  heap region   0x5863000000 + 0x200000000
  alias region  0x6bab400000 + 0x1000000000
  stack region  0x3adfc00000 + 0x80000000
  memory: 3285 MB for this process, 3281 MB used
  process handle 0x581fa; svcs allowed: MapProcessCodeMemory 1, SetProcessMemoryPermission 1, Unmap 1
  window W = 0x4000000000 (the fixed choice is free)
  ok   game heap                  W+003BE000, 28936 KB
  ok   game stack                 W+02000000, 16384 KB
  ok   hardware registers         W+10000000, 64 KB
  ok   GS registers               W+12000000, 8 KB
  ok   low arena (16 of 192 MB)   W+13000000, 16384 KB
  ok   scratchpad                 W+70000000, 16 KB
  ok   code at W+20000000 returned 42 (42 expected)
  memory window: PASS

[2] Processor
  CPU clock: 1785 MHz
  soft float: 13.52 ns per operation (check 3E600658)
  memory walk: 3.91 ns per step (check 7F8C0DCC)

[3] OpenGL
  4.3 (Core Profile) Mesa 20.1.0-rc3
  NV120
  GLSL 4.30
  context 4.3 core, clip control yes
  ok   gs          63 ms
  ok   vu0         66 ms
  ok   vu4         49 ms
  ok   vu6         52 ms
  ok   gs nz       42 ms
  ok   vu0 nz      64 ms
  ok   vu4 nz      47 ms
  ok   vu6 nz      49 ms
  ok   gs wb       70 ms
  ok   outline     22 ms
  ok   alphakey     5 ms
  ok   dclut        7 ms
  OpenGL: PASS
```

What it settles:
- **The memory model works on Horizon.** The calls are allowed to a program under the homebrew loader, the regions
  map at W + their PS2 addresses, and code mapped at W + 0x20000000 runs. The fixed W of 256 GB was free: ASLR put
  the stack region at 235 GB, the heap at 353 GB and the alias region at 430 GB, all clear of it, but they move from
  start to start, so the launcher still checks it and falls back to another 4 GB-aligned window (the second run is
  to show how often it must).
- **The checks' results are the PC's** (`check 3E600658`, `check 7F8C0DCC`, the same as the Xeon's): the software
  float gives the same bits on the console, as the online play with PCs needs.
- **Speed**: 13.5 ns per soft-float operation against 5.7-6.9 ns on the Xeon at 2.1 GHz (about 2.2 times), the memory
  walk 3.9 ns against 2.8 ns, at 1785 MHz.
- **OpenGL**: Mesa gives a 4.3 core context when 3.3 is asked for, with clip control (so `gs_gl.c` takes its
  `glClipControl` path); all twelve programs compile and link. Compiling them takes about 0.5 s in all, once at
  start.
- The process had 3285 MB, nearly all of it taken by libnx's heap at start (as homebrew does), so the regions come
  from that heap, as the launcher's would.

### 2026-10-29, second and third runs (the same console, stock clock)

Two more starts with the CPU at the stock 1020 MHz, both passed; the report file keeps the last one. What differs
from the first run:

```
  heap region   0x4b93a00000 + 0x200000000
  alias region  0xebec00000 + 0x1000000000
  stack region  0x612e800000 + 0x80000000
  window W = 0x4000000000 (the fixed choice is free)
  (every region and the code at W+20000000: ok, as in the first run)
  CPU clock: 1020 MHz
  soft float: 23.71 ns per operation (check 3E600658)
  memory walk: 6.86 ns per step (check 7F8C0DCC)
  context 4.3 core, clip control yes
  ok   gs         101 ms        ok   gs nz       70 ms        ok   outline     37 ms
  ok   vu0        109 ms        ok   vu0 nz     106 ms        ok   alphakey     9 ms
  ok   vu4         84 ms        ok   vu4 nz      79 ms        ok   dclut       12 ms
  ok   vu6         86 ms        ok   vu6 nz      82 ms
  ok   gs wb      115 ms
  OpenGL: PASS
```

- **The fixed W was free in all three starts.** The regions moved (this time the alias region at 59-123 GB, the
  heap at 302 GB, the stack at 388 GB), never over 256-260 GB. The address space is 512 GB and the three regions
  take 74 GB of it, so a collision is possible but should be uncommon; the launcher keeps the fallback to another
  4 GB-aligned window, and the program is built so that it can be loaded at any.
- **Stock speed**: 23.7 ns per soft-float operation, 3.4 to 4.2 times the Xeon's 5.7-6.9 ns, and 1.75 times the
  1785 MHz figure (the clock ratio is 1.75: the code is bound by the processor, not by memory). The memory walk 6.9 ns
  against 2.8 ns. The same checks as the PC's again.
- **Shader compiling at stock**: about 0.9 s for all twelve programs, once at start.
- What this means for the game is measured in phase 3 (its demo fight's time per field on the console). If stock is
  too slow, the hot paths can be made faster with the same results, and an overclock helps in proportion.

### 2026-10-29, phase 3: the demo fight on a console (the same console)

The test kit of commit 6fbe4da (the launcher choosing any free window), started from the homebrew menu through a
game. (The kit before it stopped at the launcher: the fixed window at 256 GB was taken by one of the system's
regions in two starts in a row; hence the moving program.) `bt3_log.txt`:

```
bt3: attract-demo battle
bt3: battle finished
```

`bt3_hash.txt` against the same fight on the player's Windows PC (`bt3_hash_pc.txt`, the release installed there):

```
$ compare_hash.py bt3_hash_pc.txt bt3_hash.txt --fight
fight values identical for 4242 blanks of fighting
```

Both runs have 4428 blanks, as the Linux and macOS runs of this fight. The checksum of all of the game's memory
differs from the first blank, as between any two builds (it holds addresses of the program's functions and
variables); the fight's own values (both fighters' health and positions, the clock, the random generator) are the
same at every blank. So the Switch computes the game exactly as the PC does: the memory window, the moved program,
the software float on the console's processor, the data tables fetched from the player's disc.

The launcher showed "The game ended after 89 s, result 0". The fight is 4428 blanks, 73.8 s at 60 a second: the
whole run, with loading the game's data at start and BT3_HASH's checksum of all of the game's memory (the 28 MB heap
among it) written to the card at every blank, took 1.2 times the fight's real time. The checksum alone is a large
part of that, so the game's own work is likely within the 16.7 ms of a blank; a run without BT3_HASH measures it.

### 2026-10-29, phase 4: the game's first start on a console (the same console)

The kit of commit 1872257. The port's language list (the first screen, drawn by the game's own frames through the
GL renderer) came up and worked with the controller. After a language was chosen the screen stayed black, with no
sound, and the whole console slowed down: the volume buttons and HOME answered only after about 10 seconds; + and −
did not open the settings window; nothing changed in 5 minutes. Every later start was the same from its first frame
(the choice was saved, so the list is not shown again and the game goes straight on to the same point).
`bt3_log.txt` and `bt3_log_prev.txt` were empty: the log's lines were not on the card when the game was closed from
outside (the phase 3 run, which ended by itself, had its lines).

What the two kinds of start share is the first sound: the confirm sound of the language list, or the first sound
after it; that is when the game opens the sound output (`gs/snd_adx.c`), through SDL's Switch driver, which no
earlier console run had used (phase 3 ran without picture or sound). A sound driver whose mixing thread never waits
(a buffer refused by audout, so the thread is at once ready for the next) keeps the console's sound service busy,
which would slow the volume buttons and HOME as seen. Not shown yet; the next kit tells:
- the log is written to the card twice a second (`fsync` in a thread of its own, `plat_mem.c`), so it survives a hang;
- the same thread writes a line when the game has drawn nothing for 3 seconds (with how far the start had got);
- the sound driver writes what audout refuses (the first five times, with the error code) and when it starts
  playing, and waits a period after a refused buffer or a failed wait instead of trying again at once;
- `bt3_env.txt` has `#BT3_NOSOUND=1`: with the `#` removed the game starts without sound, which shows whether the
  sound is what hangs it.

The kit of commit 910eb9f (the above), the same console: the same black screen and slow console with sound and
with `BT3_NOSOUND=1`, and the log empty both times, even the `bt3_env.txt` line written before anything else (the
language list's sounds had played in the first start). So the sound is not it, and the log's twice-a-second
`fsync` did not reach the card either: whatever slows the console holds up the program's own file writes too, as
it holds up the console's menus. The next kit counts instead of guessing (`port/src/plat_nxfs.c`): every call of
libnx's file service (the C library's fopen, fread, stat, mkdir... go through them; the link wraps them) is
counted by kind, with the path most named; past 4000 calls a second each further call waits a millisecond (the
console stays usable and the program goes on, slower); and the watch thread writes, every 2 seconds, a line to
`bt3_watch.txt` (opened and closed each time) with the counts, how late it woke, the frame counters and the start's
stage. A flood of file calls shows there by kind and path; a console slowed by something else (the GPU, a busy
processor) shows as few calls with nothing drawn, or the thread waking late.

The kit of commit 97da336: no `bt3_watch.txt` at all, the log empty again. The player's answers: no text of the
launcher on the screen before the black; the console slow for about the first minute only, then normal while the
game stays black; the same after a restart of the console, every time. So nothing stays jammed between starts, and
the watch thread (then started at the top of main()) never wrote its first line. What the code shows:
- the launcher draws its screen only once the program is loaded (its first `consoleUpdate`), so a black screen from
  the start does not say where it is;
- newlib's buffered reads ask the file service for 1 KB at a time, whatever the size asked for (`__swhatbuf_r`:
  `BUFSIZ`, 1024; `fread` refills the buffer in a loop; only an unbuffered stream reads straight into the caller's
  memory): the launcher reading the 13.8 MB program was some 13,500 requests to the card, and the game's start
  (its disc's programs, 2388 runs from them, then its files) many more; enough to keep the console's menus waiting
  for the first minute;
- a thread of the game stuck while holding one of the C library's locks (`fopen` takes a lock of all the streams)
  would stop the watch thread at its first `fopen`, before anything reached the card, the log's `fsync` included.
The next kit: the launcher shows its lines at once and reads the program in 1 MB pieces (`setvbuf`), with the time
it took; the game's `fopen` is wrapped to give every file a 32 KB buffer; the watch thread starts first of all
(`nx_start`, before the game's data is read) and writes `bt3_watch.txt` through libnx's file service directly (no C
library lock), with any file call under way for over a second (its kind and path, main thread or another) and the
main thread's last file call.

The kit of commit c1f9186: the launcher's lines showed (for a moment: "Window: 0x300000000 (the usual one was in use
this time)", "Loading the game..."), and `bt3_watch.txt` came back. What it says:
- the main thread drew 69 frames, made its last file call at 2.7 s (`stat` of `saves/card1/BASLUS-21678DBZT3`, the
  memory card's check at boot) and then neither drew nor called the file service again for the 80 s the run lasted;
- another thread called the file service without pause: open, entry type, time stamp, get size, close, file after
  file of `gamedata/pzs3us1/` and `pzs3us2/` (`stat` of every file), 2000 to 20,000 calls a second, while single
  reads of the game's waited in the service's queue for 1.4 to 7 s, and the watch thread's own writes made it wake
  up to 17 s late. That thread is the game data check (`plat_verify.c`): at every start a `stat` of each of the
  68,605 files of the disc and of its place under `mods/`, and when their sizes and times differ from the ones
  kept in `.verified` (as on a first start anywhere) a read of all 3 GB. Instant on a PC; on the Switch each `stat`
  is about five requests to the file service and the pass takes minutes;
- the log was empty because it never got anything: in the whole run not one write. `nx_start` opened bt3_log.txt
  twice (freopen for stdout, then for stderr); the Switch's file system lets one handle write a file, so the second
  freopen failed, which closed stderr, where the program writes nearly all it says. (Phase 3's two lines are
  printed to stdout.)
Changed: the check runs on the Switch only with `BT3_VERIFY=1` (its fingerprint is for online play, phase 6; a
cheaper check there would list each folder once, `fsDirRead` giving every file's size, rather than `stat` each
file); the log is opened once and made descriptors 1 and 2 (`dup2`); and `gPortWhere` (plat_stub.c) marks where
the game's thread is among the port's main parts (the blank, the controllers, game files, the memory card, the
sound's stream locks, a texture's decoding, the picture), reported by the watch thread, should the main thread
still stand still with the file service quiet.

The kit of commit e5996aa: the console stayed responsive (the file service quiet: a few calls a second), the log
came back (the display, the GL renderer "NV120 (4.3 (Core Profile) Mesa 20.1.0-rc3)", the menu fonts, sound
playing through audout, the European menu art paired in 20 s on its thread), and the game still stood still at
frame 69 from 4 s on, the watch thread's last mark of the game's thread being "decoding a texture" (gs_draw.c,
`tex_decode`): a large texture is decoded by worker threads while the game's thread waits for them all. That mark
was set on the way in only, so the thread may also have been somewhere after it. The next kit says which: the
mark is set again when the decoding ends and while the game's thread waits for the workers, the watch thread
reports each worker's state (`GsDraw_DecodeReport`: started or not, waiting, decoding, done) and, in builds with
`BT3_NX_TRACE=1` (the `switch` workflow's, for now), the game's thread's calls in the renderer, which is compiled
with `-finstrument-functions` for it (`NxTrace_Report`, plat_nxfs.c: offsets from the program's start, looked up
in `bt3_nx.map`). And `bt3_env.txt` offers `BT3_TEX_THREADS=0`: no workers at all.

The kit of commit 6352c02, two runs. With the texture workers (as built): `texture decoding: -1 workers (0 started)`
and the game's thread in `tex_decode` (under `texture_get`, `GsGpu_Draw`, ... `Port_GsVif1Chain`), before the
number of workers was even set: in `SDL_GetNumLogicalCPUCores`, whose Switch build used `sysctlbyname("hw.ncpu")`
(SDL's configure found the symbol; newlib here has no `sysconf`). libnx's `sysctl` is the BSD sockets service's
(`socket.o`), and the program never starts that service (`socketInitialize`): the call never returned. With
`BT3_TEX_THREADS=0`: the game ran, to the main menu and on (the player's photo and video: the Italian menus, the
music), and crashed entering a fight (nothing of it in the log: so not the port's handler, plat_crash.c, nor a stub;
the report asked for: bt3_crash.txt if any, Atmosphere's crash_reports/). Changed: SDL built with
`HAVE_SYSCTLBYNAME=OFF` (its other uses of sysctl, the RAM and page size, behind the same switch), two texture
workers on the Switch (its three cores: the game's thread on one, the sound on another), and the trace
(`BT3_NX_TRACE`) off in the workflow's build again: it instruments the renderer's per-pixel functions too, which made
the decoding of the menu's textures on the game's thread (no workers) slower still. The main menu stuttered at each
move (the player's video: the cursor moves, the line of description at the bottom changes a moment later): the trace
caught the game's thread in `tex_decode_rows` / `Gs_VramRead` there; with the workers back and no trace, to be seen.
(For online play, phase 6: the sockets service has to be started first.)

The kit of commit c52854b: the game ran to the menus and into fights, without the black screen. The player:
fights at 6 to 11 pictures a second (30 is the game's), the main menus (title, continue, mode select) stuttering at
each move, worse than before (the character select smooth), and a crash at the start of one fight
(`bt3_crash.txt`): a null read in `memcpy` from `vif()` (gs_core.c), its data pointer 0: a DMA tag of the frame's
list with address 0 (REF / REFS / REFE). On the PS2 that reads the first kilobytes of memory; here address 0 is
NULL (Port_Ptr). Changed:
- `walk_chain` leaves such a block out and says so once in the log (with the tag's address, id and size);
- the European menu art (`MenuLabels_ArtSwap`, the player has the European disc's archives): each swap decoded the
  picture again, setting up its 5 MB model of GS memory every time; the main menu's sheets carry the PC's item and
  are made again at each move. The last 32 pictures decoded are kept;
- the slow-frame line of `BT3_GS_VERBOSE` splits the texture time into the European art, the checks for battle
  text and the menus' own text; the test kit's `bt3_env.txt` sets `BT3_GS_VERBOSE=1` for these tests: where each
  frame's time goes (the game's code, the list and its VU1 work, textures, pipelines, the frame's end);
- the settings window on the Switch: no Full screen, Display, Renderer (Vulkan: not built for the Switch, Mesa's
  OpenGL only), render thread, mouse pointer, update check (and no check at start: the Switch's updates are phase
  5's), no keyboard column in Controls, and the buttons by the Switch's names;
- the launcher's icon (the game's cover, 256 x 256, `port/switch/launcher/icon.jpg`).

The kit of commit 9380613 (`BT3_GS_VERBOSE=1`), the same console: no crash entering a fight; the fight's HUD (health
bars) and its pause menu not shown; the main menus still stuttering, at the press of Start and at each move in the
mode select, except to and from "Tenkaichi Budokai Online" (the PC's own item, untranslated); with sys-clk at
1785 MHz CPU / 614 MHz GPU / 1331 MHz memory, a fight from 6 to 12-13 pictures a second. The log:
- a fight's frame, about 49 ms: the game's code 7 to 8 ms, the display list 41 ms, of which "submitting the frame"
  (GsGpu_FrameEnd, the OpenGL back end) about 35 ms, for about 2100 draws and 42,000 vertices a frame. The game's
  code (software float) is not what is slow; the menus' frames take about 5 ms;
- the main menus' stutters: frames of 1 to 3 s, nearly all of it the game's code (frame 3573: 3.2 s of 3.3), with
  a file or two of `pzs3us2` read at each (a few small reads), the texture work only milliseconds.
Changed:
- the OpenGL back end, per frame: every draw's uniform blocks (its 80-byte Params, a VU1 draw's 320-byte U, equal ones
  in a row once) are gathered before the draws, uploaded with one glBufferData and bound by range
  (glBindBufferRange), instead of a glBufferSubData of one small buffer before each draw; the vertex and index
  buffers are given new storage of the frame's size with their data (glBufferData) rather than written over while
  the previous frame's draws may still read them. `BT3_GS_VERBOSE` adds where the frame's end spends its time
  (uploads, issuing the draws, the picture and overlay, the swap) and logs the picture's placement only when it
  changes. `bt3_env.txt` offers `mesa_glthread=true` (Mesa's GL on a thread of its own) to compare;
- a sampling profile of the game's thread (plat_mem.c, nx_prof): about 250 times a second it is paused
  (svcSetThreadActivity), its pc and lr read (svcGetThreadContext3) and it goes on; in bt3_watch.txt the most frequent
  addresses of the samples taken in stalls (no vertical blank for 100 ms), every 2 s, and of all samples every
  30 s (offsets from the program's start; `BT3_PROFILE=0`: none). For the menus' stalls and the fights' time alike;
- the slow-frame line also gives the game's own file reads and opens (time, number, bytes; plat_fcache.c);
- `BT3_HUD_LOG=1` in the test kit: whether a fight's HUD pieces are drawn at all;
- on the Switch, the start without the logos, the intro movie and the copyright page by default (the movies flicker),
  and the settings that do not apply there (full screen, display, renderer, render thread, mouse pointer, update
  check) neither kept nor written.

The kit of commit c93129d, the same console, two runs (the second with `mesa_glthread=true`): a fight at 20 to 30
pictures a second without Mesa's thread (with it, slower again), with stops of a second or more; the HUD and the
pause menu still not shown (`BT3_HUD_LOG` logged no HUD piece at all); the main menus' stutters as before. The log
and the profile:
- a fight's frame, about 34 ms: the game's code 9 ms, the display list 25 ms, of which the OpenGL back end 17.5 ms
  (uploads 2.7, issuing the 2100 draws 13.4);
- every stall, in the menus and in fights, is the file service: 95 to 100% of the stall samples in
  `svcSendSyncRequest`, from libnx's `fsFsOpenFile` and `fsFsGetFileTimeStampRaw` (which fsdev's open asks for
  too), opening files of `gamedata/pzs3us2` (the disc's PZS3US2.AFS unpacked: about 65,000 files in one folder).
  On the card an open there takes 0.3 to 0.4 s (the console reads the folder from its start to find a name). The
  menus play a spoken guide for the item chosen (`Port_FilePath` and the stream's own open, snd_adx.c), the
  fights their fighters' lines: the online item has no guide, the one move without a stop. The game's own file
  reads (plat_fcache.c) were not it: 0 ms in those frames.
Changed:
- **the packs** (plat_nxfs.c, the Switch only): a folder with `<folder>.afs` next to it (an archive of the disc's own
  kind: "AFS\0", the number of files, an offset and a size for each) is read from it: `fopen` of
  `<folder>/<number>.bin` for reading gives the pack's piece (`funopen`, one handle for the pack, its table read
  once), and the folder is not looked at. `port/switch/pack_gamedata.py` (in the kit; Python 3, nothing to install)
  makes the pack from the card's copy of the folder (or from the PC's onto the card, `--out`), checks it and with
  `--delete` removes the folder. A slow open of a folder without a pack is said once in the log;
- **the render thread on the Switch**, always (`BT3_GS_THREAD=0` to compare): the list and the OpenGL back end on a
  thread of their own, pinned to core 1 (the game's thread is on core 0, the sound's on core 2), with an 8 MB stack
  (Mesa's shader compiler runs there). 9 ms of the game beside 25 of the drawing fit in a fight's 33. With OpenGL on
  the thread the cross-fade's snapshot of the frame is taken on that thread (a job of its own), so the fight's scene
  changes keep their cross-fade; reading the picture back to the game is still left out;
- `BT3_HUD_LOG`: every 30 frames, the game's markers for the HUD by kind, the primitives drawn after a side's
  marker, those the renderer kept and those of them that are 2D with alpha 0; `BT3_GS_DUMP=hud`: the frame 300 after
  the first HUD marker listed primitive by primitive, its markers included;
- `bt3_env.txt` no longer offers `mesa_glthread` (slower on the console).

The kit of commit d85cc4b, the same console, with the pack made (`gamedata/pzs3us2/: read from gamedata/pzs3us2.afs
(65201 files, 1160 MB)`): the game started and ran (sound, input) but the screen stayed black. The log: every
target "framebuffer incomplete" from frame 0 and GL_INVALID_OPERATION at every frame. With the render thread, the
game's own transfers on the GIF channel (`Port_GsGifChannel`: it clears its frame buffers that way at the start, and
uploads textures) ran on the game's thread, where the OpenGL context is not current: the targets were made there
without a context. Changed: with OpenGL on the render thread such a transfer is a job of that thread (the game's
thread waits for it). `BT3_GS_DUMP=hud` listed frame 0 (`atoi("hud")`): fixed. The launcher's icon from a
higher-quality cover. Merged: upstream v0.1.39.2 (widescreen menus beta, macOS fixes).

The kit of commit 3617802, the same console: a picture again, and the main menus without stutters (the pack). Italian
menus after 10 to 15 s; fights at 9-10 pictures a second at the stock clock, 18-28 with sys-clk; no HUD, and no
effects at all (ki blasts, beams, particles); during a super attack the HUD and the effects were there, and gone
after it. `BT3_HUD_LOG`: in a fight no HUD marker at all, not even the one that starts the battle's 2D
(`BtlGame_Draw`), except during the super attack. The frame listed (`BT3_GS_DUMP=hud`) ended at
`dma tag at 01001c40: id 2 qwc 29 addr 00000000` inside a CALL: a NEXT to address 0, where the walk stops
(Port_Ptr(0) is NULL), so everything the game put in the list after it (effects, HUD, pause menu) was never drawn.
The packet is the fighters' shadow (`ObjShadow_BuildPacket`, its words 0x6C018000 / 0x8000), whose tag is a RET:
`pkt[0] = (p - pkt) / 4 + 0x5FFFFFFE`. `pkt` is a parameter (ptr32.py keeps those 64-bit host pointers), `p` a
4-byte game pointer, and clang subtracts their ptrtoint to 64 bits: the window's base W was in the difference, and
W / 16 put bits 32 to 35 of W on the tag's id. The fixed window (0x4000000000) has those bits 0, as has the Mac's
(1 TB): this console's launcher takes another window (the fixed one is the system's there, phase 3), and the RET
became a NEXT. In the super attack's scenes no shadow is drawn. Changed:
- the window pass (macwin.cpp): a 64-bit ptrtoint of an ordinary pointer subtracted from (or from) a 4-byte
  pointer's is taken as its game address, as is a global's address in such a difference. Of the whole game only
  gfxm_e_b.o compiles differently;
- `walk_chain` says once when the list leads to address 0 (the rest was dropped silently);
- the log in 64 KB blocks, put on the card by the watch thread twice a second (it takes the stream's lock only when
  free); line by line, each line was a write of about 4 ms on the thread writing it. `BT3_GS_DUMP=hud` is out of
  `bt3_env.txt` (written line by line, it held the game for minutes: the "stuck" after the super attack);
- the European menu art's pairing (12.4 s on the console, the Italian pictures missing until then) kept in
  `gamedata/game-text/menu_art_<bank>.cache` once made, and read from it at the next start (the Switch only).

The kit of commit 1b0a10a, the same console, two starts: Italian at once on the second start (menu art from its
cache, 0.6 s); the HUD, the effects and the pause menu shown. The first start's fight good, the second's worse
(drops to 9 pictures a second, the HUD jumping about). The logs: OpenGL's "issuing the draws" 14.1 ms in the first,
24.5 ms in the second, the ratio of 1785 MHz to 1020 MHz: the second ran at the stock clock (sys-clk's profile not
applied: its profiles are per title, and the homebrew menu runs as the game held R on). Ticks not shown: about 35 of
120 in the first, 75 of 120 in the second (the HUD's own moves, its shake on a hit, seen at a third of its pictures,
unevenly). "Uploads" 5 to 9 ms (OC) and 9 to 25 ms (stock), more than before the render thread. Changed:
- OpenGL's frame buffers in three sets used in turn (gs_gl.c): made once, written in place through an
  unsynchronised mapping, a fence behind each frame's draws waited for before its set is written again (the
  Switch; `BT3_GL_RING=0` for the old way: new storage of every buffer every frame);
- `BT3_GS_VERBOSE`: the time waited for the GPU at those fences (the GPU's share), and how many draws could be one
  with the draw before (the same state, the next vertices): what joining them would save.

The kit of commit e92c2f2, the same console, overclocked (sys-clk 1785 MHz): "perfect, no issues whatsoever, a clean
30 pictures a second (25 at the least in some scenes)". The log of a fight: OpenGL's uploads 1.9 ms (8.5 before the
buffers in turn), waiting for the GPU 0.01 ms (the GPU is not what limits), issuing the draws 14.5 ms, 1 to 12 ticks
of 120 not shown; 0 of about 2270 draws a frame could be joined with the draw before (each has its own uniform
block), so joining draws is not worth doing. At the stock clock the draws' 14.5 ms would be about 25: the driver's
CPU work is what an unclocked console still lacks. Changed: vibration at 30% by default on the Switch (at 100% too
strong in the Joy-Cons, the player's report), in the settings' default and the Controls tab's reset alike.

The same kit at the stock clock (sys-clk off), with the intro movie on: fights at about 15 pictures a second, uneven,
the HUD jumping (61 to 68 ticks of 120 not shown); OpenGL's draws 24 ms a fight frame (14.5 at 1785 MHz), the GPU
never waited for. The intro movie: every fifth frame about 90 ms of the game's own time (the movie's decoding, at
the stock clock), 89 ticks of 120 skipped, and the picture flickering black (only the intro movie, not the legal
screens). The movie puts its pictures straight into a display buffer (gs_core.c, transfer_data), and the copy into
that buffer's texture is a draw of the tick (GsGpu_FbUpload), recorded once while the upload stays marked: a
skipped tick dropped its draws, the mark stayed, the next tick recorded no copy, and the buffer shown had an older
picture or none. Changed:
- after a skipped tick, the copy of a picture uploaded in it is recorded again (gs_draw.c, GsGpu_FrameEnd);
- the sampling profile takes the render thread too, every 30 s in bt3_watch.txt with its callers: where the OpenGL
  driver's 24 ms go (Mesa's functions are in bt3_nx's symbols);
- `MESA_NO_ERROR=1` on the Switch (Mesa without its per-call error checks; no GL error in any run since the
  black-screen kit); `MESA_NO_ERROR=0` in bt3_env.txt to compare;
- the launcher is named "Dragon Ball Z: Budokai Tenkaichi 3"; the NSP is dropped (see "The NSP").

The kit of commit e4b61c9, two runs. Overclocked: fights perfect; the intro movie at 20-26 pictures a second with a
slight flicker (less than before). Stock clock: the intro movie at 6 pictures a second without any flicker; fights
uneven, the HUD seeming to jump (the player's video: the HUD in place and whole in every picture, its own animations
seen at uneven intervals, about half of the ticks not shown). The render thread's profile, during the movie
(overclocked): 44% in frame_end's copy of the movie's picture out of GS memory (gs_gl.c, `Gs_VramRead` pixel by
pixel, 1024 x 1024 of them per upload); the game's thread decoding the MPEG-2 (plat_mpeg2.c, integer only) and
sleeping the rest. In the overclocked fight the render thread waited 58% of the time. No 30-second report covered
the stock fight. Changed:
- `Gs_FrameRead32` (gs_core.c): a 32-bit buffer read a block of 8 x 8 pixels at a time (one address per block, the
  column table within it), checked equal to the pixel-by-pixel reading over the whole 1024 x 1024; both back ends'
  copies of an uploaded picture use it;
- the render thread's profile every 10 s (the game thread's stays at 30 s).

The kit of commit a96e4fc, two runs. The intro movie perfect in both (30 pictures a second, no flicker, the stock
clock too); fights perfect overclocked, at the stock clock 10 to 15 pictures a second with very uneven pacing. The
stock fight's log: every third tick 55 ms of the game's time, of it 26 ms waiting for the render thread; the render
thread about 43 ms per drawn list (OpenGL's draws 23 ms, uploads 2.5, the list's walk the rest), idle a third of
the time; the game's thread waiting 42% of it. The pacing: sceGsSwapDBuff sets the buffer the next frame is drawn
into (and shown from, gGsMainFbp) before the game waits for the render thread, so a list still being drawn read
the next frame's buffer and showed the other one, an older picture (two or more ticks old when the tick before
was skipped). Overclocked the list is done before the game gets there, which is why those runs were smooth.
Changed:
- gGsMainFbp is the render thread's: each job takes the game's value as it was when the job was handed over
  (gs_sony.c `gGsMainFbpGame`, gs_core.c `sJobFbp`);
- `BT3_GS_VERBOSE`: per 60 frames, the game's thread's waits for the render thread (in GIF channel transfers, at
  the frame's end, elsewhere) and the render thread's time per drawn and per skipped list, of it ending the frame.

The kit of commit 5e79e35, the stock clock: the pacing and the HUD right ("no inconsistencies"), 10 to 15 pictures
a second. The new timing: the game's thread waited for the render thread only 0 to 3.5 ms a frame at the sites
timed (GIF channel transfers, the frame's end); the render thread 41 ms per drawn list (27 of it ending the frame),
5 ms per skipped one, idle about 40% of the time; yet the game's thread was 42% in a condition wait. That is the pad
read's wait for the drawing of the last list (Gs_RenderListWait, when the last blank was waited for): after skipped
ticks the game is ahead of its grid, waits there for the render thread, and then computes its tick while the render
thread has nothing to do. Changed:
- no wait before the pad read while ticks are being left out (one in the last 60, gs_draw.c
  `GsDraw_SkippedRecently`); its time is in the `BT3_GS_VERBOSE` line with the other waits.
At 41 ms per drawn list and 5 per skipped one the render thread can show about 23 of the 30 ticks a second; 30 at the
stock clock needs the list's walk (about 14 ms) off the thread that issues OpenGL's draws.

The kit of commit 2f12352, the stock clock: 16 to 19 pictures a second (10 to 15 before). The fight's log: 35 of 60
ticks drawn, the render thread 43 ms per drawn list (29 of it OpenGL's end of the frame) and 5 per skipped one, busy
82% of the time: the render thread is what limits now; the game's thread waited 13 ms a frame for it (at the frame's
end, in sceGsSwapDBuff's transfer), and no longer before the pad read. Saving checked: the "unlock everything"
cheat's state, saved, survived a restart. Changed:
- the GL thread (gs_gl.c, "the GL thread"; on the Switch by default, `BT3_GL_THREAD=0` in bt3_env.txt for the old
  way): the context, the window and every OpenGL call are its own; the render thread walks the lists and records the
  draws as before, and hands each frame over to it (the arrays of draws, vertices and uniform blocks in two sets in
  turn, the targets' state copied), so that the walk of frame n + 1 goes on while frame n's draws are issued. The back
  end's calls during the walk (textures made, filled, deleted; targets made, dropped) are queued in order, textures
  named by ids of gs_gl.c's that the GL thread maps to its GL names; reading a target back or copying one waits for
  the queue. The GL thread is on core 1, the render thread moves to core 2 (beside the sound; `BT3_RENDER_CORE`);
  Smooth motion is off with it (its pictures swap the frame's arrays). `BT3_GS_VERBOSE`: how long the GL thread
  waited for work per frame.

The kit of commit 7a5e6bd (the GL thread), the stock clock: no crash, the sound right, 1 or 2 pictures a second more,
the pacing again slightly uneven and some combos missed. The log: the render thread 34 to 37 ms per drawn list (19
to 21 of it the hand-over, waiting for the GL thread); the GL thread waited for work only 3 to 7 ms a frame, its
frame end now with 25 ms in "the picture and the overlay" (0.8 before), 40% of its time in svcWaitSynchronization
from waitObjects: the present waiting for the screen's next buffer, which is the GPU (at its stock clock) not done
with the one before. With the CPU's work split, the GPU is what limits at the stock clocks; the extra frame in the
pipeline is a frame more between a press and its picture. Decided with the player: the overclock is a requirement
(fights overclocked were a clean 30 in every kit). Changed:
- the GL thread off by default (`BT3_GL_THREAD=1` to have it); the fixes before it (each job's shown buffer, no
  wait before the pad read while ticks are skipped) stay;
- the settings window's footer with the Switch's names: "B: choose   A: back   L / R: tab";
- added songs and stages checked working on the Switch.

Settings on the Switch (2026-10-30, the player's requests):
- Internal resolution 1x to 3x (4x closed the game: out of memory), 1x marked recommended and the default where none
  was saved (gs_draw.c, GsGpu_Init / GsGpu_SetSettings);
- no Smooth motion (each in-between picture is another pass through OpenGL: 14 ms overclocked on top of the real
  picture's, more than a tick holds) and no V-sync (the system shows each finished picture at the screen's next
  refresh: nothing tears), both left out of the settings file there (plat_settings.c); aspect ratios up to 16:9;
- "Nintendo layout in the menus (A: choose, B: back)" in Controls (setting `menu_nintendo_ab`, off by default): the
  game's menus choose with Cross and go back with Triangle, by position B and X; with it, while the menus run
  (headless.c `gPortInMenus`) a controller's A is Cross, B Triangle and X Circle (gs_input.c, menu_ab), and the
  settings windows and the quit question swap choosing and going back the same way. Fights, the pause menu
  included, are left as they are.
