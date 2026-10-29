# Nintendo Switch (Atmosphère) port: plan

The Switch port is IN PROGRESS: the game builds for the Switch, but nothing of it runs there yet. This file is the design, the plan and the
record of what was checked; update it as phases land, as docs/port/macos_plan.md is for the Mac.

Status 2026-10-29: phase 4 under way: the Switch's drivers for SDL3 (video with OpenGL through Mesa, sound,
controllers) are written and the game links with them. Its first start on a console stopped after the language list
(a black screen, the console slowed down, see "Results"); the kit after it says why (`switch-test-kit`).
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
| 4. Playable | The platform layer: picture (GL), sound, controllers; the game plays from boot to a fight and saves. Built (2026-10-29): SDL3's Switch drivers, the game's folder as the PC release's (`package.py`). First start on a console: hangs after the language list (see "Results"). |
| 5. The package | The setup's Switch mode, the NSP from the player's keys, `Tenkaichi3Decomp-switch.zip` in the release workflow, the updater's Switch asset and its restart through the launcher. |
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
