/* SDL3's audio driver for the Nintendo Switch (homebrew, libnx): part of Tenkaichi3Decomp's Switch port
   (docs/port/switch_plan.md), under that project's licence, NOT part of SDL. port/switch/sdl3/build.py copies it into
   SDL's source tree to build SDL for the port. SDL does not accept code written with AI tools (its CLAUDE.md), and
   this file was: it is not meant to be offered to SDL. Modelled on SDL's console drivers (audio/n3ds).

   Playback through libnx's audout service: 48000 Hz, two channels, signed 16-bit (all it takes; SDL converts the
   rest), NUM_BUFFERS buffers of one device period appended in turn. The mixing thread runs on its own core. */
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_SWITCH

#include "../SDL_sysaudio.h"

#include <malloc.h>
#include <switch.h>

#define NUM_BUFFERS 3

struct SDL_PrivateAudioData
{
    AudioOutBuffer buffer[NUM_BUFFERS];
    Uint8 *mixbuf;
    int next, queued;
};

static bool SWITCHAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *h;
    size_t size;
    int i;

    h = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*h));
    if (!h) {
        return false;
    }
    device->hidden = h;
    if (R_FAILED(audoutInitialize())) {
        return SDL_SetError("audout: could not open the sound output");
    }
    if (R_FAILED(audoutStartAudioOut())) {
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

    while (h->queued >= NUM_BUFFERS && !SDL_GetAtomicInt(&device->shutdown)) {
        AudioOutBuffer *released = NULL;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 100000000ull))) { /* (0.1 s, to see a shutdown) */
            continue;
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

    SDL_memcpy(b->buffer, buffer, (size_t)buflen);
    b->data_size = (u64)buflen;
    if (R_SUCCEEDED(audoutAppendAudioOutBuffer(b))) {
        h->next = (h->next + 1) % NUM_BUFFERS;
        h->queued++;
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
