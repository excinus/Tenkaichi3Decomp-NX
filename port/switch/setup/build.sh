#!/bin/sh
# Builds the Switch setup (port/switch/setup: setup.cpp, work.cpp, and the PC setup's native.cpp in it; docs/port/
# switch_plan.md, "The setup") with SDL3 linked in (built here as a static library), then the release's zip for that
# system (zip.py): the setup beside the Switch's files.
#
#   port/switch/setup/build.sh win     -> port/build/Tenkaichi3Decomp-switch-windows.zip   (mingw-w64, posix threads)
#   port/switch/setup/build.sh linux   -> port/build/Tenkaichi3Decomp-switch-linux.zip     (X11/Wayland headers needed)
#   port/switch/setup/build.sh mac     -> port/build/Tenkaichi3Decomp-switch-macos.zip     (on a Mac: Xcode's clang,
#                                         arm64 and x86_64 in one program)
#   port/switch/setup/build.sh test    -> port/build/switch-setup-test/setup  (this Linux, no window: --install only)
#
# Needs cmake and the compiler; SDL's source is the one port/switch/sdl3/build.py downloads (port/build/nx/
# SDL3-<version>.tar.gz), fetched here when it is missing. The Switch's files: port/build/Tenkaichi3Decomp-switch.zip
# (BT3_CC=nx64 port/tools/package.py).
set -e
cd "$(dirname "$0")/../../.."
V=${SDL_VERSION:-3.4.12}
I=port/third_party/imgui
T=${1:-test}
EXE=setup
FLAGS=""
case $T in
win)
    CXX=$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++)
    CC=$(command -v x86_64-w64-mingw32-gcc-posix || command -v x86_64-w64-mingw32-gcc)
    EXE=setup.exe
    CMAKE_SYS="-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres"
    LINK="-static -static-libgcc -static-libstdc++ -mwindows" ;;
linux)
    CXX=g++; CC=gcc
    CMAKE_SYS=""
    LINK="-static-libgcc -static-libstdc++ -lpthread -ldl" ;;
mac)
    CXX=clang++; CC=clang
    FLAGS="-arch arm64 -arch x86_64 -mmacosx-version-min=11.0"
    CMAKE_SYS="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"
    LINK="" ;;
test)
    CXX=g++; CC=gcc
    CMAKE_SYS="-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF"
    LINK="-lpthread -ldl" ;;
*)
    echo "usage: $0 win|linux|mac|test"; exit 2 ;;
esac
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
        $CXX -std=c++17 -O2 -w $FLAGS -I$I -I$S/include -c "$f" -o "$o"
    fi
done
$CXX $FLAGS -o $W/$EXE $W/*.o -L$S/lib -lSDL3 $SDLLIBS $LINK
echo "built $W/$EXE"
[ $T = test ] || python3 port/switch/setup/zip.py $T $W/$EXE
