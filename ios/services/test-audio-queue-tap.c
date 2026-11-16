/* Queue headroom and paired tap together, using only muted dummy output. */
#include "../../src/runtime_audio.c"
#include <assert.h>
#include <unistd.h>
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *name) {
    for(size_t i=0;i<n;++i)if(!strcmp(t[i].name,name))return (uintptr_t)t[i].function;
    return 0;
}
int main(void) {
    char dir[]="/tmp/odin-queue-tap-XXXXXX",prefix[512],guest[600],converted[600];
    assert(mkdtemp(dir));snprintf(prefix,sizeof(prefix),"%s/pair",dir);
    setenv("SDL_AUDIODRIVER","dummy",1);setenv("OSL_AUDIO","muted",1);
    setenv("OSL_AUDIO_QUEUE_BUFFERS","8",1);setenv("OSL_AUDIO_TAP",prefix,1);
    setenv("OSL_AUDIO_TAP_BLOCKS","2",1);setenv("OSL_AUDIO_TAP_SKIP","0",1);
    assert(audio_init()==0);int h=audio_open(1,0,0,256,48000,4);assert(h>0);
    assert(ports[0].stream && ports[0].experimental_queue_buffers==8);
    assert(SDL_GetAudioStreamGain(ports[0].stream)==0.0f);
    float samples[512];for(unsigned i=0;i<512;++i)samples[i]=(i%2)?-0.25f:0.5f;
    OslAudioInput format;assert(runtime_audio_input_format(h,&format)==0);
    for(unsigned i=0;i<2;++i)assert(runtime_audio_output_copy(h,samples,sizeof(samples),&format,1,NULL,NULL)==512);
    assert(audio_close(h)==0);runtime_audio_end_session();
    snprintf(guest,sizeof(guest),"%s.port0.gen1.guest.2ch.f32",prefix);
    snprintf(converted,sizeof(converted),"%s.port0.gen1.converted.2ch.f32",prefix);
    for(unsigned i=0;i<2;++i){const char *path=i?converted:guest;FILE *f=fopen(path,"rb");assert(f);
        for(unsigned block=0;block<2;++block){float actual[512];assert(fread(actual,sizeof(float),512,f)==512);assert(!memcmp(actual,samples,sizeof(samples)));}
        assert(fgetc(f)==EOF && !ferror(f));assert(fclose(f)==0);assert(unlink(path)==0);}
    assert(rmdir(dir)==0);puts("QUEUE_TAP: muted prefill and paired samples passed");
}
