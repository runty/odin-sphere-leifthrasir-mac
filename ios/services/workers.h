#pragma once
#include "thread_storage.h"
#include <condition_variable>
#include <functional>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cerrno>
extern "C" Memory *__remill_atomic_end(Memory *);

struct GuestThreadAttributes {
    uint64_t stack=1024*1024,stack_address=0,affinity=0x7f;
    int priority=700,detached=0,policy=1,inherit=4;
};
// Host-thread ownership for the AOT frontend. A body must use checked CPU
// checkpoints / bounded waits, and unwind its native service scopes on stop.
// Cancel is pending/deferred; this registry does not asynchronously cancel a
// host pthread or set the checked CPU's instruction-level cancellation flag.
class MobileWorkers {
public:
    struct Worker {
        Memory memory;
        State state{};
        std::unique_ptr<ThreadStorage> storage;
        uint64_t id=0,entry=0,argument=0,result=0;
        GuestThreadAttributes attributes;
        std::atomic<bool> cancel_pending{false},finished{false};
        bool exit_requested=false;
        const char *missing_service="";
    private:
        friend class MobileWorkers;
        std::thread native;
        std::mutex lifecycle,join_mutex;
        std::condition_variable changed;
        bool detached=false,guest_joined=false,join_claimed=false;
        void joinNative(){std::lock_guard<std::mutex> lock(join_mutex);if(native.joinable())native.join();}
    };
    using Body=std::function<void(Worker &)>;
private:
    std::mutex mutex_;
    std::unordered_map<uint64_t,std::shared_ptr<Worker>> workers_;
    Memory &parent_;
    const ModuleTlsTemplates &templates_;
    bool identity_;
    std::atomic<bool> &stop_;
    inline static std::atomic<uint64_t> next_{0x44000000};
    std::atomic<unsigned> started_{0};
public:
    MobileWorkers(Memory &parent,const ModuleTlsTemplates &templates,bool identity,std::atomic<bool> &stop)
        :parent_(parent),templates_(templates),identity_(identity),stop_(stop){}
    MobileWorkers(const MobileWorkers &)=delete;
    MobileWorkers &operator=(const MobileWorkers &)=delete;
    ~MobileWorkers(){stopAndJoin();}
    std::shared_ptr<Worker> find(uint64_t id){std::lock_guard<std::mutex> lock(mutex_);auto it=workers_.find(id);return it==workers_.end() ? nullptr : it->second;}
    std::vector<std::shared_ptr<Worker>> snapshot(){std::lock_guard<std::mutex> lock(mutex_);std::vector<std::shared_ptr<Worker>> result;for(const auto &item:workers_)result.push_back(item.second);return result;}
    unsigned started()const{return started_.load();}
    // The caller validates executable entry/guest ABI before this boundary.
    int spawn(uint64_t entry,uint64_t argument,const GuestThreadAttributes &attributes,Body body,uint64_t &id) {
        if(stop_.load())return EAGAIN;
        std::lock_guard<std::mutex> lock(mutex_);
        if(stop_.load())return EAGAIN;
        unsigned active=0;
        for(const auto &entry:workers_)if(!entry.second->finished.load())++active;
        if(active>=64 || workers_.size()>=4096)return EAGAIN;
        try {
            auto worker=std::make_shared<Worker>();
            worker->memory.mappings=parent_.mappings;worker->memory.external_pointer=parent_.external_pointer;
            worker->memory.checkpoint_limit=parent_.checkpoint_limit;
            worker->memory.deadline=parent_.deadline;worker->memory.stop_requested=&stop_;
            worker->id=next_.fetch_add(1);worker->entry=entry;worker->argument=argument;
            worker->attributes=attributes;worker->detached=attributes.detached;
            // Every worker gets a disjoint explicit-address interval. Native
            // identity mode instead checks the actual aligned host addresses.
            const uint64_t base=0x100000000ull+workers_.size()*0x10000000ull;
            worker->storage=std::make_unique<ThreadStorage>(worker->memory,templates_,identity_,base);
            if(!worker->storage->makeStack(attributes.stack))return ENOMEM;
            worker->attributes.stack_address=worker->storage->stackAddress();
            workers_.emplace(worker->id,worker);
            try {
                worker->native=std::thread([this,worker,body=std::move(body)] {
                    ++started_;
                    try {body(*worker);}catch(...){worker->memory.fault="native worker callback failed";stop_=true;}
                    worker->memory.recovery=nullptr;
                    if(worker->memory.atomic_held)__remill_atomic_end(&worker->memory);
                    // The frontend body owns its guest checkpoint and mutex
                    // cleanup. No native C++ scope is bypassed by pthread_exit.
                    {std::lock_guard<std::mutex> done(worker->lifecycle);worker->finished.store(true,std::memory_order_release);}
                    worker->changed.notify_all();
                });
            } catch(...){workers_.erase(worker->id);throw;}
            id=worker->id;return 0;
        } catch(const std::bad_alloc &){return ENOMEM;}
          catch(const std::system_error &){return EAGAIN;}
    }
    int cancel(uint64_t id){auto worker=find(id);if(!worker)return ESRCH;worker->cancel_pending=true;worker->changed.notify_all();return 0;}
    int detach(uint64_t id){auto worker=find(id);if(!worker)return ESRCH;std::lock_guard<std::mutex> lock(worker->lifecycle);if(worker->detached || worker->guest_joined || worker->join_claimed)return EINVAL;worker->detached=true;return 0;}
    struct Join {int error=0;const char *stop=nullptr;uint64_t result=0;};
    Join join(uint64_t id,uint64_t caller_id,Memory &caller) {
        auto worker=find(id);if(!worker)return {ESRCH};if(id==caller_id)return {EDEADLK};
        {
            std::unique_lock<std::mutex> lock(worker->lifecycle);
            if(worker->detached || worker->guest_joined || worker->join_claimed)return {EINVAL};
            worker->join_claimed=true;
            while(!worker->finished.load(std::memory_order_acquire)) {
                if(const char *reason=guestInterruption(caller)){worker->join_claimed=false;return {0,reason};}
                const auto now=std::chrono::steady_clock::now();
                if(now>=caller.deadline){worker->join_claimed=false;return {0,"guest join exceeded host execution deadline"};}
                worker->changed.wait_until(lock,std::min(caller.deadline,now+std::chrono::milliseconds(5)));
            }
        }
        worker->joinNative();
        {std::lock_guard<std::mutex> lock(worker->lifecycle);worker->guest_joined=true;worker->join_claimed=false;}
        return {0,nullptr,worker->result};
    }
    // No service/global-memory lock may be held while joining. Keep all CPU
    // state/stack/TLS storage after join until the frontend drains its GPU.
    void stopAndJoin() {
        stop_=true;
        std::array<std::shared_ptr<Worker>,4096> owned;size_t count=0;
        {std::lock_guard<std::mutex> lock(mutex_);for(const auto &item:workers_)owned[count++]=item.second;}
        for(size_t i=0;i<count;++i)owned[i]->changed.notify_all();
        for(size_t i=0;i<count;++i)owned[i]->joinNative();
    }
};
