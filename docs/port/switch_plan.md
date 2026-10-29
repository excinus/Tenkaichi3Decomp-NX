# Nintendo Switch (Atmosphère) port: plan

The Switch port is PLANNED: nothing of the game runs on a Switch yet. This file is the design, the plan and the
record of what was checked; update it as phases land, as docs/port/macos_plan.md is for the Mac.

Status 2026-10-29: phase 1 done, phase 2 next. `port/switch/spike`, a homebrew program that checks on a console
what the port depends on (memory, speed, OpenGL), built by `.github/workflows/switch.yml`, passed everything in
three runs on a console (see "Results"): the 4 GB window maps at the fixed place every time, code runs at
W + 0x20000000, every OpenGL program of the renderer compiles, and the game's software float is 3.4 to 4.2 times
slower than on the build machine's Xeon at the stock 1020 MHz (about 2.2 times at 1785 MHz).

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
       Tenkaichi3Decomp.nro      the game                         (PC: Tenkaichi3Decomp / .exe)
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
4. **Play**: the HOME menu's icon starts the launcher in the NSP, which loads `Tenkaichi3Decomp.nro` from the
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

So the NSP holds a small launcher in the manner of nx-hbloader, which rarely changes:

1. finds the game's folder (`sdmc:/atmosphere/contents/0100B73DEC000000/`; a message with what to do when it is
   missing: run the setup, copy the folder, check the card);
2. finds a free 4 GB-aligned window W of 4 GB in the address space (svcQueryMemory, clear of the heap, alias and
   stack regions), and reserves it;
3. maps the game's regions at W + their PS2 addresses (heap 0x003BE000-0x02000000, its stack at 0x02000000, the
   register blocks at 0x10000000 and 0x12000000, the low arena at 0x13000000, the scratchpad at 0x70000000:
   `port/src/plat_mem.c`) and the program `Tenkaichi3Decomp.nro` at W + 0x20000000, with svcMapProcessCodeMemory
   and svcSetProcessMemoryPermission (code read-execute, data read-write), as homebrew loaders map a program;
4. starts it with the homebrew ABI (as nx-hbloader does: the heap, the process handle, the allowed calls, the
   arguments, and W), and when it returns asking to be loaded again (Restart now after an update), unmaps it and
   loads the new one.

The NPDM gives the launcher what that needs: the application's memory (about 3.2 GB), a 39-bit address space, and
the calls svcMapProcessCodeMemory (0x77), svcUnmapProcessCodeMemory (0x78), svcSetProcessMemoryPermission (0x73)
and svcQueryMemory.

Two open points, which the spike answers or narrows:
- **Where W lands.** ASLR places the heap, alias and stack regions at random in the 512 GB space each start; the
  spike reports whether a fixed W (256 GB) is free and which W it found. The program's 4-byte slots for addresses of
  its own globals hold offsets from W (`.long g - W` at link time), which do not change when W does, so the program
  can be linked once at W0 + 0x20000000 and loaded at any W + 0x20000000 with its 8-byte pointers relocated (as
  libnx's start-up does for a homebrew program) — if the linker accepts that combination; otherwise a post-link
  step writes those slots (as `make_dat.py` does on macOS).
- **The low arena.** On PC it is 192 MB of address space with pages taken as they are first used; Horizon has no
  such lazy memory, so on the Switch `Port_LowAlloc` maps more of it as it grows.

### The build (variant `nx64`)

As `mac64` (docs/port/macos_plan.md, "Native arm64: how the build works"), with ELF instead of Mach-O:

- game code: clang 22 (the release container already has it) to LLVM IR, the `macwin` window pass, then
  `llc -mtriple=aarch64-none-elf -mattr=-fp-armv8,-neon` (the game's floats in software, port/src/softfloat_ps2.c,
  as on every 64-bit build);
- port code and libraries: devkitA64 (GCC 15) with libnx, `-mtp=soft`, as homebrew is built;
- data tables (`.s`): a variant of `port/tools/macho.py` for ELF (window offsets);
- link: devkitA64's linker with libnx's `switch.specs` and the program placed at W0 + 0x20000000; then `elf2nro`.

`port/release/Dockerfile` gets devkitA64 (or the release workflow a second job in `devkitpro/devkita64`, as the spike
workflow does), so the Switch archive is built from the repository alone like the others (`BT3_SKELETON=1`: the
data tables are fetched from the player's disc).

### The platform layer

The port uses SDL3 throughout, and SDL3 has no public Switch port (the official one is for Nintendo developers
only, under their agreement: https://wiki.libsdl.org/SDL3/README-switch). What is used: about 150 functions outside
the GPU back end (counted 2026-10-29: windows and the GL context, gamepads and a virtual joystick, audio streams,
threads, mutexes, conditions, atomics, timers, files and folders, properties, processes for the PC's helpers), and
SDL_GPU only in `gs_gpu.c`, which the Switch does not use.

Plan: a `port/src/nx/` layer that provides that subset of the SDL3 API on top of devkitPro's SDL2 (`switch-sdl2`:
EGL window and GL context, Joy-Con and Pro Controller, audio, threads), with the few things SDL2 lacks done with
libnx directly (`SDL_GetBasePath` = the game's folder; no processes: the PC's helper programs are not run; software
keyboard for text). Writing the subset directly on libnx (nwindow + EGL, hid, audren) is the alternative if the SDL2
layer costs speed.

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
| 2. The game builds for the Switch | `BT3_CC=nx64` compiles and links the whole game with libnx (`BT3_SKELETON=1`), in CI. |
| 3. The demo fight on a console | The launcher loads the game; the attract-demo fight runs without a picture and its `BT3_HASH` lines (written to the card) equal Linux's (`compare_hash.py`). |
| 4. Playable | The platform layer: picture (GL), sound, controllers; the game plays from boot to a fight and saves. |
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
