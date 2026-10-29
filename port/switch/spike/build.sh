#!/bin/sh
# Builds the Switch spike (port/switch/spike/source; docs/port/switch_plan.md, phase 1)  ->  port/build/switch/spike.nro
#
#   port/switch/spike/build.sh            in devkitPro's container (needs docker; pulls devkitpro/devkita64)
#   port/switch/spike/build.sh --here     with devkitA64 and libnx installed here (DEVKITPRO set; packages
#                                         switch-dev, switch-mesa, switch-glad)
# Copy spike.nro to sdmc:/switch/ and start it from the homebrew menu, best through a game (hold R while starting
# one). It writes its results to sdmc:/switch/bt3_spike.txt.
cd "$(dirname "$0")/../../.." || exit 1
if [ "$1" != --here ]; then
    exec docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/src -w /src devkitpro/devkita64 sh port/switch/spike/build.sh --here
fi
: "${DEVKITPRO:=/opt/devkitpro}"
O=port/build/switch/spike
mkdir -p $O || exit 1
CC=$DEVKITPRO/devkitA64/bin/aarch64-none-elf-gcc
CXX=$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++ # (the link: Mesa is C++)
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE"
CFLAGS="-O2 -g -Wall -ffunction-sections $ARCH -D__SWITCH__ -I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include -Iinclude -I$O"
python3 port/switch/spike/gen_shaders.py $O/spike_shaders.h || exit 1
for f in port/switch/spike/source/main.c port/switch/spike/source/bench.c port/src/softfloat_ps2.c; do
    $CC $CFLAGS -Iport/switch/spike/source -c "$f" -o $O/$(basename "$f" .c).o || exit 1
done
$CXX -specs=$DEVKITPRO/libnx/switch.specs $ARCH -Wl,--gc-sections -o $O/spike.elf $O/main.o $O/bench.o $O/softfloat_ps2.o \
    -L$DEVKITPRO/portlibs/switch/lib -L$DEVKITPRO/libnx/lib -lglad -lEGL -lglapi -ldrm_nouveau -lnx || exit 1
$DEVKITPRO/tools/bin/nacptool --create "BT3 Switch spike" "Tenkaichi3Decomp" "1" $O/spike.nacp || exit 1
$DEVKITPRO/tools/bin/elf2nro $O/spike.elf port/build/switch/spike.nro --nacp=$O/spike.nacp || exit 1
echo "built port/build/switch/spike.nro"
