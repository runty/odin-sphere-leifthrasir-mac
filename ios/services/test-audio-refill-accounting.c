/* Paused dummy output makes the actual post-refill queue observable. */
#include "../../src/runtime_audio.c"
#include <assert.h>
#include <limits.h>
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
    setenv("SDL_AUDIODRIVER","dummy",1);setenv("OSL_AUDIO","muted",1);
    unsetenv("OSL_AUDIO_QUEUE_BUFFERS");assert(audio_init()==0);
    int handle=audio_open(1,0,0,256,48000,4);assert(handle>0);
    Port *p=&ports[0];assert(p->stream && SDL_PauseAudioStreamDevice(p->stream));
    float input[256*2]={0};OslAudioInput format;
    assert(runtime_audio_input_format(handle,&format)==0 && format.bytes==sizeof(input));
    for (int initial=0;initial<=2048;initial+=1024) {
        assert(SDL_ClearAudioStream(p->stream));
        if(initial) assert(SDL_PutAudioStreamData(p->stream,input,initial));
        p->window_count=1;p->window_min=INT_MAX;
        assert(runtime_audio_output_copy(handle,input,sizeof(input),&format,0,NULL,NULL)==512);
        int queued=SDL_GetAudioStreamQueued(p->stream);
        int after_refill=queued-(int)sizeof(input);
        assert(after_refill==(initial<2048 ? initial+2048 : initial));
        assert(p->window_min==after_refill);
    }
    assert(audio_close(handle)==0);runtime_audio_end_session();
    puts("REFILL_ACCOUNTING: empty, partial and full pre-queues agree with actual SDL stream");
}
