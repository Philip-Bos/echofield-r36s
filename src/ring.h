/* ECHOFIELD - single-producer / single-consumer lock-free stereo float ring buffer.
 * Producer: capture thread. Consumer: audio callback. No locks, no allocation after init. */
#ifndef EF_RING_H
#define EF_RING_H
#include "common.h"

typedef struct {
    float *buf;              /* interleaved stereo */
    int cap;                 /* frames, power of two */
    _Atomic int64_t wr, rd;  /* monotonically increasing frame counters */
    _Atomic int64_t overruns;
} EfRing;

static inline bool ef_ring_init(EfRing *r, int cap_pow2) {
    r->buf = calloc((size_t)cap_pow2 * 2, sizeof(float));
    r->cap = cap_pow2;
    atomic_store(&r->wr, 0); atomic_store(&r->rd, 0); atomic_store(&r->overruns, 0);
    return r->buf != NULL;
}
static inline void ef_ring_free(EfRing *r) { free(r->buf); r->buf = NULL; }
static inline int ef_ring_fill(EfRing *r) {
    return (int)(atomic_load_explicit(&r->wr, memory_order_acquire) - atomic_load_explicit(&r->rd, memory_order_acquire));
}
/* producer: drops (and counts) frames that do not fit */
static inline int ef_ring_write(EfRing *r, const float *lr, int frames) {
    int64_t w = atomic_load_explicit(&r->wr, memory_order_relaxed);
    int64_t rd = atomic_load_explicit(&r->rd, memory_order_acquire);
    int space = r->cap - (int)(w - rd);
    if (frames > space) { atomic_fetch_add(&r->overruns, frames - space); frames = space; }
    for (int i = 0; i < frames; i++) {
        int idx = (int)((w + i) & (r->cap - 1));
        r->buf[idx * 2] = lr[i * 2];
        r->buf[idx * 2 + 1] = lr[i * 2 + 1];
    }
    atomic_store_explicit(&r->wr, w + frames, memory_order_release);
    return frames;
}
/* consumer: peek frame at offset from read head (offset < fill) */
static inline void ef_ring_peek(EfRing *r, int offset, float *l, float *rr) {
    int64_t rd = atomic_load_explicit(&r->rd, memory_order_relaxed);
    int idx = (int)((rd + offset) & (r->cap - 1));
    *l = r->buf[idx * 2]; *rr = r->buf[idx * 2 + 1];
}
static inline void ef_ring_advance(EfRing *r, int frames) {
    atomic_fetch_add_explicit(&r->rd, frames, memory_order_release);
}
static inline void ef_ring_reset_consumer(EfRing *r) {
    atomic_store_explicit(&r->rd, atomic_load_explicit(&r->wr, memory_order_acquire), memory_order_release);
}
#endif
