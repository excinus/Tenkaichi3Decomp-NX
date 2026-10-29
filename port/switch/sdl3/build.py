#!/usr/bin/env python3
"""SDL3 for the Nintendo Switch build (BT3_CC=nx64, docs/port/switch_plan.md): SDL's own source, the version the PC
release uses (port/release/Dockerfile), built as a static library with devkitPro's toolchain and the Switch platform
of this folder (switch.cmake, src/: added to SDL's source tree, and one line in its CMakeLists.txt that includes
switch.cmake among the other consoles). SDL3 has no public Switch port; this is it, for what the game uses.

    port/switch/sdl3/build.py      -> port/build/nx/SDL3-<version>-switch/{include,lib/libSDL3.a}
                                      (toolchain.py finds it there; needs DEVKITPRO, cmake, ninja, curl or the tarball)
SDL is under the zlib licence (its LICENSE.txt is installed with it). The files of this folder are Tenkaichi3Decomp's,
not SDL's, and are not meant to be offered to SDL: SDL does not accept code written with AI tools (its CLAUDE.md),
and these were."""
import os, pathlib, shutil, subprocess, sys, tarfile, urllib.request

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
VERSION = os.environ.get("SDL_VERSION", "3.4.12")
WORK = ROOT / "port/build/nx"
SRC = WORK / f"SDL3-{VERSION}-src"
BUILD = WORK / f"SDL3-{VERSION}-build"
PREFIX = WORK / f"SDL3-{VERSION}-switch"
DEVKITPRO = pathlib.Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
# The block's place: before the 3DS's, in the chain of the consoles' elseif()s
ANCHOR = "elseif(N3DS)\n"
HOOK = 'elseif(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")\n  include("${SDL3_SOURCE_DIR}/switch.cmake")\n'

def fetch():
    if (SRC / "CMakeLists.txt").exists():
        return
    WORK.mkdir(parents=True, exist_ok=True)
    tgz = WORK / f"SDL3-{VERSION}.tar.gz"
    if not tgz.exists():
        url = f"https://github.com/libsdl-org/SDL/releases/download/release-{VERSION}/SDL3-{VERSION}.tar.gz"
        print("downloading", url)
        with urllib.request.urlopen(url) as r, open(tgz, "wb") as f:
            shutil.copyfileobj(r, f)
    tmp = WORK / f"SDL3-{VERSION}-unpack"
    shutil.rmtree(tmp, ignore_errors=True)
    with tarfile.open(tgz) as t:
        try:
            t.extractall(tmp, filter="data")
        except TypeError:  # (Python before 3.11.4 / 3.12: no filter; SDL's own release archive)
            t.extractall(tmp)
    (tmp / f"SDL3-{VERSION}").rename(SRC)
    tmp.rmdir()

# SDL's dynamic API (a program's SDL replaced by another copy at run time) has no use without shared libraries; SDL
# turns it off per platform in this header and wants it edited for that
DYNAPI = "src/dynapi/SDL_dynapi.h"
DYNAPI_ANCHOR = "#if defined(SDL_PLATFORM_PRIVATE) // probably not useful on private platforms.\n#define SDL_DYNAMIC_API 0\n"
DYNAPI_HOOK = "#elif defined(__SWITCH__) // Tenkaichi3Decomp's Switch build (port/switch/sdl3): no shared libraries\n#define SDL_DYNAMIC_API 0\n"

# The Switch's drivers in SDL's lists of drivers (each list and its declarations: the entry before the 3DS's)
REGISTER = [
    ("src/video/SDL_video.c", "#ifdef SDL_VIDEO_DRIVER_N3DS\n    &N3DS_bootstrap,\n",
     "#ifdef SDL_VIDEO_DRIVER_SWITCH\n    &SWITCH_bootstrap,\n#endif\n"),
    ("src/video/SDL_sysvideo.h", "extern VideoBootStrap N3DS_bootstrap;\n", "extern VideoBootStrap SWITCH_bootstrap;\n"),
    ("src/audio/SDL_audio.c", "#ifdef SDL_AUDIO_DRIVER_N3DS\n    &N3DSAUDIO_bootstrap,\n",
     "#ifdef SDL_AUDIO_DRIVER_SWITCH\n    &SWITCHAUDIO_bootstrap,\n#endif\n"),
    ("src/audio/SDL_sysaudio.h", "extern AudioBootStrap N3DSAUDIO_bootstrap;\n", "extern AudioBootStrap SWITCHAUDIO_bootstrap;\n"),
    ("src/joystick/SDL_joystick.c", "#ifdef SDL_JOYSTICK_N3DS\n    &SDL_N3DS_JoystickDriver,\n",
     "#ifdef SDL_JOYSTICK_SWITCH\n    &SDL_SWITCH_JoystickDriver,\n#endif\n"),
    ("src/joystick/SDL_sysjoystick.h", "extern SDL_JoystickDriver SDL_N3DS_JoystickDriver;\n",
     "extern SDL_JoystickDriver SDL_SWITCH_JoystickDriver;\n"),
]

def patch():
    """Our Switch files into SDL's tree, and SDL's own files edited: the Switch named in CMakeLists.txt and in the lists
    of drivers, the dynamic API off (the build's only changes to them)."""
    shutil.copy(HERE / "switch.cmake", SRC / "switch.cmake")
    shutil.copytree(HERE / "src", SRC / "src", dirs_exist_ok=True)
    cm = SRC / "CMakeLists.txt"
    text = cm.read_text()
    if HOOK not in text:
        if text.count(ANCHOR) != 1:
            sys.exit(f"SDL {VERSION}: its CMakeLists.txt has no single '{ANCHOR.strip()}' to put the Switch before")
        cm.write_text(text.replace(ANCHOR, HOOK + ANCHOR))
    dh = SRC / DYNAPI
    text = dh.read_text()
    if DYNAPI_HOOK not in text:
        if text.count(DYNAPI_ANCHOR) != 1:
            sys.exit(f"SDL {VERSION}: {DYNAPI} has not the lines this build adds the Switch after")
        dh.write_text(text.replace(DYNAPI_ANCHOR, DYNAPI_ANCHOR + DYNAPI_HOOK))
    for rel, anchor, entry in REGISTER:
        f = SRC / rel
        text = f.read_text()
        if entry not in text:
            if text.count(anchor) != 1:
                sys.exit(f"SDL {VERSION}: {rel} has not the one place this build adds the Switch's driver before")
            f.write_text(text.replace(anchor, entry + anchor))

def build():
    env = dict(os.environ, DEVKITPRO=str(DEVKITPRO),
               PATH=f"{DEVKITPRO}/tools/bin:{DEVKITPRO}/devkitA64/bin:" + os.environ.get("PATH", ""))
    off = ["GPU", "RENDER", "CAMERA", "HIDAPI", "DIALOG", "SENSOR", "POWER", "TRAY", "HAPTIC", "VULKAN", "OPENGL",
           "OPENGLES", "SHARED", "TESTS", "EXAMPLES", "TEST_LIBRARY"]
    subprocess.run(["cmake", "-S", str(SRC), "-B", str(BUILD), "-G", "Ninja", f"-DCMAKE_TOOLCHAIN_FILE={DEVKITPRO}/cmake/Switch.cmake",
                    "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={PREFIX}", "-DSDL_STATIC=ON",
                    # libnx's sysctlbyname is the BSD sockets service's (socket.o), which only answers once the
                    # program has started that service: SDL_GetNumLogicalCPUCores asked it "hw.ncpu" and never came
                    # back (the game stood still at its first large texture, docs/port/switch_plan.md, "Results")
                    "-DHAVE_SYSCTLBYNAME=OFF"] +
                   [f"-DSDL_{o}=OFF" for o in off], check=True, env=env)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, env=env)
    subprocess.run(["cmake", "--install", str(BUILD)], check=True, env=env, stdout=subprocess.DEVNULL)
    print("SDL3", VERSION, "for the Switch in", PREFIX.relative_to(ROOT))

fetch()
patch()
build()
