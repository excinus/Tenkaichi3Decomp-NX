#!/bin/sh
# The Switch test kit of phase 3 (docs/port/switch_plan.md): the game's program for the Switch (port/build/bt3_nx,
# BT3_CC=nx64), its lists (.dat, .mem, .rel), the launcher (port/switch/launcher/build.sh) and a bt3_env.txt that runs the game's demo
# fight without a picture and logs the fight's state, with the instructions  ->  port/build/switch/test-kit/
# Needs devkitA64's strip (DEVKITPRO, default /opt/devkitpro).
cd "$(dirname "$0")/../.." || exit 1
: "${DEVKITPRO:=/opt/devkitpro}"
K=port/build/switch/test-kit
T=$K/atmosphere/contents/0100B73DEC000000
for f in port/build/bt3_nx port/build/bt3_nx.dat port/build/bt3_nx.mem port/build/bt3_nx.rel port/build/switch/Tenkaichi3Decomp.nro; do
    [ -f "$f" ] || { echo "missing $f (build the game with BT3_CC=nx64 and the launcher first)"; exit 1; }
done
rm -rf $K && mkdir -p $T $K/switch || exit 1
$DEVKITPRO/devkitA64/bin/aarch64-none-elf-strip -o $T/Tenkaichi3Decomp port/build/bt3_nx || exit 1
cp port/build/bt3_nx.dat $T/Tenkaichi3Decomp.dat && cp port/build/bt3_nx.mem $T/Tenkaichi3Decomp.mem && cp port/build/bt3_nx.rel $T/Tenkaichi3Decomp.rel || exit 1
cp port/build/switch/Tenkaichi3Decomp.nro $K/switch/ || exit 1
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
Tenkaichi3Decomp for the Nintendo Switch - test kit (phase 3 of docs/port/switch_plan.md)

This is NOT the playable game yet. It runs the game's demo fight (the computer against itself) on the Switch,
without picture or sound, and writes the fight's state at every frame. Compared with the same fight on a PC, it
shows whether the Switch computes the game exactly as the PC does.

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
echo "test kit in $K"
