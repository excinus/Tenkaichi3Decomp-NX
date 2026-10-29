#!/bin/sh
# A Switch test kit (docs/port/switch_plan.md): the game's folder for the Switch (BT3_CC=nx64 port/tools/package.py:
# the program, its lists, the release folder's files), the launcher (port/switch/launcher/build.sh), a bt3_env.txt
# and the instructions
#   -> port/build/switch/test-kit/
#   port/switch/test_kit.sh         the game as it is played (phase 4): bt3_env.txt with comments only
#   port/switch/test_kit.sh demo    phase 3's test: the demo fight without a picture, its state logged to compare
cd "$(dirname "$0")/../.." || exit 1
K=port/build/switch/test-kit
T=$K/atmosphere/contents/0100B73DEC000000
R=port/build/release-switch  # the game's folder, as the PC release's (BT3_CC=nx64 port/tools/package.py)
for f in $R/Tenkaichi3Decomp $R/Tenkaichi3Decomp.rel port/build/switch/Tenkaichi3Decomp.nro; do
    [ -f "$f" ] || { echo "missing $f (build the game with BT3_CC=nx64, package.py, and the launcher first)"; exit 1; }
done
rm -rf $K && mkdir -p $T $K/switch || exit 1
cp -R $R/. $T/ || exit 1
cp port/build/switch/Tenkaichi3Decomp.nro $K/switch/ || exit 1
if [ "$1" = demo ]; then
cat > $T/bt3_env.txt <<'ENV'
# Settings for this test (port/src/plat_mem.c, nx_start): the game's demo fight without a picture, as the PC
# setup's self-test runs it, with the fight's state written to bt3_hash.txt at every vertical blank.
BT3_DEMO=1
BT3_GS=none
BT3_SETTINGS=selftest_settings.txt
BT3_SAVES=selftest_saves
BT3_HASH=bt3_hash.txt
ENV
cat > $K/README.txt <<'TXT'
Tenkaichi3Decomp for the Nintendo Switch - test kit: the demo fight without a picture (phase 3 of
docs/port/switch_plan.md)

It runs the game's demo fight (the computer against itself) on the Switch, without picture or sound, and writes the
fight's state at every frame. Compared with the same fight on a PC, it shows whether the Switch computes the game
exactly as the PC does.

What you need: a Switch with Atmosphere, and your PC installation of Tenkaichi3Decomp (its gamedata folder, which
its setup made from your disc).

1. Copy this kit's folders to the root of the SD card: atmosphere/ and switch/ (they merge with what is there).
2. Copy the gamedata folder of your PC installation into  atmosphere/contents/0100B73DEC000000/  on the card,
   next to Tenkaichi3Decomp (so that atmosphere/contents/0100B73DEC000000/gamedata/disc/SLUS_216.78 exists).
   Its files are large: the card needs exFAT if any file is over 4 GB.
3. On the Switch, start the homebrew menu THROUGH A GAME (hold R while starting any game; from the album there is
   not enough memory), then "Tenkaichi3Decomp".
4. The launcher says where it loaded the game, then the screen goes black while the fight runs (a minute or more).
   Then the launcher shows how it ended and the last lines of the log. A good run ends with "battle finished".
5. Send back from atmosphere/contents/0100B73DEC000000/:  bt3_log.txt, bt3_hash.txt, and bt3_crash.txt if there is one.

The same fight on the PC, for comparison (Windows: a command prompt in your PC installation's folder):
    set BT3_DEMO=1
    set BT3_GS=none
    set BT3_SETTINGS=selftest_settings.txt
    set BT3_SAVES=selftest_saves
    set BT3_HASH=bt3_hash_pc.txt
    Tenkaichi3Decomp.exe
(Linux: the same variables before ./Tenkaichi3Decomp.) It takes a few seconds, prints nothing on Windows (see
bt3_log.txt), and writes bt3_hash_pc.txt: send that too. Then
    python3 port/tools/compare_hash.py bt3_hash_pc.txt bt3_hash.txt --fight
says whether the fight is the same on both.
TXT
else
cat > $T/bt3_env.txt <<'ENV'
# Settings for tests (port/src/plat_mem.c, nx_start): NAME=value lines, as the environment variables of the PC
# builds. None are needed to play. (Phase 3's demo fight was: BT3_DEMO=1 and BT3_GS=none.)
# Without sound: remove the # in front of the next line.
#BT3_NOSOUND=1
# Textures decoded on the game's own thread only, without helper threads: remove the # in front of the next line.
#BT3_TEX_THREADS=0
ENV
cat > $K/README.txt <<'TXT'
Tenkaichi3Decomp for the Nintendo Switch - test kit: the game (phase 4 of docs/port/switch_plan.md)

The first build of the game on the Switch with picture, sound and controllers. EXPERIMENTAL: it has never run
before; what does not work yet is what this test is for.

What you need: a Switch with Atmosphere, and your PC installation of Tenkaichi3Decomp (its gamedata folder, which
its setup made from your disc). If you ran an earlier kit, the gamedata folder is already on the card.

1. Copy this kit's folders to the root of the SD card: atmosphere/ and switch/ (they merge with what is there;
   replace the files of an earlier kit, bt3_env.txt included).
2. If it is not there yet: copy the gamedata folder of your PC installation into
   atmosphere/contents/0100B73DEC000000/  on the card, next to Tenkaichi3Decomp.
3. On the Switch, start the homebrew menu THROUGH A GAME (hold R while starting any game), then "Tenkaichi3Decomp".
   The launcher shows a few lines while it loads the game (it says how long that took), then the screen is black
   while the game reads its data.
4. Play. The controllers work as a PS2 pad by position (B is Cross, A is Circle, Y is Square, X is Triangle;
   L/R and ZL/ZR are L1/R1 and L2/R2). + and - pressed together open the settings window (what the PC opens with
   its F1 key): the D-pad moves, B (Cross) selects, A (Circle) goes back, L/R change the tab, and the touch screen
   works as a mouse there. Its "Exit game" button ends the game; its Controls tab changes the buttons.
5. Tell what works and what does not (picture, sound, controllers, menus, a fight, its speed), and send from
   atmosphere/contents/0100B73DEC000000/:  bt3_log.txt, bt3_watch.txt, and bt3_crash.txt if there is one (each
   start keeps the one before as bt3_log_prev.txt and bt3_watch_prev.txt: send those too if the game was started
   again since). After a crash the launcher shows the report's first lines at its next start.
6. If the game stops (a black screen, a picture that no longer moves): wait a minute, close it with HOME, and send
   bt3_watch.txt and bt3_log.txt: bt3_watch.txt is written every 2 seconds by a part of the program that watches
   the rest (what it was doing, how it used the SD card). If it closes by itself or the console shows an error:
   send bt3_crash.txt if there is one, the newest file in atmosphere/crash_reports/ on the card if there is one,
   and a photo of the error.

Saves are in saves/ in that folder, as on the PC (copy them between PC and Switch). Online play is not ready yet.
TXT
fi
echo "test kit in $K"
