/* Deterministic opt-in bounds and format checks using SDL's dummy driver. */
#include "../../src/runtime_audio.c"
#include <assert.h>
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    setenv("SDL_AUDIODRIVER","dummy",1);setenv("OSL_AUDIO","muted",1);
    assert(audio_init()==0);
    const char *choices[]={"2","8","16","1","17","garbage","8x",""};
    for(unsigned c=0;c<sizeof(choices)/sizeof(*choices);++c) for(int fmt=0;fmt<8;++fmt) for(int len=256;len<=2048;len+=1792) {
        setenv("OSL_AUDIO_QUEUE_BUFFERS",choices[c],1);
        int handle=audio_open(1,0,0,(uint32_t)len,48000,(uint32_t)fmt);assert(handle>0);
        Port *p=&ports[0];assert(p->stream && SDL_GetAudioStreamGain(p->stream)==0.0f);
        int wanted=c<3?atoi(choices[c]):0;assert(p->experimental_queue_buffers==wanted);
        assert(SDL_PauseAudioStreamDevice(p->stream));
        int queued=SDL_GetAudioStreamQueued(p->stream),bytes=len*(p->downmix?2:p->channels)*p->sample_bytes;
        assert(queued>=0 && queued<=wanted*bytes);
        if(wanted)assert(queued>=(wanted-1)*bytes);
        assert(audio_close(handle)==0);
    }
    runtime_audio_end_session();puts("QUEUE_BOUNDS: 128 format/length/prefill cases passed; muted dummy sink");
}
