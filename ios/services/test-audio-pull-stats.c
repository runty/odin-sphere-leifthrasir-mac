/* Exercise the real adapter with a muted, paused dummy stream. */
#include "../../src/runtime_audio.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}

int main(void) {
    setenv("SDL_AUDIODRIVER","dummy",1);
    setenv("OSL_AUDIO","muted",1);
    assert(audio_init()==0);
    for (int enabled=0;enabled<=1;++enabled) {
        if (enabled) setenv("OSL_AUDIO_PULL_STATS","1",1);
        else unsetenv("OSL_AUDIO_PULL_STATS");
        int handle=audio_open(1,0,0,256,48000,4);assert(handle>0);
        Port *p=&ports[0];assert(p->stream && SDL_PauseAudioStreamDevice(p->stream));
        assert(p->pull_stats_enabled==enabled);
        atomic_store(&p->pull_stats.pulls,0);
        atomic_store(&p->pull_stats.requests_with_shortfall,0);
        atomic_store(&p->pull_stats.estimated_shortfall_bytes,0);
        assert(SDL_ClearAudioStream(p->stream));
        float out[256*2];
        assert(SDL_GetAudioStreamData(p->stream,out,sizeof(out))==0);
        assert(atomic_load(&p->pull_stats.pulls)==(unsigned)enabled);
        assert(atomic_load(&p->pull_stats.requests_with_shortfall)==(unsigned)enabled);
        if(enabled) assert(atomic_load(&p->pull_stats.estimated_shortfall_bytes)>=sizeof(out));
        assert(audio_close(handle)==0);
    }
    runtime_audio_end_session();
    puts("audio pull observation: disabled/enabled adapter and close passed");
}
