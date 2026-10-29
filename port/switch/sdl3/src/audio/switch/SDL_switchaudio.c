/* SDL3's audio driver for the Nintendo Switch (homebrew, libnx): part of Tenkaichi3Decomp's Switch port
   (docs/port/switch_plan.md), under that project's licence, NOT part of SDL. port/switch/sdl3/build.py copies it into
   SDL's source tree to build SDL for the port. SDL does not accept code written with AI tools (its CLAUDE.md), and
   this file was: it is not meant to be offered to SDL. Modelled on SDL's console drivers (audio/n3ds).

   Playback through libnx's audout service: 48000 Hz, two channels, signed 16-bit (all it takes; SDL converts the
   rest), NUM_BUFFERS buffers of one device period appended in turn. The mixing thread runs on its own core.
   What audout refuses is written to the log (stderr: the game's bt3_log.txt), the first few times, and the thread
   waits a period then instead of trying again at once: a mixing thread that never waits keeps a core and the
   console's sound service busy (the first console run of the game hung the whole console after its first sound). */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_SWITCH

#include "../SDL_sysaudio.h"

#include <malloc.h>
#include <stdio.h>
#include <switch.h>

#define NUM_BUFFERS 3

struct SDL_PrivateAudioData
{
    AudioOutBuffer buffer[NUM_BUFFERS];
    Uint8 *mixbuf;
    int next, queued;
    int refused;      /* the last buffer was refused: wait a period before the next */
    int complaints;   /* lines written to the log so far */
    int played;       /* a buffer has come back played (said once in the log) */
};

#define COMPLAINTS 5

/* One line in the log, the first COMPLAINTS times */
static void complain(struct SDL_PrivateAudioData *h, const char *what, Result rc)
{
    if (h->complaints < COMPLAINTS) {
        h->complaints++;
        fprintf(stderr, "bt3: sound (audout): %s: error 0x%X (module %u, description %u)%s\n", what, (unsigned)rc,
                (unsigned)R_MODULE(rc), (unsigned)R_DESCRIPTION(rc), h->complaints == COMPLAINTS ? "; not said again" : "");
    }
}

/* A device period, in nanoseconds */
static u64 period_ns(SDL_AudioDevice *device)
{
    return (u64)device->sample_frames * 1000000000ull / (u64)(device->spec.freq > 0 ? device->spec.freq : 48000);
}

static bool SWITCHAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *h;
    size_t size;
    Result rc;
    int i;

    h = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*h));
    if (!h) {
        return false;
    }
    device->hidden = h;
    rc = audoutInitialize();
    if (R_FAILED(rc)) {
        complain(h, "opening the sound output", rc);
        return SDL_SetError("audout: could not open the sound output");
    }
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        complain(h, "starting the sound output", rc);
        audoutExit();
        return SDL_SetError("audout: could not start the sound output");
    }
    device->spec.freq = (int)audoutGetSampleRate();
    device->spec.channels = (int)audoutGetChannelCount();
    device->spec.format = SDL_AUDIO_S16;
    SDL_UpdatedAudioDeviceFormat(device);
    size = ((size_t)device->buffer_size + 0xFFF) & ~(size_t)0xFFF; /* (audout wants 4 KB-aligned buffers) */
    h->mixbuf = (Uint8 *)SDL_malloc((size_t)device->buffer_size);
    if (!h->mixbuf) {
        return false;
    }
    for (i = 0; i < NUM_BUFFERS; i++) {
        h->buffer[i].buffer = memalign(0x1000, size);
        if (!h->buffer[i].buffer) {
            return SDL_OutOfMemory();
        }
        SDL_memset(h->buffer[i].buffer, 0, size);
        h->buffer[i].buffer_size = size;
        h->buffer[i].data_size = (u64)device->buffer_size;
        h->buffer[i].data_offset = 0;
    }
    return true;
}

/* Until a buffer is free: one is given back by audout when it has been played */
static bool SWITCHAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *h = device->hidden;

    if (h->refused) {
        h->refused = 0;
        svcSleepThread(period_ns(device));
        return true;
    }
    while (h->queued >= NUM_BUFFERS && !SDL_GetAtomicInt(&device->shutdown)) {
        AudioOutBuffer *released = NULL;
        u32 count = 0;
        Result rc = audoutWaitPlayFinish(&released, &count, 100000000ull); /* (0.1 s, to see a shutdown) */
        if (R_FAILED(rc)) {
            if (R_VALUE(rc) != KERNELRESULT(TimedOut)) {
                complain(h, "waiting for a buffer", rc);
                svcSleepThread(period_ns(device)); /* (never at once again: the error may be at once too) */
            }
            continue;
        }
        if (count == 0) { /* (woken with nothing given back: not at once again either) */
            svcSleepThread(1000000ull);
            continue;
        }
        if (!h->played) {
            h->played = 1;
            fprintf(stderr, "bt3: sound (audout): playing, %d Hz, %d channels, %d frames a buffer\n", device->spec.freq,
                    device->spec.channels, device->sample_frames);
        }
        h->queued -= (int)count;
        if (h->queued < 0) {
            h->queued = 0;
        }
    }
    return true;
}

static bool SWITCHAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    struct SDL_PrivateAudioData *h = device->hidden;
    AudioOutBuffer *b = &h->buffer[h->next];
    Result rc;

    SDL_memcpy(b->buffer, buffer, (size_t)buflen);
    b->data_size = (u64)buflen;
    rc = audoutAppendAudioOutBuffer(b);
    if (R_SUCCEEDED(rc)) {
        h->next = (h->next + 1) % NUM_BUFFERS;
        h->queued++;
    } else {
        complain(h, "handing over a buffer", rc);
        h->refused = 1; /* (not here: this runs holding the device's lock) */
    }
    return true;
}

static Uint8 *SWITCHAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    return device->hidden->mixbuf;
}

static void SWITCHAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *h = device->hidden;
    int i;

    if (!h) {
        return;
    }
    audoutStopAudioOut();
    audoutExit();
    for (i = 0; i < NUM_BUFFERS; i++) {
        free(h->buffer[i].buffer);
    }
    SDL_free(h->mixbuf);
    SDL_free(h);
    device->hidden = NULL;
}

/* The mixing thread: on core 2 (the game runs on core 0) and a little before the others */
static void SWITCHAUDIO_ThreadInit(SDL_AudioDevice *device)
{
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, 1u << 2);
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x2B);
}

static bool SWITCHAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = SWITCHAUDIO_OpenDevice;
    impl->WaitDevice = SWITCHAUDIO_WaitDevice;
    impl->PlayDevice = SWITCHAUDIO_PlayDevice;
    impl->GetDeviceBuf = SWITCHAUDIO_GetDeviceBuf;
    impl->CloseDevice = SWITCHAUDIO_CloseDevice;
    impl->ThreadInit = SWITCHAUDIO_ThreadInit;
    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->HasRecordingSupport = false;
    return true;
}

AudioBootStrap SWITCHAUDIO_bootstrap = {
    "switch",
    "Nintendo Switch audio driver (Tenkaichi3Decomp)",
    SWITCHAUDIO_Init,
    false,
    false
};

#endif // SDL_AUDIO_DRIVER_SWITCH
