/* SDL3's joystick driver for the Nintendo Switch (homebrew, libnx): part of Tenkaichi3Decomp's Switch port
   (docs/port/switch_plan.md), under that project's licence, NOT part of SDL. port/switch/sdl3/build.py copies it into
   SDL's source tree to build SDL for the port. SDL does not accept code written with AI tools (its CLAUDE.md), and
   this file was: it is not meant to be offered to SDL. Modelled on SDL's console drivers (joystick/n3ds).

   Up to MAX_PADS players through libnx's pad API: player 1 is the first controller or the console in the hand
   (handheld), the others the next controllers (a Pro Controller, a pair of Joy-Con). Each is a gamepad for SDL, its
   buttons by position: the bottom face button (B) is SDL's south, the right one (A) east, as SDL names them on
   every controller; + is start, - is back. ZL and ZR are the triggers (digital). Rumble through hid's vibration.

   A single Joy-Con held sideways (two players on one pair, after the console's Controllers screen: the game's
   port/src/plat_nxpad.c) is turned here into the same pad: libnx reports its buttons by their names and its stick as
   held upright, so the four buttons under the thumb are read by where they are when it lies sideways (the bottom one
   is B, the right one A...), the stick turned with it, SL is ZL and SR is R (the game's Charge Aura and Ascend under
   the fingers), its shoulder button is L and its trigger ZR, and its one of - and + is +: the player's choice,
   2026-10-30. Should the system turn it already (a left one giving A, B, X or Y), what it gives is taken as it is.
   When a slot's controller changes kind (a pair split in two by the Controllers screen, joined again) it is taken
   off and put back as a new controller, so that SDL and the game see its name ("..., sideways") and buttons anew. */
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_SWITCH

#include "../SDL_sysjoystick.h"

#include <stdio.h> /* (the game's log: stderr) */
#include <switch.h>

#define MAX_PADS 4
#define NUM_BUTTONS 16 /* libnx's HidNpadButton bits 0..15: A B X Y, sticks, L R ZL ZR, + -, left up right down */

typedef struct
{
    PadState pad;
    SDL_JoystickID id; /* 0 while nothing is connected there */
    u32 style;         /* the kind of controller it was put in as (padGetStyleSet) */
    HidVibrationDeviceHandle vibration[2];
    int vibrationReady;
} Slot;

static Slot sSlot[MAX_PADS];
static int sSystemTurns; /* a left Joy-Con alone gave A, B, X or Y: the system reports sideways Joy-Con turned itself */

/* One Joy-Con alone (left or right), not a pair, a Pro Controller or the console in the hand: 1 left, 2 right, 0 not. */
static int single_of(u32 style)
{
    if (style & (HidNpadStyleTag_NpadFullKey | HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)) {
        return 0;
    }
    return (style & HidNpadStyleTag_NpadJoyLeft) ? 1 : (style & HidNpadStyleTag_NpadJoyRight) ? 2 : 0;
}

#define MOVE(from, to) ((raw >> (from)) & 1 ? 1ull << (to) : 0)

static void slot_init(int i)
{
    if (i == 0) {
        padInitialize(&sSlot[0].pad, HidNpadIdType_No1, HidNpadIdType_Handheld);
    } else {
        padInitializeWithMask(&sSlot[i].pad, 1ull << (HidNpadIdType_No1 + i));
    }
}

static int slot_of_device(int device_index)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        if (sSlot[i].id != 0 && device_index-- == 0) {
            return i;
        }
    }
    return -1;
}

static void SWITCH_JoystickDetect(void)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        u32 style;
        padUpdate(&sSlot[i].pad);
        style = padGetStyleSet(&sSlot[i].pad);
        if (sSlot[i].id != 0 && (!padIsConnected(&sSlot[i].pad) || style != sSlot[i].style)) {
            SDL_PrivateJoystickRemoved(sSlot[i].id); /* (gone, or another kind now: put back below as a new one) */
            sSlot[i].id = 0;
        }
        if (padIsConnected(&sSlot[i].pad) && sSlot[i].id == 0) {
            sSlot[i].id = SDL_GetNextObjectID();
            sSlot[i].style = style;
            sSlot[i].vibrationReady = 0;
            SDL_PrivateJoystickAdded(sSlot[i].id);
        }
    }
}

static bool SWITCH_JoystickInit(void)
{
    int i;

    padConfigureInput(MAX_PADS, HidNpadStyleSet_NpadStandard);
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal); /* (a Joy-Con alone is held sideways; a pair is not affected) */
    for (i = 0; i < MAX_PADS; i++) {
        slot_init(i);
    }
    SWITCH_JoystickDetect();
    return true;
}

static int SWITCH_JoystickGetCount(void)
{
    int i, n = 0;

    for (i = 0; i < MAX_PADS; i++) {
        n += sSlot[i].id != 0;
    }
    return n;
}

static bool SWITCH_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    return false;
}

static const char *SWITCH_JoystickGetDeviceName(int device_index)
{
    int i = slot_of_device(device_index);
    u32 style = i < 0 ? 0 : padGetStyleSet(&sSlot[i].pad);

    if (style & HidNpadStyleTag_NpadHandheld) {
        return "Nintendo Switch (handheld)";
    }
    if (style & HidNpadStyleTag_NpadJoyDual) {
        return "Nintendo Switch Joy-Con (L/R)";
    }
    if (single_of(style) == 1) {
        return "Nintendo Switch Joy-Con (L), sideways";
    }
    if (single_of(style) == 2) {
        return "Nintendo Switch Joy-Con (R), sideways";
    }
    if (style & HidNpadStyleTag_NpadFullKey) {
        return "Nintendo Switch Pro Controller";
    }
    return "Nintendo Switch Controller";
}

static const char *SWITCH_JoystickGetDevicePath(int device_index)
{
    return NULL;
}

static int SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int SWITCH_JoystickGetDevicePlayerIndex(int device_index)
{
    return slot_of_device(device_index);
}

static void SWITCH_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_GUID SWITCH_JoystickGetDeviceGUID(int device_index)
{
    return SDL_CreateJoystickGUIDForName(SWITCH_JoystickGetDeviceName(device_index));
}

static SDL_JoystickID SWITCH_JoystickGetDeviceInstanceID(int device_index)
{
    int i = slot_of_device(device_index);
    return i < 0 ? 0 : sSlot[i].id;
}

static bool SWITCH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int i = slot_of_device(device_index);

    if (i < 0) {
        return SDL_SetError("No such controller");
    }
    joystick->hwdata = (struct joystick_hwdata *)(uintptr_t)(i + 1);
    joystick->nbuttons = NUM_BUTTONS;
    joystick->naxes = 4;
    joystick->nhats = 0;
    SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, true);
    return true;
}

static int slot_of(SDL_Joystick *joystick)
{
    return (int)(uintptr_t)joystick->hwdata - 1;
}

static bool SWITCH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    Slot *s = &sSlot[slot_of(joystick)];
    HidVibrationValue v[2];
    HidNpadIdType id = padIsHandheld(&s->pad) ? HidNpadIdType_Handheld : (HidNpadIdType)(HidNpadIdType_No1 + slot_of(joystick));
    u32 style = padGetStyleSet(&s->pad);
    int single = single_of(style), devices = single ? 1 : 2;
    HidNpadStyleTag tag = (style & HidNpadStyleTag_NpadHandheld) ? HidNpadStyleTag_NpadHandheld :
                          (style & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey :
                          single == 1 ? HidNpadStyleTag_NpadJoyLeft : single == 2 ? HidNpadStyleTag_NpadJoyRight : HidNpadStyleTag_NpadJoyDual;

    if (s->vibrationReady != (int)tag) { /* (made again when the controller there changes: a pair split in two) */
        if (R_FAILED(hidInitializeVibrationDevices(s->vibration, devices, id, tag))) {
            s->vibrationReady = 0;
            return SDL_SetError("No rumble on this controller");
        }
        s->vibrationReady = (int)tag;
    }
    v[0].amp_low = (float)low_frequency_rumble / 65535.0f;
    v[0].freq_low = 160.0f;
    v[0].amp_high = (float)high_frequency_rumble / 65535.0f;
    v[0].freq_high = 320.0f;
    v[1] = v[0];
    return R_SUCCEEDED(hidSendVibrationValues(s->vibration, v, devices));
}

static bool SWITCH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool SWITCH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    return SDL_Unsupported();
}

/* libnx's sticks: -32767..32767, up positive; SDL's: down positive */
static Sint16 axis(s32 v, int flip)
{
    if (flip) {
        v = -v;
    }
    return (Sint16)SDL_clamp(v, -32767, 32767);
}

static void SWITCH_JoystickUpdate(SDL_Joystick *joystick)
{
    Slot *s = &sSlot[slot_of(joystick)];
    Uint64 now = SDL_GetTicksNS();
    u64 raw = padGetButtons(&s->pad), buttons = raw;
    HidAnalogStickState l = padGetStickPos(&s->pad, 0), r = padGetStickPos(&s->pad, 1);
    int single = single_of(padGetStyleSet(&s->pad));
    Uint8 b;

    if (single == 1 && (raw & 15) != 0 && !sSystemTurns) {
        sSystemTurns = 1;
        fprintf(stderr, "bt3: pad: a left Joy-Con alone gave A, B, X or Y: the system turns sideways Joy-Con itself\n");
    }
    if (single == 1 && !sSystemTurns) {
        /* the left one sideways (its rail up, the stick on the left): the buttons turned a quarter to the left */
        buttons = MOVE(15, 0) | MOVE(12, 1) | MOVE(14, 2) | MOVE(13, 3) | MOVE(4, 4) | MOVE(24, 8) | MOVE(25, 7) |
                  MOVE(6, 6) | MOVE(8, 9) | MOVE(11, 10);
        r.x = -l.y; /* (r: the turned stick, for the moment) */
        r.y = l.x;
        l = r;
        r.x = r.y = 0;
    } else if (single == 2 && !sSystemTurns) {
        /* the right one sideways (its rail up, the stick on the left): the buttons turned a quarter to the right */
        buttons = MOVE(2, 0) | MOVE(0, 1) | MOVE(3, 2) | MOVE(1, 3) | MOVE(5, 4) | MOVE(26, 8) | MOVE(27, 7) |
                  MOVE(7, 6) | MOVE(9, 9) | MOVE(10, 10);
        l.x = r.y;
        l.y = -r.x;
        r.x = r.y = 0;
    } else if (single == 1) {
        buttons = (raw & ~(u64)(1 << 6 | 1 << 8)) | MOVE(24, 8) | MOVE(25, 7) | MOVE(6, 6) | MOVE(8, 9) | MOVE(11, 10); /* (turned by the system) */
    } else if (single == 2) {
        buttons = (raw & ~(u64)(1 << 7 | 1 << 9)) | MOVE(26, 8) | MOVE(27, 7) | MOVE(7, 6) | MOVE(9, 9);
        l = r; /* (its stick is the right one's) */
        r.x = r.y = 0;
    }

    for (b = 0; b < NUM_BUTTONS; b++) {
        SDL_SendJoystickButton(now, joystick, b, (buttons >> b) & 1);
    }
    SDL_SendJoystickAxis(now, joystick, 0, axis(l.x, 0));
    SDL_SendJoystickAxis(now, joystick, 1, axis(l.y, 1));
    SDL_SendJoystickAxis(now, joystick, 2, axis(r.x, 0));
    SDL_SendJoystickAxis(now, joystick, 3, axis(r.y, 1));
}

static void SWITCH_JoystickClose(SDL_Joystick *joystick)
{
}

static void SWITCH_JoystickQuit(void)
{
    int i;

    for (i = 0; i < MAX_PADS; i++) {
        sSlot[i].id = 0;
    }
}

static bool SWITCH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    /* the bits of HidNpadButton: 0 A, 1 B, 2 X, 3 Y, 4 left stick, 5 right stick, 6 L, 7 R, 8 ZL, 9 ZR, 10 +, 11 -,
       12 left, 13 up, 14 right, 15 down. SDL's a/b/x/y are the positions south/east/west/north. */
    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, 1 },
        .b = { EMappingKind_Button, 0 },
        .x = { EMappingKind_Button, 3 },
        .y = { EMappingKind_Button, 2 },
        .back = { EMappingKind_Button, 11 },
        .start = { EMappingKind_Button, 10 },
        .leftstick = { EMappingKind_Button, 4 },
        .rightstick = { EMappingKind_Button, 5 },
        .leftshoulder = { EMappingKind_Button, 6 },
        .rightshoulder = { EMappingKind_Button, 7 },
        .dpup = { EMappingKind_Button, 13 },
        .dpdown = { EMappingKind_Button, 15 },
        .dpleft = { EMappingKind_Button, 12 },
        .dpright = { EMappingKind_Button, 14 },
        .leftx = { EMappingKind_Axis, 0 },
        .lefty = { EMappingKind_Axis, 1 },
        .rightx = { EMappingKind_Axis, 2 },
        .righty = { EMappingKind_Axis, 3 },
        .lefttrigger = { EMappingKind_Button, 8 },
        .righttrigger = { EMappingKind_Button, 9 },
    };
    return true;
}

SDL_JoystickDriver SDL_SWITCH_JoystickDriver = {
    SWITCH_JoystickInit,
    SWITCH_JoystickGetCount,
    SWITCH_JoystickDetect,
    SWITCH_JoystickIsDevicePresent,
    SWITCH_JoystickGetDeviceName,
    SWITCH_JoystickGetDevicePath,
    SWITCH_JoystickGetDeviceSteamVirtualGamepadSlot,
    SWITCH_JoystickGetDevicePlayerIndex,
    SWITCH_JoystickSetDevicePlayerIndex,
    SWITCH_JoystickGetDeviceGUID,
    SWITCH_JoystickGetDeviceInstanceID,
    SWITCH_JoystickOpen,
    SWITCH_JoystickRumble,
    SWITCH_JoystickRumbleTriggers,
    SWITCH_JoystickSetLED,
    SWITCH_JoystickSendEffect,
    SWITCH_JoystickSetSensorsEnabled,
    SWITCH_JoystickUpdate,
    SWITCH_JoystickClose,
    SWITCH_JoystickQuit,
    SWITCH_JoystickGetGamepadMapping
};

#endif // SDL_JOYSTICK_SWITCH
