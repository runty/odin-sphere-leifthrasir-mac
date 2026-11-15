/* Host-side regression for PCM snapshots racing a port close/reopen.
 * Link the real SDL audio implementation. Use the timer sink for deterministic
 * interleaving and the SDL dummy driver for a real queued-stream pass. */
#include "runtime.h"
#include "runtime_audio_host.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>

uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(name,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
typedef int32_t (ABI *Init)(void);
typedef int32_t (ABI *Open)(int32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t);
typedef int32_t (ABI *Close)(int32_t);
static Close close_port;
static OslAudioInput format;
static int32_t handle;
static float samples[2048*2];
static atomic_int close_entered,waiting;
static int callback_count;
static int cancel_during_wait(void *unused) {
    (void)unused;
    if (++callback_count==1) return 0;
    atomic_store(&waiting,1);
    while (!atomic_load(&close_entered)) sched_yield();
    return 1;
}
static void *close_while_output_waits(void *unused) {
    (void)unused;
    while (!atomic_load(&waiting)) sched_yield();
    atomic_store(&close_entered,1);
    assert(close_port(handle)==0);
    return NULL;
}
int main(void) {
    Init init=(Init)runtime_audio_resolve("sceAudioOutInit");
    Open open_port=(Open)runtime_audio_resolve("sceAudioOutOpen");
    close_port=(Close)runtime_audio_resolve("sceAudioOutClose");
    assert(init()==0);
    handle=open_port(1,0,0,2048,48000,4);
    assert(handle>0 && runtime_audio_input_format(handle,&format)==0);
    assert(format.bytes==sizeof(samples));
    assert(runtime_audio_output_copy(handle,samples,1,&format,1,NULL,NULL)<0);
    assert(runtime_audio_output_copy(handle,NULL,0,&format,1,NULL,NULL)==0);
    pthread_t closer;
    assert(!pthread_create(&closer,NULL,close_while_output_waits,NULL));
    /* First write establishes the 42.7 ms port period. */
    assert(runtime_audio_output_copy(handle,samples,sizeof(samples),&format,1,NULL,NULL)==4096);
    assert(runtime_audio_output_copy(handle,samples,sizeof(samples),&format,1,cancel_during_wait,NULL)==OSL_AUDIO_INTERRUPTED);
    assert(!pthread_join(closer,NULL));
    /* Reopen with identical PCM layout: size alone cannot distinguish ports. */
    assert(open_port(1,0,0,2048,48000,4)==handle);
    assert(runtime_audio_output_copy(handle,samples,sizeof(samples),&format,1,NULL,NULL)<0);
    OslAudioInput refreshed;
    assert(runtime_audio_input_format(handle,&refreshed)==0);
    assert(refreshed.generation!=format.generation);
    assert(runtime_audio_output_copy(handle,samples,sizeof(samples),&refreshed,0,NULL,NULL)==4096);
    assert(close_port(handle)==0);
    puts("PCM bounds, cancellation with concurrent close and recycled handle: passed");
}
