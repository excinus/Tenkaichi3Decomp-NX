#!/bin/sh
# SDL3's headers for the Switch build (BT3_CC=nx64), the version the PC release is built with
# (port/release/Dockerfile): unpacked to port/build/nx/SDL3-<version>/include, where port/tools/toolchain.py finds them.
# Only the headers: SDL3 has no public Switch port, the functions the port uses are provided for the Switch by the
# port itself (docs/port/switch_plan.md, phase 4). Needs curl and tar.
cd "$(dirname "$0")/../.." || exit 1
V=${SDL_VERSION:-3.4.12}
D=port/build/nx/SDL3-$V
[ -f "$D/include/SDL3/SDL.h" ] && exit 0
mkdir -p port/build/nx || exit 1
curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL3-$V.tar.gz" | tar -xz -C port/build/nx "SDL3-$V/include" || exit 1
echo "SDL3 $V headers in $D/include"
