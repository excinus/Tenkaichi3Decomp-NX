#!/bin/sh
# Builds a Switch release from a source tree (docs/port/switch_plan.md, phase 5): SDL3 for the Switch, the game
# (BT3_CC=nx64, BT3_SKELETON=1: no disc involved), the launcher, and the release's zip package.py makes:
#   <tree>/port/build/Tenkaichi3Decomp-switch.zip      the Switch's files as the SD card has them (port/switch/setup/
#                                                      build.sh then makes each PC system's zip from it)
#
#   sh port/switch/release/build.sh <tree>
#
# BT3_VERSION (the release's tag: v0.1.39.2-nx1) and BT3_UPDATE_KEY (the public half of the signing key) are passed
# through. Only commands every tree has are used, so that this file can build an older release's tree as well (the
# comparison of .github/workflows/follow-upstream.yml). In devkitPro's container, with LLVM 22 (as switch.yml).
set -e
cd "$1"
export BT3_CC=nx64 BT3_SKELETON=1 BT3_NX_LAUNCHER=1
mkdir -p port/build
python3 port/switch/sdl3/build.py
if ! python3 port/tools/undefined.py > port/build/compile_nx64.txt 2>&1; then tail -40 port/build/compile_nx64.txt; exit 1; fi
tail -3 port/build/compile_nx64.txt
if grep -q "^FAILED" port/build/compile_nx64.txt; then grep -A12 "^FAILED" port/build/compile_nx64.txt | head -80; exit 1; fi
python3 port/tools/link.py 2>&1 | tee port/build/link_nx64.txt
test -f port/build/bt3_nx
sh port/switch/launcher/build.sh --here
python3 port/tools/package.py
test -f port/build/Tenkaichi3Decomp-switch.zip
