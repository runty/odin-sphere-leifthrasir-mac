#pragma once
#include "../cpu/runtime.h"
#include "wait_scope.h"
#include "../../src/runtime_audio_host.h"
#include <array>
#include <cstring>
#include <vector>

extern "C" uintptr_t runtime_audio_resolve(const char *);
extern "C" void runtime_audio_end_session(void);

// The runner serializes guest service lookups. Output owns a copy of all guest
// data before releasing the guest locks. Port generations protect that copy
// against concurrent close/reopen. Destruction follows the guest worker join.
class GuestAudio {
    bool initialized=false;
    std::array<int32_t,25> handles{};
    template<class F> static F resolve(const char *name) {
        return reinterpret_cast<F>(runtime_audio_resolve(name));
    }
    bool owns(int32_t handle) const {
        const unsigned slot=static_cast<unsigned>(handle)&255;
        return handle>0 && slot<handles.size() && handles[slot]==handle;
    }
    struct Buffer {
        int32_t handle;
        OslAudioInput format;
        std::vector<uint8_t> pcm;
    };
    struct Wait {
        GuestWaitScope scope;
        explicit Wait(GuestWaitScope s):scope(s){scope.before();}
        ~Wait(){scope.after();}
    };
    static int interrupted(void *opaque) {
        const auto &m=*static_cast<Memory *>(opaque);
        return guestInterruption(m) || std::chrono::steady_clock::now()>=m.deadline;
    }
public:
    void finish() {
        if (!initialized) return;
        for (auto &handle:handles) if (handle) {
            resolve<int32_t(*)(int32_t)>("sceAudioOutClose")(handle);
            handle=0;
        }
        runtime_audio_end_session();
        initialized=false;
    }
    ~GuestAudio(){finish();}
    static bool call(void *opaque,const char *name,const uint64_t a[6],Memory &m,uint64_t &result,GuestWaitScope locks) {
        return static_cast<GuestAudio *>(opaque)->dispatch(name,a,m,result,locks);
    }
    bool dispatch(const char *name,const uint64_t a[6],Memory &m,uint64_t &result,GuestWaitScope locks) {
        if (std::strncmp(name,"sceAudioOut",11)) return false;
        if (!std::strcmp(name,"sceAudioOutInit")) {
            result=uint32_t(resolve<int32_t(*)()>(name)());
            if (!result) initialized=true;
            return true;
        }
        if (!std::strcmp(name,"sceAudioOutOpen")) {
            if (!initialized) {result=0x8026000fu;return true;}
            const auto handle=resolve<int32_t(*)(int32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t)>(name)(a[0],a[1],a[2],a[3],a[4],a[5]);
            if (handle>0) handles.at(unsigned(handle)&255)=handle;
            result=uint32_t(handle);return true;
        }
        if (!std::strcmp(name,"sceAudioOutClose")) {
            const int32_t handle=a[0];
            if (!owns(handle)) {result=invalid(handle);return true;}
            result=uint32_t(resolve<int32_t(*)(int32_t)>(name)(handle));
            if (!result) handles[unsigned(handle)&255]=0;
            return true;
        }
        const bool batch=!std::strcmp(name,"sceAudioOutOutputs");
        if (!batch && std::strcmp(name,"sceAudioOutOutput")) return false;
        const uint32_t count=batch ? uint32_t(a[1]) : 1;
        if (batch && !a[0]) {result=0x80260004u;return true;}
        if (!count || count>handles.size()) {result=0x80260005u;return true;}
        const uint8_t *table=batch ? guestPointer(m,a[0],count*16) : nullptr;
        if (batch && !table) return true;
        std::vector<Buffer> buffers(count);
        for (unsigned i=0;i<count;++i) {
            auto &b=buffers[i]; b.handle=int32_t(a[0]);uint64_t address=a[1];
            if (batch) {
                std::memcpy(&b.handle,table+i*16,4);
                std::memcpy(&address,table+i*16+8,8);
            }
            if (!owns(b.handle)) {result=invalid(b.handle);return true;}
            result=uint32_t(runtime_audio_input_format(b.handle,&b.format));
            if (result) return true;
            if (address) {
                const auto *pcm=guestPointer(m,address,b.format.bytes);
                if (!pcm) return true;
                b.pcm.assign(pcm,pcm+b.format.bytes);
            }
        }
        int32_t status=0;
        {
            Wait unlocked(locks);
            for (unsigned i=0;i<count && status>=0;++i) {
                const auto &b=buffers[i];
                status=runtime_audio_output_copy(b.handle,b.pcm.empty()?nullptr:b.pcm.data(),b.pcm.size(),
                                                 &b.format,i==0,interrupted,&m);
            }
        }
        if (status==OSL_AUDIO_INTERRUPTED) {
            const char *reason=guestInterruption(m);
            stopGuest(m,reason?reason:"guest execution deadline exceeded");
        } else result=uint32_t(status);
        return true;
    }
private:
    static uint32_t invalid(int32_t handle) {
        return (handle&0x3f000000)==0x20000000 && (handle&255)<25 ? 0x80260001u : 0x80260003u;
    }
};
