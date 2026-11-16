/* Isolated muted/dummy sink experiment. This does not test game audio quality. */
#ifndef OSL_AUDIO_IMPLEMENTATION
#define OSL_AUDIO_IMPLEMENTATION "../../src/runtime_audio.c"
#endif
#include OSL_AUDIO_IMPLEMENTATION
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(int argc,char **argv) {
    assert(argc==3);
    setenv("OSL_AUDIO","muted",1);
    if (atoi(argv[1])) setenv("OSL_AUDIO_QUEUE_BUFFERS",argv[1],1);
    else unsetenv("OSL_AUDIO_QUEUE_BUFFERS");
    unsigned n=(unsigned)strtoul(argv[2],NULL,10);assert(n>=100 && n<=5000);
    assert(audio_init()==0);
    int handle=audio_open(1,0,0,256,48000,5);
    assert(handle>0 && ports[0].stream && SDL_GetAudioStreamGain(ports[0].stream)==0.0f);
    SDL_AudioSpec spec;int device_frames=0;
    assert(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(ports[0].stream),&spec,&device_frames));
    int initial=SDL_GetAudioStreamQueued(ports[0].stream);
    float silence[256*8]={0};OslAudioInput input;
    assert(runtime_audio_input_format(handle,&input)==0 && input.bytes==sizeof(silence));
    unsigned empty=0,warm_empty=0;int min=INT32_MAX,max=0;
    for(unsigned i=0;i<n;++i) {
        /* A deterministic 30 ms producer delay every 40 submissions tests
         * queue headroom; it is not a model of the game's CPU scheduling. */
        if(i && i%40==0) SDL_Delay(30);
        int q=SDL_GetAudioStreamQueued(ports[0].stream);assert(q>=0);
        if(q==0) {++empty;if(i>=40)++warm_empty;}
        if(i>=40 && q<min)min=q;if(q>max)max=q;
        assert(runtime_audio_output_copy(handle,silence,sizeof(silence),&input,1,NULL,NULL)==256*8);
    }
    printf("QUEUE_STUDY:{\"driver\":\"%s\",\"device_frames\":%d,\"rate\":%d,\"host_muted\":true,\"requested_buffers\":%d,\"initial_bytes\":%d,\"submissions\":%u,\"empty\":%u,\"empty_after_warmup\":%u,\"min_after_warmup\":%d,\"max_bytes\":%d,\"injected_delay_ms\":30,\"game_audio_accepted\":false}\n",SDL_GetCurrentAudioDriver(),device_frames,spec.freq,atoi(argv[1]),initial,n,empty,warm_empty,min,max);
    assert(audio_close(handle)==0);runtime_audio_end_session();
}
