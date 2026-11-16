/* Bounded diagnostic tap records the unchanged guest input beside the exact
 * pre-queue conversion. This is a synthetic timer-sink test, not game audio. */
#include "../../src/runtime_audio.c"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <unistd.h>
uintptr_t runtime_lookup(const RuntimeExport *t,size_t n,const char *s) {
    for(size_t i=0;i<n;++i) if(!strcmp(t[i].name,s)) return (uintptr_t)t[i].function;
    return 0;
}
static void read_exact(const char *path,float *out,size_t count) {
    FILE *f=fopen(path,"rb");assert(f);assert(fread(out,sizeof(float),count,f)==count);
    assert(fgetc(f)==EOF && !ferror(f));assert(fclose(f)==0);
}
int main(void) {
    char dir[]="/tmp/odin-audio-tap-XXXXXX"; assert(mkdtemp(dir));
    char prefix[512],guest[600],submitted[600],guest2[600];
    snprintf(prefix,sizeof(prefix),"%s/capture",dir);
    setenv("OSL_AUDIO","none",1);setenv("OSL_AUDIO_TAP",prefix,1);
    setenv("OSL_AUDIO_TAP_SKIP","1",1);setenv("OSL_AUDIO_TAP_BLOCKS","2",1);
    assert(audio_init()==0);int h=audio_open(1,0,0,256,48000,4);assert(h>0);
    OslAudioInput input;assert(runtime_audio_input_format(h,&input)==0);
    assert(input.bytes==256*2*sizeof(float));
    int32_t volumes[8]={16384,32768};assert(audio_volume(h,3,volumes)==0);
    float samples[512];for(size_t i=0;i<512;++i)samples[i]=(i%2 ? -0.5f : 0.5f);
    assert(runtime_audio_output_copy(h,samples,sizeof(samples)-1,&input,0,NULL,NULL)<0);
    assert(runtime_audio_output_copy(h,NULL,0,&input,0,NULL,NULL)==0);
    for(int i=0;i<4;++i)assert(runtime_audio_output_copy(h,samples,sizeof(samples),&input,0,NULL,NULL)==512);
    assert(audio_close(h)==0);
    snprintf(guest,sizeof(guest),"%s.port0.gen1.guest.2ch.f32",prefix);
    snprintf(submitted,sizeof(submitted),"%s.port0.gen1.converted.2ch.f32",prefix);
    float raw[1024],out[1024];read_exact(guest,raw,1024);read_exact(submitted,out,1024);
    for(size_t i=0;i<1024;++i){assert(raw[i]==samples[i%512]);assert(out[i]==(i%2 ? -0.5f : 0.25f));}
    h=audio_open(1,0,0,256,48000,4);assert(h>0);assert(audio_close(h)==0);
    snprintf(guest2,sizeof(guest2),"%s.port0.gen2.guest.2ch.f32",prefix);
    assert(access(guest2,F_OK)==0 && access(guest,F_OK)==0);
    setenv("OSL_AUDIO_TAP_SKIP","0",1);setenv("OSL_AUDIO_TAP_BLOCKS","1",1);
    h=audio_open(1,0,0,256,48000,5);assert(h>0);
    OslAudioInput eight;assert(runtime_audio_input_format(h,&eight)==0);
    float surround[256*8],before[256*8],after[256*8];
    for(size_t i=0;i<256*8;++i)surround[i]=(float)(i%8)/8.0f;
    assert(runtime_audio_output_copy(h,surround,sizeof(surround),&eight,0,NULL,NULL)==256*8);
    assert(audio_close(h)==0);
    snprintf(guest,sizeof(guest),"%s.port0.gen3.guest.8ch.f32",prefix);
    snprintf(submitted,sizeof(submitted),"%s.port0.gen3.converted.8ch.f32",prefix);
    read_exact(guest,before,256*8);read_exact(submitted,after,256*8);
    const int remap[8]={0,1,2,3,6,7,4,5};
    for(size_t f=0;f<256;++f)for(int c=0;c<8;++c){assert(before[f*8+c]==surround[f*8+c]);assert(after[f*8+remap[c]]==surround[f*8+c]);}
    setenv("OSL_AUDIO_TAP_BLOCKS","-1",1);assert(tap_number("OSL_AUDIO_TAP_BLOCKS",17,100)==17);
    setenv("OSL_AUDIO_TAP_BLOCKS","101",1);assert(tap_number("OSL_AUDIO_TAP_BLOCKS",17,100)==17);
    unsetenv("OSL_AUDIO");h=audio_open(1,0,0,256,48000,4);assert(h>0);assert(audio_close(h)==0);
    snprintf(guest,sizeof(guest),"%s.port0.gen4.guest.2ch.f32",prefix);assert(access(guest,F_OK)!=0);
    setenv("OSL_AUDIO","none",1);setenv("OSL_AUDIO_TAP_BLOCKS","1",1);
    h=audio_open(1,0,0,256,48000,5);assert(h>0);
    /* Force the timer sink's stereo conversion path without an audio device. */
    ports[0].downmix=1;assert(runtime_audio_input_format(h,&eight)==0);
    for(size_t i=0;i<256*8;++i)surround[i]=(float)(i%8)/32.0f;
    assert(runtime_audio_output_copy(h,surround,sizeof(surround),&eight,0,NULL,NULL)==256*8);assert(audio_close(h)==0);
    /* The output filename is set at open, so this forced path retains 8ch in
     * the name even though its verified contents are two channels. */
    snprintf(submitted,sizeof(submitted),"%s.port0.gen5.converted.8ch.f32",prefix);
    float stereo[256*2];read_exact(submitted,stereo,256*2);
    for(size_t f=0;f<256;++f){assert(fabsf(stereo[f*2]-(0.70710678f*(2.0f+6.0f+4.0f)/32.0f))<1e-6f);assert(fabsf(stereo[f*2+1]-(1.0f+0.70710678f*(2.0f+7.0f+5.0f))/32.0f)<1e-6f);}
    runtime_audio_end_session();
    DIR *files=opendir(dir);assert(files);struct dirent *entry;
    while((entry=readdir(files))) {
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        char path[1024];int n=snprintf(path,sizeof(path),"%s/%s",dir,entry->d_name);
        assert(n>0 && (size_t)n<sizeof(path) && unlink(path)==0);
    }
    assert(closedir(files)==0 && rmdir(dir)==0);
    puts("Paired tap: skip, cap, pre-volume data, conversion and reopen passed");
}
