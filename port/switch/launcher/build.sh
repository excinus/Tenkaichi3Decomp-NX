#!/bin/sh
# Builds the Switch launcher (port/switch/launcher; docs/port/switch_plan.md, "The launcher")
#   -> port/build/switch/Tenkaichi3Decomp.nro, to start from the homebrew menu
#
#   port/switch/launcher/build.sh            in devkitPro's container (needs docker; pulls devkitpro/devkita64)
#   port/switch/launcher/build.sh --here     with devkitA64 and libnx installed here (DEVKITPRO set)
cd "$(dirname "$0")/../../.." || exit 1
if [ "$1" != --here ]; then
    exec docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src devkitpro/devkita64 sh port/switch/launcher/build.sh --here
fi
: "${DEVKITPRO:=/opt/devkitpro}"
O=port/build/switch/launcher
mkdir -p $O || exit 1
CC=$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
for f in port/switch/launcher/source/main.c port/switch/launcher/source/trampoline.s; do
    $CC -O2 -g -Wall -ffunction-sections $ARCH -D__SWITCH__ -I$DEVKITPRO/libnx/include -c "$f" -o $O/$(basename "$f").o || exit 1
done
$CC -specs=$DEVKITPRO/libnx/switch.specs $ARCH -o $O/launcher.elf $O/main.c.o $O/trampoline.s.o -L$DEVKITPRO/libnx/lib -lnx || exit 1
# (the icon: the game's cover, 256 x 256, port/switch/launcher/icon.jpg, as the homebrew menu shows it)
$DEVKITPRO/tools/bin/nacptool --create "Dragon Ball Z: Budokai Tenkaichi 3" "Tenkaichi3Decomp" "phase 4" $O/launcher.nacp || exit 1
$DEVKITPRO/tools/bin/elf2nro $O/launcher.elf port/build/switch/Tenkaichi3Decomp.nro --nacp=$O/launcher.nacp \
    --icon=port/switch/launcher/icon.jpg || exit 1
echo "built port/build/switch/Tenkaichi3Decomp.nro"
