// Exercise the live SDL stream with the same implementation as the Mac game.
#undef NDEBUG
#include <assert.h>
#include "../src/runtime_audio.c"

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(name, table[i].name)) return (uintptr_t)table[i].function;
    }
    return 0;
}

int main(void) {
    assert(setenv("OSL_AUDIO", "muted", 1) == 0);
    assert(setenv("OSL_AUDIO_STATS", "1", 1) == 0);
    assert(audio_init() == 0);
    const int frames = 256;
    const int handle = audio_open(1, 0, 0, frames, 48000, 5);
    assert(handle > 0);
    SDL_AudioStream *stream = ports[0].stream;
    assert(stream && SDL_GetAudioStreamGain(stream) == 0.0f);
    SDL_AudioSpec input, device;
    int quantum = 0;
    assert(SDL_GetAudioStreamFormat(stream, &input, NULL));
    assert(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, &quantum));
    assert(quantum > 0 && input.freq == 48000 && device.freq > 0);
    const int frame_bytes = input.channels * SDL_AUDIO_BYTESIZE(input.format);
    float silence[256 * 8] = {0};

    for (int i = 0; i < 64; ++i) assert(audio_output(handle, silence) == frames * 8);
    const unsigned initial_starvation = ports[0].stat_starved;
    assert(SDL_PauseAudioStreamDevice(stream));
    for (int i = 0; i < 192; ++i) {
        assert(audio_output(handle, silence) == frames * 8);
        assert(SDL_GetAudioStreamQueued(stream) == 0);
    }
    assert(ports[0].stat_starved == initial_starvation);
    assert(SDL_ResumeAudioStreamDevice(stream));
    int max_queued = 0;
    for (int i = 0; i < 64; ++i) {
        assert(audio_output(handle, silence) == frames * 8);
        const int queued = SDL_GetAudioStreamQueued(stream);
        assert(queued >= 0);
        if (queued > max_queued) max_queued = queued;
    }
    // Express the device quantum in source frames before comparing queued bytes.
    const int input_quantum = (int)((int64_t)quantum * input.freq / device.freq) + 1;
    assert(max_queued > 0 && max_queued <= (4 * input_quantum + 8 * frames) * frame_bytes);
    printf("Audio pause passed on %s, resumed peak %d source frames\n",
           SDL_GetCurrentAudioDriver(), max_queued / frame_bytes);
    assert(audio_close(handle) == 0);
    runtime_audio_end_session();
}
