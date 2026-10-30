#!/bin/sh
# Builds the Switch setup (port/switch/setup: setup.cpp, work.cpp, and the PC setup's native.cpp in it; docs/port/
# switch_plan.md, "The setup"), with SDL3 linked in (built here as a static library), and puts the release's SD zip
# at its end: one program to put anywhere.
#
#   port/switch/setup/build.sh win <SD zip>  -> port/build/Tenkaichi3Decomp-switch-setup.exe   (mingw-w64, posix threads)
#   port/switch/setup/build.sh [<SD zip>]    -> port/build/Tenkaichi3Decomp-switch-setup        (this Linux, for tests)
#
# Needs cmake and the compiler; SDL's source is the one port/switch/sdl3/build.py downloads (port/build/nx/
# SDL3-<version>.tar.gz), fetched here when it is missing. The zip: port/build/Tenkaichi3Decomp-switch-SD.zip
# (BT3_CC=nx64 port/tools/package.py), followed by a trailer: "BT3NXSD1" and its size (8 bytes, little-endian).
set -e
cd "$(dirname "$0")/../../.."
V=${SDL_VERSION:-3.4.12}
I=port/third_party/imgui
if [ "$1" = win ]; then
    T=win; shift
    CXX=$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++)
    CC=$(command -v x86_64-w64-mingw32-gcc-posix || command -v x86_64-w64-mingw32-gcc)
    OUT=port/build/Tenkaichi3Decomp-switch-setup.exe
    CMAKE_SYS="-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres"
else
    T=linux
    CXX=g++; CC=gcc
    OUT=port/build/Tenkaichi3Decomp-switch-setup
    CMAKE_SYS="-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF"  # (for tests: --install, no window needed)
fi
ZIP=${1:-port/build/Tenkaichi3Decomp-switch-SD.zip}
W=port/build/switch-setup-$T
S=$W/sdl
mkdir -p $W

# SDL3, static
if [ ! -f $S/lib/libSDL3.a ]; then
    TGZ=port/build/nx/SDL3-$V.tar.gz
    if [ ! -f $TGZ ]; then
        mkdir -p port/build/nx
        curl -fsSL -o $TGZ https://github.com/libsdl-org/SDL/releases/download/release-$V/SDL3-$V.tar.gz
    fi
    rm -rf $W/sdl-src $W/sdl-build
    mkdir -p $W/sdl-src
    tar -xzf $TGZ -C $W/sdl-src --strip-components=1
    cmake -S $W/sdl-src -B $W/sdl-build $CMAKE_SYS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/$S" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF > $W/sdl-cmake.log
    cmake --build $W/sdl-build --parallel 8 > $W/sdl-build.log
    cmake --install $W/sdl-build > /dev/null
fi
# (what the static library needs: on Windows SDL's sdl3.pc lists it with -lSDL3 itself, elsewhere under Libs.private)
SDLLIBS=$(sed -n -e 's/^Libs.private: *//p' -e 's/^Libs: *//p' $S/lib/pkgconfig/sdl3.pc | sed 's/-L[^ ]*//g; s/-lSDL3//g' | tr '\n' ' ')

# the setup
for f in $I/imgui.cpp $I/imgui_draw.cpp $I/imgui_tables.cpp $I/imgui_widgets.cpp $I/imgui_impl_sdl3.cpp $I/imgui_impl_sdlgpu3.cpp \
         $I/imgui_impl_opengl3.cpp port/switch/setup/setup.cpp port/switch/setup/work.cpp; do
    o=$W/$(basename "$f" .cpp).o
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ port/setup/native.cpp -nt "$o" ] || [ port/switch/setup/work.h -nt "$o" ]; then
        $CXX -std=c++17 -O2 -w -I$I -I$S/include -c "$f" -o "$o"
    fi
done
if [ $T = win ]; then
    $CXX -o $W/setup.exe $W/*.o -L$S/lib -lSDL3 $SDLLIBS -static -static-libgcc -static-libstdc++ -mwindows
else
    $CXX -o $W/setup $W/*.o -L$S/lib -lSDL3 $SDLLIBS -lpthread -ldl
fi

# the release's files at its end
if [ ! -f "$ZIP" ]; then
    echo "no $ZIP: build the SD zip first (BT3_CC=nx64 port/tools/package.py)"; exit 1
fi
cp $W/setup$( [ $T = win ] && echo .exe ) $OUT
cat "$ZIP" >> $OUT
python3 -c "import struct,sys,os; sys.stdout.buffer.write(b'BT3NXSD1' + struct.pack('<Q', os.path.getsize(sys.argv[1])))" "$ZIP" >> $OUT
echo "built $OUT ($(du -h $OUT | cut -f1), with $(basename $ZIP))"
