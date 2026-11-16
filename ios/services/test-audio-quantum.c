/* Real CoreAudio configuration probe; submits only digital silence.
 * Compile/link with SDL3 and src/ includes on macOS. An optional
 * OSL_AUDIO_IMPLEMENTATION string macro selects an archived baseline source.
 * argv[1] is the expected device frame count (the driver must honor the hint),
 * and optional argv[2] selects the diagnostic "muted" mode.
 * This tests the output quantum and environment precedence, not game sound. */
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
    assert(argc==2 || argc==3);
    unsetenv("OSL_AUDIO");
    int muted=argc==3 && !strcmp(argv[2],"muted");
    if (muted) setenv("OSL_AUDIO","muted",1);
    assert(audio_init()==0);
    int handle=audio_open(1,0,0,256,48000,5);
    assert(handle>0 && ports[0].stream);
    SDL_AudioSpec spec;int frames=0;
    assert(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(ports[0].stream),&spec,&frames));
    assert(!strcmp(SDL_GetCurrentAudioDriver(),"coreaudio"));
    assert(frames==atoi(argv[1]));
    assert(SDL_GetAudioStreamGain(ports[0].stream)==(muted ? 0.0f : 1.0f));
    float silence[256*8]={0};OslAudioInput input;
    assert(runtime_audio_input_format(handle,&input)==0 && input.bytes==sizeof(silence));
    for(unsigned i=0;i<32;++i)
        assert(runtime_audio_output_copy(handle,silence,sizeof(silence),&input,1,NULL,NULL)==256*8);
    assert(audio_close(handle)==0);
    runtime_audio_end_session();
    printf("QUANTUM_RESULT:{\"device_frames\":%d,\"rate\":%d,\"channels\":%d,\"host_muted\":%s,\"silent_guest_buffers\":32,\"game_audio_accepted\":false}\n",frames,spec.freq,spec.channels,muted?"true":"false");
}
