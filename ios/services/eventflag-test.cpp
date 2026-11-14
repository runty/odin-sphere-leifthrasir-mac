#include "eventflag.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
extern "C" unsigned runtime_eventflag_waiters(uint64_t);

static unsigned checks;
static void require(bool ok) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"event flag check %u failed\n",checks); std::abort(); }
}
static void parked(MobileEventFlags &flags,uint64_t id) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(flags.poll(id,1,1,nullptr)!=0x80020001u) {
        if(std::chrono::steady_clock::now()>end) std::abort();
        std::this_thread::yield();
    }
}
int main() {
    const auto initial=runtime_eventflag_live_count();
    MobileEventFlags flags;
    uint64_t id=0,result=0;
    require(flags.create("test",0x800,0,id)==MobileEventFlags::invalid && id==0);
    require(flags.create("test",0x10,1,id)==0 && id && id<(uint64_t(1)<<40));
    require(flags.count()==1 && runtime_eventflag_live_count()==initial+1);
    uint32_t status=0,timeout=1000000;
    std::thread waiter([&] { status=flags.wait(id,2,0x11,&result,&timeout); });
    parked(flags,id);
    require(flags.poll(id,1,1,nullptr)==0x80020001u);
    require(flags.update("sceKernelSetEventFlag",id,2)==0);
    waiter.join();
    require(status==0 && result==3 && timeout<=1000000);
    require(flags.poll(id,1,1,&result)==0x80020010u && result==0);
    require(flags.update("sceKernelSetEventFlag",id,0xff)==0);
    require(flags.update("sceKernelClearEventFlag",id,0xf0)==0);
    require(flags.poll(id,0xf0,1,&result)==0 && result==0xf0);
    require(flags.poll(id,0x80,0x22,&result)==0 && result==0xf0);
    require(flags.poll(id,0x80,2,&result)==0x80020010u && result==0x70);
    require(flags.remove(id)==0);
    require(runtime_eventflag_live_count()==initial);

    require(flags.create("cancel",0x10,0,id)==0);
    timeout=1000000; result=0;
    waiter=std::thread([&] { status=flags.wait(id,1,1,&result,&timeout); });
    parked(flags,id);
    int32_t count=-1;
    require(flags.cancel(id,8,&count)==0 && count==1);
    waiter.join();
    require(status==0x80020055u && result==8);
    require(flags.poll(id,8,1,nullptr)==0);
    require(flags.remove(id)==0 && runtime_eventflag_live_count()==initial);

    require(flags.create("delete",0x10,0,id)==0);
    timeout=1000000;
    waiter=std::thread([&] { status=flags.wait(id,1,1,&result,&timeout); });
    parked(flags,id);
    require(flags.remove(id)==0);
    waiter.join();
    require(status==MobileEventFlags::missing && runtime_eventflag_live_count()==initial);
    const auto stale=id;
    require(flags.create("replacement",0x10,0,id)==0 && id!=stale);
    require(flags.update("sceKernelSetEventFlag",stale,1)==MobileEventFlags::missing);
    require(flags.poll(UINT64_MAX,1,1,nullptr)==MobileEventFlags::missing);
    timeout=2000; result=UINT64_MAX;
    require(flags.wait(id,1,1,&result,&timeout)==MobileEventFlags::timed_out && timeout==0 && result==0);
    require(flags.remove(id)==0);

    for(bool destroy:{false,true}) {
        require(flags.create("multi",0x20,0,id)==0);
        std::array<uint32_t,8> statuses{};
        std::array<uint64_t,8> results{};
        std::vector<std::thread> group;
        for(unsigned i=0;i<8;++i) group.emplace_back([&,i] {
            uint32_t limit=2000000;
            statuses[i]=flags.wait(id,1,0x21,&results[i],&limit);
        });
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(runtime_eventflag_waiters(id)!=8) {
            if(std::chrono::steady_clock::now()>end) std::abort();
            std::this_thread::yield();
        }
        if(destroy) require(flags.remove(id)==0);
        else for(unsigned i=0;i<8;++i) require(flags.update("sceKernelSetEventFlag",id,1)==0);
        for(auto &thread:group) thread.join();
        for(unsigned i=0;i<8;++i)
            require(statuses[i]==(destroy ? MobileEventFlags::missing : 0) && (destroy || results[i]==1));
        if(!destroy) require(flags.remove(id)==0);
        require(runtime_eventflag_live_count()==initial);
    }

    std::atomic<unsigned> errors{0};
    for(unsigned run=0;run<200;++run) {
        require(flags.create("race",0x20,0,id)==0);
        const auto handle=id;
        std::atomic<bool> go{false};
        std::atomic<unsigned> ready{0};
        auto native_set=reinterpret_cast<int32_t (*)(void *,uint64_t)>(runtime_eventflag_resolve("sceKernelSetEventFlag"));
        auto native_poll=reinterpret_cast<int32_t (*)(void *,uint64_t,uint32_t,uint64_t *)>(runtime_eventflag_resolve("sceKernelPollEventFlag"));
        std::vector<std::thread> workers;
        for(unsigned i=0;i<4;++i) workers.emplace_back([&] {
            ++ready;
            while(!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for(unsigned j=0;j<100;++j) {
                // Call the production C API directly, without the owner's
                // map lock hiding a lifetime race inside the native core.
                uint32_t a=native_set(reinterpret_cast<void *>(handle),1);
                uint32_t b=native_poll(reinterpret_cast<void *>(handle),1,1,nullptr);
                if((a && a!=MobileEventFlags::missing) ||
                   (b && b!=0x80020010u && b!=MobileEventFlags::missing)) ++errors;
            }
        });
        while(ready.load()!=4) std::this_thread::yield();
        go.store(true,std::memory_order_release);
        require(flags.remove(id)==0);
        for(auto &thread:workers) thread.join();
        require(runtime_eventflag_live_count()==initial);
    }
    require(errors==0);
    {
        MobileEventFlags owned;
        for(unsigned i=0;i<4096;++i) require(owned.create(nullptr,0,0,id)==0);
        require(owned.create(nullptr,0,0,id)==MobileEventFlags::no_memory);
        require(runtime_eventflag_live_count()==initial+4096);
    }
    require(runtime_eventflag_live_count()==initial);
    // Native lookup itself must reject foreign pointers and stale generations.
    auto native_set=reinterpret_cast<int32_t (*)(void *,uint64_t)>(runtime_eventflag_resolve("sceKernelSetEventFlag"));
    require(uint32_t(native_set(reinterpret_cast<void *>(UINT64_MAX),1))==MobileEventFlags::missing);
    require(uint32_t(native_set(reinterpret_cast<void *>(stale),1))==MobileEventFlags::missing);

    Memory memory;
    State state{};
    std::array<uint8_t,4096> bytes{};
    std::array<uint8_t,64> protected_bytes{};
    constexpr uint64_t base=0x20000000;
    mapRegion(memory,base,bytes.data(),bytes.size());
    mapRegion(memory,base+0x2000,protected_bytes.data(),protected_bytes.size(),false);
    auto call=[&](const char *name,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0) {
        memory.fault={}; state=State{};
        state.gpr.rdi.qword=a;state.gpr.rsi.qword=b;state.gpr.rdx.qword=c;
        state.gpr.rcx.qword=d;state.gpr.r8.qword=e;
        uint64_t value=0;
        require(GuestEvents::call(name,state,memory,flags,value));
        return value;
    };
    std::fill(bytes.begin(),bytes.end(),0x7b);
    std::memcpy(bytes.data()+0x200,"guest",6);
    require(call("sceKernelCreateEventFlag",0)==MobileEventFlags::invalid);
    call("sceKernelCreateEventFlag",base+4092);
    require(!memory.fault.empty() && flags.count()==0);
    require(call("sceKernelCreateEventFlag",base+0x101,base+0x200,0x10,0,1)==MobileEventFlags::invalid);
    call("sceKernelCreateEventFlag",base+0x101,UINT64_MAX,0x10);
    require(!memory.fault.empty() && flags.count()==0);
    require(call("sceKernelCreateEventFlag",base+0x101,base+0x200,0x10)==0);
    std::memcpy(&id,bytes.data()+0x101,8);
    require(id && bytes[0x100]==0x7b && bytes[0x109]==0x7b);
    require(call("sceKernelPollEventFlag",UINT64_MAX,1,1,UINT64_MAX)==MobileEventFlags::missing && memory.fault.empty());
    require(call("sceKernelSetEventFlag",id,1)==0);
    call("sceKernelPollEventFlag",id,1,0x21,base+0x2000);
    require(!memory.fault.empty());
    require(call("sceKernelPollEventFlag",id,1,0x21,base+0x301)==0);
    std::memcpy(&result,bytes.data()+0x301,8);
    require(result==1 && bytes[0x300]==0x7b && bytes[0x309]==0x7b);
    require(call("sceKernelPollEventFlag",id,1,1,base+0x301)==0x80020010u);
    timeout=2000;std::memcpy(bytes.data()+0x321,&timeout,4);
    require(call("sceKernelWaitEventFlag",id,1,1,base+0x301,base+0x321)==MobileEventFlags::timed_out);
    std::memcpy(&timeout,bytes.data()+0x321,4);
    require(timeout==0 && bytes[0x320]==0x7b && bytes[0x325]==0x7b);
    memory.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(10);
    call("sceKernelWaitEventFlag",id,1,1);
    require(memory.fault=="guest execution deadline exceeded");
    timeout=1000000;std::memcpy(bytes.data()+0x321,&timeout,4);
    memory.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(10);
    call("sceKernelWaitEventFlag",id,1,1,base+0x301,base+0x321);
    require(memory.fault=="guest execution deadline exceeded");
    std::memcpy(&timeout,bytes.data()+0x321,4);
    require(timeout==1000000); // Host budget expiry did not fabricate a guest timeout return.
    require(call("sceKernelDeleteEventFlag",id)==0 && flags.count()==0);
    require(runtime_eventflag_live_count()==initial);
    std::printf("{\"eventflag_checks\":%u,\"delete_races\":200,\"failed\":0,\"live_objects_preserved\":true,\"game_frames\":false}\n",checks);
}
