#pragma once
#include "../cpu/runtime.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <unordered_map>

// A bounded session-owned condition registry. Guest tokens never expose native
// pthread objects. The caller must join all workers before destroying the owner.
class MobileConditions {
    struct Condition {
        pthread_cond_t native;
        unsigned waiters=0;
        bool initialized=false;
        ~Condition(){if(initialized)pthread_cond_destroy(&native);}
    };
    std::mutex mutex_;
    std::unordered_map<uint64_t,std::shared_ptr<Condition>> conditions_;
    inline static std::atomic<uint64_t> next_{0x5c00000000000000ull};
    std::shared_ptr<Condition> find(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it=conditions_.find(id);return it==conditions_.end() ? nullptr : it->second;
    }
public:
    size_t count(){std::lock_guard<std::mutex> lock(mutex_);return conditions_.size();}
    unsigned waiters(uint64_t id){std::lock_guard<std::mutex> lock(mutex_);auto it=conditions_.find(id);return it==conditions_.end() ? 0 : it->second->waiters;}
    int create(uint64_t &id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(conditions_.size()>=4096)return ENOMEM;
        try {
            auto object=std::make_shared<Condition>();
            const int error=pthread_cond_init(&object->native,nullptr);if(error)return error;
            object->initialized=true;
            const auto token=next_.fetch_add(1,std::memory_order_relaxed);
            conditions_.emplace(token,std::move(object));id=token;return 0;
        } catch(const std::bad_alloc &){return ENOMEM;}
    }
    int remove(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it=conditions_.find(id);if(it==conditions_.end())return EINVAL;
        if(it->second->waiters)return EBUSY;
        conditions_.erase(it);return 0;
    }
    int notify(uint64_t id,bool all) {
        auto object=find(id);if(!object)return EINVAL;
        return all ? pthread_cond_broadcast(&object->native) : pthread_cond_signal(&object->native);
    }
    struct WaitResult { int error; bool host_deadline; };
    WaitResult wait(uint64_t id,pthread_mutex_t *mutex,bool timed,uint32_t usec,
                    std::chrono::steady_clock::time_point deadline) {
        if(!mutex)return {EINVAL,false};
        std::shared_ptr<Condition> object;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it=conditions_.find(id);if(it==conditions_.end())return {EINVAL,false};
            object=it->second;++object->waiters;
        }
        struct Leaving {
            MobileConditions &owner;Condition &object;
            ~Leaving(){std::lock_guard<std::mutex> lock(owner.mutex_);--object.waiters;}
        } leaving{*this,*object};
        using Clock=std::chrono::steady_clock;
        const auto requested=timed ? Clock::now()+std::chrono::microseconds(usec) : Clock::time_point::max();
        for(;;) {
            const auto now=Clock::now();
            if(now>=deadline)return {0,true};
            const auto end=std::min(requested,deadline);
            const auto ns=end>now ? std::chrono::duration_cast<std::chrono::nanoseconds>(end-now).count() : 0;
            timespec interval{time_t(ns/1000000000),long(ns%1000000000)};
#ifdef __APPLE__
            const int error=pthread_cond_timedwait_relative_np(&object->native,mutex,&interval);
#else
            timespec absolute{};clock_gettime(CLOCK_REALTIME,&absolute);
            absolute.tv_sec+=interval.tv_sec;absolute.tv_nsec+=interval.tv_nsec;
            if(absolute.tv_nsec>=1000000000){++absolute.tv_sec;absolute.tv_nsec-=1000000000;}
            const int error=pthread_cond_timedwait(&object->native,mutex,&absolute);
#endif
            if(error!=ETIMEDOUT)return {error,false}; // Spurious wakeups are valid POSIX wakeups.
            const auto after=Clock::now();
            if(timed && after>=requested && requested<=deadline)return {ETIMEDOUT,false};
            if(after>=deadline)return {0,true};
        }
    }
};
namespace GuestConditions {
inline bool error(Memory &memory,int native,uint64_t &result) {
    unsigned code=0;
    switch(native) {
    case 0:break;case EPERM:code=1;break;case ENOMEM:code=12;break;case EBUSY:code=16;break;
    case EINVAL:code=22;break;case EAGAIN:code=35;break;case EDEADLK:code=11;break;case ETIMEDOUT:code=60;break;
    default:stopGuest(memory,"unsupported native condition error");return false;
    }
    result=code ? 0x80020000u|code : 0;return true;
}
template<class MutexLookup>
bool call(const char *name,State &state,Memory &memory,MobileConditions &owner,MutexLookup mutex_lookup,uint64_t &result) {
    const bool init=!std::strcmp(name,"scePthreadCondInit"),remove=!std::strcmp(name,"scePthreadCondDestroy"),
        signal=!std::strcmp(name,"scePthreadCondSignal"),broadcast=!std::strcmp(name,"scePthreadCondBroadcast"),
        wait=!std::strcmp(name,"scePthreadCondWait"),timed=!std::strcmp(name,"scePthreadCondTimedwait");
    if(!(init || remove || signal || broadcast || wait || timed))return false;
    result=0;
    const uint64_t slot=state.gpr.rdi.qword;
    if(!slot){error(memory,EINVAL,result);return true;}
    // Lazy initialization and destroy may write the opaque slot; validate the
    // entire span before creating/deleting any native object.
    auto output=guestPointer(memory,slot,8,init || remove);if(!output)return true;
    uint64_t id=0;std::memcpy(&id,output,8);
    if(init) {
        if(state.gpr.rsi.qword){error(memory,EINVAL,result);return true;} // default attributes only
        if(state.gpr.rdx.qword)for(unsigned i=0;i<33;++i) {
            auto byte=guestPointer(memory,state.gpr.rdx.qword+i,1);if(!byte)return true;
            if(!*byte)break;
            if(i==32){error(memory,EINVAL,result);return true;}
        }
        uint64_t token=0;const int native=owner.create(token);
        if(error(memory,native,result) && !native)std::memcpy(output,&token,8);
        return true;
    }
    if(remove) {
        const int native=id<2 ? 0 : owner.remove(id);
        if(error(memory,native,result) && !native){const uint64_t zero=0;std::memcpy(output,&zero,8);}
        return true;
    }
    pthread_mutex_t *mutex=nullptr;
    if(wait || timed) {
        if(!state.gpr.rsi.qword){error(memory,EINVAL,result);return true;}
        auto input=guestPointer(memory,state.gpr.rsi.qword,8);if(!input)return true;
        uint64_t token=0;std::memcpy(&token,input,8);mutex=mutex_lookup(token);
        if(!mutex){error(memory,EINVAL,result);return true;}
    }
    if(id<2) {
        output=guestPointer(memory,slot,8,true);if(!output)return true;
        const int native=owner.create(id);
        if(native){error(memory,native,result);return true;}
        std::memcpy(output,&id,8);
    }
    if(signal || broadcast){error(memory,owner.notify(id,broadcast),result);return true;}
    const auto outcome=owner.wait(id,mutex,timed,state.gpr.rdx.dword,memory.deadline);
    if(outcome.host_deadline)stopGuest(memory,"condition wait exceeded host execution deadline");
    else error(memory,outcome.error,result);
    return true;
}
}
