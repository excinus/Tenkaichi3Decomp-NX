# Tenkaichi3Decomp for the Nintendo Switch

A native Nintendo Switch port of **Dragon Ball Z: Budokai Tenkaichi 3** (PlayStation 2, USA release, SLUS-21678),
based on z3xox's [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the PC port built on the
BT3-Decompiled decompilation. The game's own code is compiled for the Switch, with the PC port's OpenGL renderer,
sound and controls running on the console. It is not an emulator. It runs as homebrew, on Atmosphère.

This repository contains **no game data**. The game's data comes from your own disc of the USA release, through
the PC version, see [Game files](#game-files).

This is an unofficial port. It is not affiliated with the Tenkaichi3Decomp project; please report problems with
the Switch version here, not upstream.

<!-- screenshots -->

## What works

- The game from its opening film through the menus to the fights, saving and loading included; docked and
  handheld; the HOME menu and sleep.
- The PS2 version's screen effects (outlines, glow, blur, the depth tint), internal resolution 1x to 3x, and 4:3,
  16:10 and 16:9 pictures.
- Music, voices, sound effects and films.
- Two players on one console: **Duel → 1P vs 2P** opens the console's Controllers screen: a Joy-Con each, held
  sideways, or two controllers.
- Switch buttons throughout: the game's button icons show A, B, X, Y, L, R, ZL, ZR, the Controller page shows
  Joy-Cons, and the menus use A to choose and B to go back.
- Added songs and stages, texture packs and mods, as on the PC.
- Updates: the game says when a new release is out and installs it itself.

Fights run at the PS2 version's 30 fps **with [sys-clk](https://github.com/retronx-team/sys-clk) installed** (the
game sets the clocks itself, see [Notes](#notes)); without it they run at about 16 to 20 fps. Known issues:
two-player split screen runs at about 20 fps; online play is not available yet.

## Installation

Requirements:

- A Switch running [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) with the homebrew menu
- [sys-clk](https://github.com/retronx-team/sys-clk) (required for full speed)
- The PC version of Tenkaichi3Decomp installed from your disc (for its `gamedata` folder), and Python 3 on the PC
- Free space on the SD card for that `gamedata` folder (exFAT if a file in it is over 4 GB)

Steps:

1. Download `Tenkaichi3Decomp-switch-SD.zip` from the
   [releases page](https://github.com/excinus/Tenkaichi3Decomp-NX/releases) and copy its `atmosphere` and `switch`
   folders to the root of the SD card.
2. Copy the game files as described below.
3. Open the homebrew menu **through a game** (hold R while starting any game; from the album there is not enough
   memory) and start **Tenkaichi3Decomp**.

`INSTALL.txt` in the zip has the same steps in detail.

## Game files

The Switch version uses the game data of the PC version, which its setup makes from your disc image. Copy the
`gamedata` folder of your PC installation into the game's folder on the SD card:

```
atmosphere/contents/0100B73DEC000000/gamedata/disc/SLUS_216.78      (and the rest of gamedata)
```

Then pack its folder of 65,000 voice files into one file, once, with the card in the PC (the Switch is slow to
open files in so large a folder; without this the menus and fights stop at every spoken line). From the zip's
folder, where `F:` is the SD card:

```
python pack_gamedata.py F:\atmosphere\contents\0100B73DEC000000\gamedata --delete
```

Do not run it with `--delete` on the PC's own `gamedata`: the PC version needs that folder.

## Controls

In fights the buttons are the PS2 pad's by position:

| Switch | PS2 |
|---|---|
| B / A / Y / X | Cross / Circle / Square / Triangle |
| L / R | L1 / R1 |
| ZL / ZR | L2 / R2 |
| Left stick / right stick pressed | L3 / R3 |
| + / − | START / SELECT |

- **A Joy-Con held sideways** (one per player): SL is ZL, SR is R, the top button L and the top trigger ZR; its
  stick also moves through the menus.
- **In the menus** A chooses and B goes back; **Controls → Nintendo layout in the menus** turns that off.
- **+ and − together** open the settings window (the PC version's F1): the D-pad moves, L / R change the tab, and
  the touch screen works as a mouse. Its **Exit game** button closes the game.

## Mods

As on the PC, in the game's folder (`atmosphere/contents/0100B73DEC000000/`):

- `gamedata/mods/<same path as the original>` replaces that file of the game's data.
- `textures/` takes texture packs (PCSX2 naming), `stages/` and `songs/` stages and music from outside the disc:
  see the note in each folder, and [adding_stages.md](docs/port/adding_stages.md) /
  [adding_songs.md](docs/port/adding_songs.md).

## Troubleshooting

- **"The game's program was not found"**: the zip's `atmosphere` folder was not copied to the root of the card.
- **Not enough memory, or the game does not start from the album**: open the homebrew menu through a game, holding R.
- **Fights are slow**: sys-clk is not installed or not running; `bt3_log.txt` says `bt3: clocks: sys-clk is there`
  when it is.
- **The menus stop at every voice line**: the voice files were not packed (see [Game files](#game-files)).
- **The game stops or closes**: the game's folder has `bt3_log.txt`, `bt3_watch.txt` and, after a crash,
  `bt3_crash.txt` (the launcher shows its first lines at the next start). Attaching them to a bug report helps a lot.

## Notes

- Saves and settings are in the game's folder: `saves/` (the PC version's format: copy them between PC and Switch)
  and `bt3_settings.txt`.
- With sys-clk installed the game sets CPU 1785 MHz and memory 1600 MHz, and the GPU at 921 MHz docked or charging,
  614 MHz on the battery, only while it is in front; the HOME menu and closing give the console its own clocks back.
  **Video → Overclock while playing** turns it off.
- Updates: the settings window offers **Update now**, then **Restart now**; the game closes and is started again.
  Your saves, settings, game data and mods are never touched.

## Building

See [docs/port/switch_plan.md](docs/port/switch_plan.md). The build needs devkitPro (devkitA64, libnx and the
Switch portlibs), LLVM 22 with its Python bindings and Python 3; no game files are needed. In devkitPro's container
(`devkitpro/devkita64`), with LLVM 22 installed:

```
sh port/switch/release/build.sh .
```

It makes both zips in `port/build/`. Releases are built by the `switch-release` and `follow-upstream` workflows.

The rest of the repository is Tenkaichi3Decomp, which this fork follows release by release, with the changes the
Switch needed in the shared code, most of them inside `#ifdef __SWITCH__`. The PC build is not maintained here:
for the PC version and its README, use [upstream](https://github.com/z3xox/Tenkaichi3Decomp).

## Credits

- [z3xox](https://github.com/z3xox) and the contributors of
  [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the PC port this one is built on, and the
  authors of the BT3-Decompiled decompilation it uses
- [arielxflores](https://github.com/arielxflores)'s [PS Vita port](https://github.com/arielxflores/Tenkaichi3Decomp-Vita),
  for showing how the game's controller art is stored
- Spike, the developer of Dragon Ball Z: Budokai Tenkaichi 3
- [switchbrew](https://github.com/switchbrew) for libnx, the [devkitPro](https://devkitpro.org/) team, the
  [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) team, and
  [retronx-team](https://github.com/retronx-team) for sys-clk
- SDL, Mesa and the other libraries the port uses: their licences are in the release's `licenses/` folder.

## License

The Switch port's code (`port/switch/` and the changes for the Switch in the rest of the tree) is under the
[GNU Affero General Public License, version 3](LICENSE), the license of Tenkaichi3Decomp's port code since
October 2026. The port code it is based on, from before that change, came under the MIT license; its notice is
kept in [LICENSE-MIT](LICENSE-MIT).

The game's code in `src/` and `include/` comes from the decompilation, and the button art is redrawn from the
game's own: they belong to the game's rights holders. This project is not affiliated with or endorsed by them, or by
Nintendo, and distributes none of the game's data. You need your own legally obtained copy of the game to use it.
