/* ECHOFIELD - SDL audio output */
#ifndef EF_AUDIO_H
#define EF_AUDIO_H
#include "engine.h"
#include "compat/sdl_min.h"

typedef struct {
    SDL_AudioDeviceID dev;
    SDL_AudioSpec spec;
    EfEngine *engine;
    char error[160];
    char device_name[96];
    _Atomic int callbacks;
    _Atomic float load;         /* render time / buffer time, smoothed */
    _Atomic float load_peak;
} EfAudioOut;

bool ef_audio_open(EfAudioOut *a, EfEngine *e, int want_rate, int want_frames);
void ef_audio_lock(EfAudioOut *a);
void ef_audio_unlock(EfAudioOut *a);
void ef_audio_close(EfAudioOut *a);
#endif
