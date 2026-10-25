/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* Altered for ULTRAKILL NX: release-based bounded audio pacing, not a
 * wait for the transient Playing state. Private ABI pinned to SDL 2.28.5.
 * We link into the static SDL; no dynapi indirection is needed here. */
#include <SDL2/SDL.h>
#define SDL_internal_h_
#include "util.h"
#include <stddef.h>
#include "vendor/sdl2285/audio/SDL_sysaudio.h"
_Static_assert(SDL_MAJOR_VERSION==2 && SDL_MINOR_VERSION==28 && SDL_PATCHLEVEL==5, "SDL audio ABI changed");
_Static_assert(offsetof(SDL_AudioDevice, hidden)==144, "SDL private layout changed");

#if SDL_AUDIO_DRIVER_SWITCH

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>



#include "vendor/sdl2285/audio/switch/SDL_switchaudio.h"

static const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 24,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

static int
NXSDL_OpenDevice(_THIS, const char *devname)
{
    static const u8 sink_channels[] = {0, 1};
    Result res;
    u32 size;
    int mpid;

    this->hidden = (struct SDL_PrivateAudioData *) SDL_calloc(1, sizeof(*this->hidden));
    if (this->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    res = audrenInitialize(&arConfig);
    if (R_FAILED(res)) {
        return SDL_SetError("audrenInitialize failed (0x%x)", res);
    }
    this->hidden->audr_device = true;

    res = audrvCreate(&this->hidden->driver, &arConfig, 2);
    if (R_FAILED(res)) {
        return SDL_SetError("audrvCreate failed (0x%x)", res);
    }
    this->hidden->audr_driver = true;

    this->spec.format = AUDIO_S16SYS;
    SDL_CalculateAudioSpec(&this->spec);

    size = (u32) ((this->spec.size * 2) + 0xfff) & ~0xfff;
    this->hidden->pool = memalign(0x1000, size);
    for (int i = 0; i < 2; i++) {
        this->hidden->buffer[i].data_raw = this->hidden->pool;
        this->hidden->buffer[i].size = this->spec.size * 2;
        this->hidden->buffer[i].start_sample_offset = i * this->spec.samples;
        this->hidden->buffer[i].end_sample_offset = this->hidden->buffer[i].start_sample_offset + this->spec.samples;
    }

    this->hidden->buffer_tmp = malloc(this->spec.size);
    if (!this->hidden->pool || !this->hidden->buffer_tmp) return SDL_OutOfMemory();
    mpid = audrvMemPoolAdd(&this->hidden->driver, this->hidden->pool, size);
    audrvMemPoolAttach(&this->hidden->driver, mpid);

    audrvDeviceSinkAdd(&this->hidden->driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    res = audrenStartAudioRenderer();
    if (R_FAILED(res)) {
        return SDL_SetError("audrenStartAudioRenderer failed (0x%x)", res);
    }

    audrvVoiceInit(&this->hidden->driver, 0, this->spec.channels, PcmFormat_Int16, this->spec.freq);
    audrvVoiceSetDestinationMix(&this->hidden->driver, 0, AUDREN_FINAL_MIX_ID);
    if (this->spec.channels == 1) {
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 1);
    }
    else {
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 0, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 0.0f, 0, 1);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 0.0f, 1, 0);
        audrvVoiceSetMixFactor(&this->hidden->driver, 0, 1.0f, 1, 1);
    }

    audrvVoiceStart(&this->hidden->driver, 0);

    debugPrintf("[audio-driver] release-based bounded SDL 2.28.5 backend active\n");
    return 0;
}

/* A buffer may go Queued -> Done between two observations under load. The
 * old wait for state == Playing then never returned. Pace on ownership:
 * only Free/Done buffers may be written, and never access slot -1. */
static int nx_audio_writable(SDL_AudioDevice *this) {
    for (int i=0; i<2; ++i) {
        AudioDriverWaveBufState state=this->hidden->buffer[i].state;
        if(state==AudioDriverWaveBufState_Free || state==AudioDriverWaveBufState_Done) return i;
    }
    return -1;
}
static int nx_audio_wait_writable(SDL_AudioDevice *this) {
    for(unsigned attempt=0; attempt<20; ++attempt) {
        if(SDL_AtomicGet(&this->shutdown)) return -1;
        Result rc=audrvUpdate(&this->hidden->driver);
        if(R_FAILED(rc)) {
            debugPrintf("[audio-driver] audrvUpdate failed rc=0x%x\n",rc);
            SDL_OpenedAudioDeviceDisconnected(this);return -1;
        }
        int slot=nx_audio_writable(this);
        if(slot>=0) return slot;
        // Timeouts are expected during suspend. No unbounded wait or queue reset.
        eventWait(audrenGetFrameEvent(),5000000ULL);
    }
    static unsigned timeouts;
    if(++timeouts<=4 || (timeouts%100)==0)
        debugPrintf("[audio-driver] no released buffer after bounded wait (%u)\n",timeouts);
    return -1;
}
static void NXSDL_PlayDevice(SDL_AudioDevice *this) {
    int current=nx_audio_wait_writable(this);
    if(current<0) return; // leave in-flight buffers owned by the renderer
    Uint8 *ptr=(Uint8*)this->hidden->pool+current*this->spec.size;
    memcpy(ptr,this->hidden->buffer_tmp,this->spec.size);
    armDCacheFlush(ptr,this->spec.size);
    audrvVoiceAddWaveBuf(&this->hidden->driver,0,&this->hidden->buffer[current]);
    if(!audrvVoiceIsPlaying(&this->hidden->driver,0)) audrvVoiceStart(&this->hidden->driver,0);
    Result rc=audrvUpdate(&this->hidden->driver);
    if(R_FAILED(rc)) SDL_OpenedAudioDeviceDisconnected(this);
}
static void NXSDL_WaitDevice(SDL_AudioDevice *this) { (void)nx_audio_wait_writable(this); }

static Uint8
*NXSDL_GetDeviceBuf(_THIS)
{
    return this->hidden->buffer_tmp;
}

static void
NXSDL_CloseDevice(_THIS)
{
    if (this->hidden->audr_driver) {
        audrvClose(&this->hidden->driver);
    }

    if (this->hidden->audr_device) {
        audrenExit();
    }

    if (this->hidden->buffer_tmp) {
        free(this->hidden->buffer_tmp);
    }

    free(this->hidden->pool);
    SDL_free(this->hidden);
}

static void
NXSDL_ThreadInit(_THIS)
{

}

static SDL_bool
NXSDL_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = NXSDL_OpenDevice;
    impl->PlayDevice = NXSDL_PlayDevice;
    impl->WaitDevice = NXSDL_WaitDevice;
    impl->GetDeviceBuf = NXSDL_GetDeviceBuf;
    impl->CloseDevice = NXSDL_CloseDevice;
    impl->ThreadInit = NXSDL_ThreadInit;

    impl->OnlyHasDefaultOutputDevice = 1;

    return 1;
}

AudioBootStrap SWITCHAUDIO_bootstrap = {
    "switch", "Nintendo Switch audio driver", NXSDL_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_SWITCH */
