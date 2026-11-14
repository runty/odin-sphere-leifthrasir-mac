#pragma once
#include "../cpu/runtime.h"
#include "wait_scope.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_set>
extern "C" uintptr_t runtime_eventflag_resolve(const char *);
extern "C" uint64_t runtime_eventflag_live_count();

// Session-owned, opaque kernel tokens; no native object pointer crosses the ABI.
class MobileEventFlags {
    std::mutex mutex_;
    std::unordered_set<uint64_t> owned_;
    template<class F> static F function(const char *name) {
        return reinterpret_cast<F>(runtime_eventflag_resolve(name));
    }
public:
    static constexpr uint32_t invalid=0x80020016u, missing=0x80020003u,
                              no_memory=0x8002000cu, timed_out=0x8002003cu;
    ~MobileEventFlags() {
        while (!owned_.empty()) {
            const auto id=*owned_.begin(); owned_.erase(id);
            function<int32_t (*)(void *)>("sceKernelDeleteEventFlag")(reinterpret_cast<void *>(id));
        }
    }
    bool owns(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_); return owned_.count(id)!=0;
    }
    size_t count() { std::lock_guard<std::mutex> lock(mutex_); return owned_.size(); }
    uint32_t create(const char *name,uint32_t attr,uint64_t bits,uint64_t &id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owned_.size()>=4096) return no_memory;
        void *native=nullptr;
        uint32_t result=function<int32_t (*)(void **,const char *,uint32_t,uint64_t,const void *)>(
            "sceKernelCreateEventFlag")(&native,name,attr,bits,nullptr);
        if (!result) {
            id=reinterpret_cast<uint64_t>(native);
            try { owned_.insert(id); }
            catch (...) {
                function<int32_t (*)(void *)>("sceKernelDeleteEventFlag")(native);
                return no_memory;
            }
        }
        return result;
    }
    uint32_t remove(uint64_t id) {
        { std::lock_guard<std::mutex> lock(mutex_); if (!owned_.erase(id)) return missing; }
        return function<int32_t (*)(void *)>("sceKernelDeleteEventFlag")(reinterpret_cast<void *>(id));
    }
    uint32_t update(const char *name,uint64_t id,uint64_t bits) {
        if (!owns(id)) return missing;
        return function<int32_t (*)(void *,uint64_t)>(name)(reinterpret_cast<void *>(id),bits);
    }
    uint32_t poll(uint64_t id,uint64_t bits,uint32_t mode,uint64_t *out) {
        if (!owns(id)) return missing;
        return function<int32_t (*)(void *,uint64_t,uint32_t,uint64_t *)>("sceKernelPollEventFlag")(
            reinterpret_cast<void *>(id),bits,mode,out);
    }
    uint32_t wait(uint64_t id,uint64_t bits,uint32_t mode,uint64_t *out,uint32_t *timeout) {
        if (!owns(id)) return missing;
        return function<int32_t (*)(void *,uint64_t,uint32_t,uint64_t *,uint32_t *)>("sceKernelWaitEventFlag")(
            reinterpret_cast<void *>(id),bits,mode,out,timeout);
    }
    uint32_t cancel(uint64_t id,uint64_t bits,int32_t *out) {
        if (!owns(id)) return missing;
        return function<int32_t (*)(void *,uint64_t,int32_t *)>("sceKernelCancelEventFlag")(
            reinterpret_cast<void *>(id),bits,out);
    }
};

namespace GuestEvents {
inline bool call(const char *name,State &s,Memory &m,MobileEventFlags &owner,uint64_t &result,GuestWaitScope scope={}) {
    const bool create=!std::strcmp(name,"sceKernelCreateEventFlag"),
        remove=!std::strcmp(name,"sceKernelDeleteEventFlag"),
        set=!std::strcmp(name,"sceKernelSetEventFlag"),clear=!std::strcmp(name,"sceKernelClearEventFlag"),
        poll=!std::strcmp(name,"sceKernelPollEventFlag"),wait=!std::strcmp(name,"sceKernelWaitEventFlag"),
        cancel=!std::strcmp(name,"sceKernelCancelEventFlag");
    if (!(create || remove || set || clear || poll || wait || cancel)) return false;
    const uint64_t first=s.gpr.rdi.qword,second=s.gpr.rsi.qword;
    result=0;
    if (create) {
        if (!first || s.gpr.r8.qword || (s.gpr.rdx.dword&~0x33u)) {
            result=MobileEventFlags::invalid; return true;
        }
        auto output=guestPointer(m,first,8,true); if (!output) return true;
        char label[32]{};
        if (second) for(unsigned i=0;i<31;++i) {
            auto byte=guestPointer(m,second+i,1); if (!byte) return true;
            label[i]=static_cast<char>(*byte); if (!*byte) break;
        }
        uint64_t id=0;
        result=owner.create(second ? label : nullptr,s.gpr.rdx.dword,s.gpr.rcx.qword,id);
        if (!result) std::memcpy(output,&id,8);
        return true;
    }
    if (!owner.owns(first)) { result=MobileEventFlags::missing; return true; }
    if (remove) { result=owner.remove(first); return true; }
    if (set || clear) { result=owner.update(name,first,second); return true; }
    if (cancel) {
        int32_t count=0; uint8_t *output=nullptr;
        if (s.gpr.rdx.qword) {
            output=guestPointer(m,s.gpr.rdx.qword,4,true); if (!output) return true;
            std::memcpy(&count,output,4);
        }
        result=owner.cancel(first,second,output ? &count : nullptr);
        if (output) std::memcpy(output,&count,4);
        return true;
    }
    uint64_t observed=0; uint8_t *output=nullptr,*timeout_output=nullptr;
    if (s.gpr.rcx.qword) {
        output=guestPointer(m,s.gpr.rcx.qword,8,true); if (!output) return true;
        std::memcpy(&observed,output,8);
    }
    uint32_t requested=0;
    if (wait && s.gpr.r8.qword) {
        timeout_output=guestPointer(m,s.gpr.r8.qword,4,true); if (!timeout_output) return true;
        std::memcpy(&requested,timeout_output,4);
    }
    if (poll) result=owner.poll(first,second,s.gpr.rdx.dword,output ? &observed : nullptr);
    else {
        const auto now=std::chrono::steady_clock::now();
        if (now>=m.deadline) { stopGuest(m,"guest execution deadline exceeded"); return true; }
        const auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(m.deadline-now).count();
        uint32_t bounded=static_cast<uint32_t>(std::min<int64_t>(UINT32_MAX,std::max<int64_t>(1,remaining)));
        if (timeout_output) bounded=std::min(bounded,requested);
        const uint32_t cap=bounded;
        scope.before();
        result=owner.wait(first,second,s.gpr.rdx.dword,output ? &observed : nullptr,&bounded);
        scope.after();
        if(const char *reason=guestInterruption(m)){stopGuest(m,reason);return true;}
        if (result==MobileEventFlags::timed_out && (!timeout_output || cap<requested)) {
            // The host guard expired, not the guest's requested timeout. Stop
            // outside native locks/scopes rather than fabricate a guest return.
            stopGuest(m,"guest execution deadline exceeded"); return true;
        }
        if (timeout_output) {
            const uint32_t spent=cap-bounded;
            requested-=std::min(requested,spent);
            timeout_output=guestPointer(m,s.gpr.r8.qword,4,true);if(!timeout_output)return true;
            std::memcpy(timeout_output,&requested,4);
        }
    }
    if (output) {output=guestPointer(m,s.gpr.rcx.qword,8,true);if(!output)return true;std::memcpy(output,&observed,8);}
    return true;
}
} // namespace GuestEvents
