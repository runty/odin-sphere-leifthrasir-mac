#include "guest_audio.h"
#include <cassert>
#include <cstdio>
#include <memory>

int main() {
    GuestAudio audio;
    auto m=std::make_unique<Memory>();
    std::array<uint8_t,2048> guest{};
    constexpr uint64_t base=0x30000;
    mapRegion(*m,base,guest.data(),guest.size());
    std::array<uint64_t,6> args{};
    uint64_t result=0;
    struct Handoff { unsigned released=0,acquired=0; } handoff;
    GuestWaitScope scope{&handoff,[](void *p){++static_cast<Handoff *>(p)->released;},
                                  [](void *p){++static_cast<Handoff *>(p)->acquired;}};
    auto call=[&](const char *name) {
        return audio.dispatch(name,args.data(),*m,result,scope);
    };
    assert(call("sceAudioOutInit") && result==0);
    args={1,0,0,256,48000,1};
    assert(call("sceAudioOutOpen") && int32_t(result)>0);
    int32_t port=result;
    uint64_t pcm=base+128;
    for(unsigned i=0;i<2;++i){
        std::memcpy(guest.data()+1+i*16,&port,4);
        std::memcpy(guest.data()+9+i*16,&pcm,8);
    }
    args={base+1,2};
    assert(call("sceAudioOutOutputs") && result==512 && handoff.acquired==1 && handoff.released==1);
    pcm=base+2047;
    std::memcpy(guest.data()+25,&pcm,8);
    assert(call("sceAudioOutOutputs") && !m->fault.empty() && handoff.released==1);
    m->fault={};args={uint64_t(port),base+128};
    std::atomic<bool> stop{true};m->stop_requested=&stop;
    assert(call("sceAudioOutOutput") && m->fault=="guest execution interrupted" && handoff.acquired==2 && handoff.released==2);
    audio.finish();stop=false;m->fault={};
    assert(call("sceAudioOutInit") && result==0);
    assert(call("sceAudioOutClose") && result==0x80260001u);
    puts("Unaligned batch, exact PCM bounds, lease handoff, stop and session restart: passed");
}
