/* ECHOFIELD - SDL audio output. The callback only renders the engine (no locks of
 * our own, no allocation, no I/O) and measures its own CPU cost. */
#include "audio.h"

static void callback(void *ud, Uint8 *stream, int len) {
    EfAudioOut *a = ud;
    Uint64 t0 = SDL_GetPerformanceCounter();
    int frames = len / (int)(sizeof(float) * 2);
    if (a->engine && a->engine->sr > 0) ef_engine_render(a->engine, (float *)stream, frames);
    else memset(stream, 0, (size_t)len);
    Uint64 t1 = SDL_GetPerformanceCounter();
    double used = (double)(t1 - t0) / (double)SDL_GetPerformanceFrequency();
    double avail = (double)frames / a->spec.freq;
    float l = (float)(used / avail);
    float cur = atomic_load_explicit(&a->load, memory_order_relaxed);
    atomic_store_explicit(&a->load, cur + (l - cur) * 0.05f, memory_order_relaxed);
    if (l > atomic_load_explicit(&a->load_peak, memory_order_relaxed)) atomic_store_explicit(&a->load_peak, l, memory_order_relaxed);
    atomic_fetch_add_explicit(&a->callbacks, 1, memory_order_relaxed);
}

bool ef_audio_open(EfAudioOut *a, EfEngine *e, int want_rate, int want_frames) {
    memset(&a->spec, 0, sizeof a->spec);
    a->engine = e;
    SDL_AudioSpec want;
    memset(&want, 0, sizeof want);
    want.freq = want_rate;
    want.format = AUDIO_F32LSB;
    want.channels = 2;
    want.samples = (Uint16)want_frames;
    want.callback = callback;
    want.userdata = a;
    /* only allow a frequency change: SDL converts any other format for us */
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &a->spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!a->dev) {
        snprintf(a->error, sizeof a->error, "audio output failed: %s", SDL_GetError());
        ef_log("%s", a->error);
        return false;
    }
    ef_strlcpy(a->device_name, SDL_GetNumAudioDevices(0) > 0 ? SDL_GetAudioDeviceName(0, 0) : "default", sizeof a->device_name);
    ef_log("audio out: %d Hz, %d ch, %d frames, driver %s", a->spec.freq, a->spec.channels, a->spec.samples, SDL_GetCurrentAudioDriver());
    return true;
}

void ef_audio_lock(EfAudioOut *a) { if (a->dev) SDL_LockAudioDevice(a->dev); }
void ef_audio_unlock(EfAudioOut *a) { if (a->dev) SDL_UnlockAudioDevice(a->dev); }
void ef_audio_close(EfAudioOut *a) {
    if (a->dev) { SDL_PauseAudioDevice(a->dev, 1); SDL_CloseAudioDevice(a->dev); }
    a->dev = 0;
}
