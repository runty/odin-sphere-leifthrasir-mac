#pragma once

/* Optional observation only: SDL's requested additional input is an estimate,
 * not a CoreAudio hardware-underrun counter. Callback performs no allocation,
 * I/O, or producer locking, and remains valid until stream destruction. */
#include <SDL3/SDL.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    _Atomic uint64_t pulls;
    _Atomic uint64_t requests_with_shortfall;
    _Atomic uint64_t estimated_shortfall_bytes;
} OslAudioPullStats;

static void SDLCALL osl_audio_pull_observe(void *userdata, SDL_AudioStream *stream,
                                          int additional, int total) {
    (void)stream; (void)total;
    OslAudioPullStats *stats = (OslAudioPullStats *)userdata;
    atomic_fetch_add_explicit(&stats->pulls, 1, memory_order_relaxed);
    if (additional > 0) {
        atomic_fetch_add_explicit(&stats->requests_with_shortfall, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&stats->estimated_shortfall_bytes, (uint64_t)additional, memory_order_relaxed);
    }
}
