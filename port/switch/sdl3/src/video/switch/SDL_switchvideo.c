/* SDL3's video driver for the Nintendo Switch (homebrew, libnx): part of Tenkaichi3Decomp's Switch port
   (docs/port/switch_plan.md), under that project's licence, NOT part of SDL. port/switch/sdl3/build.py copies it into
   SDL's source tree to build SDL for the port. SDL does not accept code written with AI tools (its CLAUDE.md), and
   this file was: it is not meant to be offered to SDL. Modelled on SDL's console drivers (video/vita) and on what
   devkitPro's open SDL2 port does on this system.

   One window, the size of the screen: 1920x1080 docked, 1280x720 in the hand (libnx's default window, resized when
   the console is docked or taken out). OpenGL through devkitPro's Mesa and SDL's EGL code, which calls EGL directly
   (SDL_VIDEO_STATIC_ANGLE: no shared libraries here). Events: libnx's applet loop at every SDL_PumpEvents (HOME,
   focus, the request to close, docking), the touch screen as a mouse (left button), and the software keyboard for
   text input. */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_SWITCH

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"
#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"

#include <switch.h>

struct SDL_WindowData
{
    EGLSurface egl_surface;
};

static SDL_Window *sWindow;
static int sDocked = -1, sFocused = 1, sTouching;

static void screen_size(int *w, int *h)
{
    int docked = appletGetOperationMode() == AppletOperationMode_Console;
    *w = docked ? 1920 : 1280;
    *h = docked ? 1080 : 720;
}

static bool SWITCH_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;

    SDL_zero(mode);
    screen_size(&mode.w, &mode.h);
    mode.refresh_rate = 60.0f;
    mode.format = SDL_PIXELFORMAT_ABGR8888;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }
    hidInitializeTouchScreen();
    return true;
}

static void SWITCH_VideoQuit(SDL_VideoDevice *_this)
{
}

static bool SWITCH_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    SDL_WindowData *data;
    int w, h;

    if (sWindow) {
        return SDL_SetError("Only one window on this system");
    }
    data = (SDL_WindowData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }
    window->internal = data;
    screen_size(&w, &h);
    nwindowSetDimensions(nwindowGetDefault(), (u32)w, (u32)h);
    sDocked = w == 1920;
    if (window->flags & SDL_WINDOW_OPENGL) {
        data->egl_surface = SDL_EGL_CreateSurface(_this, window, (NativeWindowType)nwindowGetDefault());
        if (data->egl_surface == EGL_NO_SURFACE) {
            SDL_free(data);
            window->internal = NULL;
            return SDL_SetError("Could not create the OpenGL window surface");
        }
    }
    sWindow = window;
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);
    return true;
}

static void SWITCH_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_WindowData *data = window->internal;

    if (data) {
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_MakeCurrent(_this, EGL_NO_SURFACE, NULL);
            SDL_EGL_DestroySurface(_this, data->egl_surface);
        }
        SDL_free(data);
    }
    window->internal = NULL;
    if (sWindow == window) {
        sWindow = NULL;
    }
}

static void SWITCH_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static SDL_FullscreenResult SWITCH_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display,
                                                      SDL_FullscreenOp fullscreen)
{
    return SDL_FULLSCREEN_SUCCEEDED;
}

static bool SWITCH_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    return SDL_EGL_LoadLibrary(_this, path, EGL_DEFAULT_DISPLAY, 0);
}

SDL_EGL_CreateContext_impl(SWITCH)
SDL_EGL_MakeCurrent_impl(SWITCH)
SDL_EGL_SwapWindow_impl(SWITCH)

/* The software keyboard (an applet: it takes the screen until it is closed). Its text arrives as typed text, then a
   press of Return, which is how the games' text lines take it. */
static bool SWITCH_HasScreenKeyboardSupport(SDL_VideoDevice *_this)
{
    return true;
}

static void SWITCH_ShowScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    SwkbdConfig kbd;
    char text[512];

    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return;
    }
    swkbdConfigMakePresetDefault(&kbd);
    text[0] = '\0';
    if (R_SUCCEEDED(swkbdShow(&kbd, text, sizeof(text))) && text[0] != '\0') {
        SDL_SendKeyboardText(text);
        SDL_SendKeyboardKeyAutoRelease(0, SDL_SCANCODE_RETURN);
    }
    swkbdClose(&kbd);
    SDL_SendScreenKeyboardHidden();
}

static void SWITCH_HideScreenKeyboard(SDL_VideoDevice *_this, SDL_Window *window)
{
}

/* The touch screen as the mouse: one finger moves it and holds the left button */
static void pump_touch(void)
{
    HidTouchScreenState state;
    Uint64 now = SDL_GetTicksNS();
    int w, h;

    if (!sWindow || hidGetTouchScreenStates(&state, 1) == 0) {
        return;
    }
    if (state.count > 0) {
        screen_size(&w, &h);
        /* (the touch screen's coordinates are always 1280x720) */
        SDL_SendMouseMotion(now, sWindow, SDL_TOUCH_MOUSEID, false, (float)state.touches[0].x * (float)w / 1280.0f,
                            (float)state.touches[0].y * (float)h / 720.0f);
        if (!sTouching) {
            SDL_SendMouseButton(now, sWindow, SDL_TOUCH_MOUSEID, SDL_BUTTON_LEFT, true);
            sTouching = 1;
        }
    } else if (sTouching) {
        SDL_SendMouseButton(now, sWindow, SDL_TOUCH_MOUSEID, SDL_BUTTON_LEFT, false);
        sTouching = 0;
    }
}

static void SWITCH_PumpEvents(SDL_VideoDevice *_this)
{
    int focused, w, h;

    if (!appletMainLoop()) {
        SDL_SendQuit();
        return;
    }
    focused = appletGetFocusState() == AppletFocusState_InFocus;
    if (sWindow && focused != sFocused) {
        SDL_SendWindowEvent(sWindow, focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST, 0, 0);
    }
    sFocused = focused;
    screen_size(&w, &h);
    if (sWindow && sDocked != (w == 1920)) { /* docked or taken out: the window follows the screen */
        sDocked = w == 1920;
        nwindowSetDimensions(nwindowGetDefault(), (u32)w, (u32)h);
        SDL_SendWindowEvent(sWindow, SDL_EVENT_WINDOW_RESIZED, w, h);
    }
    pump_touch();
}

static void SWITCH_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *SWITCH_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));

    if (!device) {
        return NULL;
    }
    device->free = SWITCH_DeleteDevice;
    device->VideoInit = SWITCH_VideoInit;
    device->VideoQuit = SWITCH_VideoQuit;
    device->CreateSDLWindow = SWITCH_CreateWindow;
    device->DestroyWindow = SWITCH_DestroyWindow;
    device->SetWindowSize = SWITCH_SetWindowSize;
    device->SetWindowFullscreen = SWITCH_SetWindowFullscreen;
    device->PumpEvents = SWITCH_PumpEvents;
    device->GL_LoadLibrary = SWITCH_GL_LoadLibrary;
    device->GL_GetProcAddress = SDL_EGL_GetProcAddressInternal;
    device->GL_UnloadLibrary = SDL_EGL_UnloadLibrary;
    device->GL_CreateContext = SWITCH_GLES_CreateContext;
    device->GL_MakeCurrent = SWITCH_GLES_MakeCurrent;
    device->GL_SwapWindow = SWITCH_GLES_SwapWindow;
    device->GL_SetSwapInterval = SDL_EGL_SetSwapInterval;
    device->GL_GetSwapInterval = SDL_EGL_GetSwapInterval;
    device->GL_DestroyContext = SDL_EGL_DestroyContext;
    device->HasScreenKeyboardSupport = SWITCH_HasScreenKeyboardSupport;
    device->ShowScreenKeyboard = SWITCH_ShowScreenKeyboard;
    device->HideScreenKeyboard = SWITCH_HideScreenKeyboard;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap SWITCH_bootstrap = {
    "switch",
    "Nintendo Switch video driver (Tenkaichi3Decomp)",
    SWITCH_CreateDevice,
    NULL, // no message boxes
    false
};

#endif // SDL_VIDEO_DRIVER_SWITCH
