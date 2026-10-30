# The Nintendo Switch in SDL3's build (port/switch/sdl3/build.py puts this file into SDL's source and includes it from
# CMakeLists.txt where the other consoles' blocks are, for CMAKE_SYSTEM_NAME NintendoSwitch: devkitPro's Switch.cmake).
# Part of Tenkaichi3Decomp (docs/port/switch_plan.md), not of SDL, and not meant to be offered to SDL (SDL does not
# accept code written with AI tools, which this was). libnx gives newlib POSIX threads, clocks and files, so SDL's own
# POSIX back ends are used for those; the file system's base path is the program's folder (filesystem/switch). Video
# (OpenGL through devkitPro's Mesa), sound and controllers are the Switch drivers of this folder (video/switch,
# audio/switch, joystick/switch), which build.py also adds to SDL's lists of drivers.

# (POSIX calls newlib and libnx do not have here, for the pthread back end: core/switch/SDL_switch_posix.h)
sdl_glob_sources("${SDL3_SOURCE_DIR}/src/core/switch/*.c")
sdl_compile_options(PRIVATE "-include" "${SDL3_SOURCE_DIR}/src/core/switch/SDL_switch_posix.h")

set(SDL_THREAD_PTHREAD 1)
set(SDL_THREAD_PTHREAD_RECURSIVE_MUTEX 1)
sdl_glob_sources(
  "${SDL3_SOURCE_DIR}/src/thread/pthread/*.c"
  "${SDL3_SOURCE_DIR}/src/thread/pthread/*.h"
)
set(HAVE_SDL_THREADS TRUE)

set(SDL_TIMER_UNIX 1)
sdl_glob_sources("${SDL3_SOURCE_DIR}/src/timer/unix/*.c")
set(HAVE_SDL_TIMERS TRUE)

set(SDL_TIME_UNIX 1)
sdl_glob_sources("${SDL3_SOURCE_DIR}/src/time/unix/*.c")
# newlib's struct tm has no tm_gmtoff: the file's other way (Solaris's, the offset from `timezone`, newlib's _timezone)
set_source_files_properties("${SDL3_SOURCE_DIR}/src/time/unix/SDL_systime.c" PROPERTIES COMPILE_DEFINITIONS "__sun=1;timezone=_timezone")
set(HAVE_SDL_TIME TRUE)

sdl_glob_sources("${SDL3_SOURCE_DIR}/src/filesystem/switch/*.c")
set(HAVE_SDL_FILESYSTEM TRUE)

set(SDL_FSOPS_POSIX 1)
sdl_sources("${SDL3_SOURCE_DIR}/src/filesystem/posix/SDL_sysfsops.c")
set(HAVE_SDL_FSOPS TRUE)

# The Switch's drivers. OpenGL through EGL (devkitPro's Mesa), whose functions SDL's EGL code calls directly
# (SDL_VIDEO_STATIC_ANGLE: it would load EGL as a shared library otherwise); Khronos' EGL types as plain pointers,
# which is what Mesa has for the Switch.
set(SDL_VIDEO_OPENGL 1)
set(SDL_VIDEO_OPENGL_EGL 1)
set(SDL_VIDEO_STATIC_ANGLE 1)
sdl_compile_definitions(PRIVATE "SDL_VIDEO_DRIVER_SWITCH=1" "SDL_AUDIO_DRIVER_SWITCH=1" "SDL_JOYSTICK_SWITCH=1"
                        "EGL_NO_PLATFORM_SPECIFIC_TYPES=1")
sdl_glob_sources(
  "${SDL3_SOURCE_DIR}/src/video/switch/*.c"
  "${SDL3_SOURCE_DIR}/src/audio/switch/*.c"
  "${SDL3_SOURCE_DIR}/src/joystick/switch/*.c"
)
set(HAVE_SDL_VIDEO TRUE)
set(HAVE_SDL_AUDIO TRUE)
set(HAVE_SDL_JOYSTICK TRUE)
