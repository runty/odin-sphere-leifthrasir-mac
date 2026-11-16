#include "runtime_audio_pull_stats.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 48000 };
    SDL_AudioStream *stream = SDL_CreateAudioStream(&spec, &spec);
    assert(stream);
    OslAudioPullStats stats;
    atomic_init(&stats.pulls, 0);
    atomic_init(&stats.requests_with_shortfall, 0);
    atomic_init(&stats.estimated_shortfall_bytes, 0);
    assert(SDL_SetAudioStreamGetCallback(stream, osl_audio_pull_observe, &stats));
    float input[512] = {0}, output[512];
    assert(SDL_PutAudioStreamData(stream, input, sizeof(input)));
    assert(SDL_GetAudioStreamData(stream, output, 256) == 256);
    assert(atomic_load(&stats.pulls) == 1);
    assert(atomic_load(&stats.requests_with_shortfall) == 0);
    assert(atomic_load(&stats.estimated_shortfall_bytes) == 0);
    assert(SDL_ClearAudioStream(stream));
    assert(SDL_GetAudioStreamData(stream, output, 512) == 0);
    assert(atomic_load(&stats.pulls) == 2);
    assert(atomic_load(&stats.requests_with_shortfall) == 1);
    assert(atomic_load(&stats.estimated_shortfall_bytes) >= 512);
    SDL_DestroyAudioStream(stream);
    puts("audio pull observation: queued and empty stream passed");
    return 0;
}
