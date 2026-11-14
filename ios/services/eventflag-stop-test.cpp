#ifndef OSL_EVENTFLAG_TEST_HEADER
#define OSL_EVENTFLAG_TEST_HEADER "eventflag.h"
#endif
#include OSL_EVENTFLAG_TEST_HEADER
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
extern "C" unsigned runtime_eventflag_waiters(uint64_t);
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"interruptible event check %u failed\n",checks+1);std::abort();}++checks;}
struct Guard {std::atomic<bool> stop{false};std::atomic<unsigned> calls{0};};
static int interrupt(void *context){auto &guard=*static_cast<Guard *>(context);++guard.calls;return guard.stop.load();}
static void parked(uint64_t id,unsigned count){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(runtime_eventflag_waiters(id)!=count){if(std::chrono::steady_clock::now()>end)std::abort();std::this_thread::yield();}
}
int main(){
    const auto initial=runtime_eventflag_live_count();MobileEventFlags flags;uint64_t id=0;
    constexpr uint32_t interrupted=0x80020004u,canceled=0x80020055u;
    uint64_t result=0;uint32_t status=0,timeout=5000000;Guard guard;
    require(flags.create("host-stop",0x10,0,id)==0);
    std::thread worker([&]{status=flags.wait_guarded(id,1,1,&result,&timeout,interrupt,&guard);});
    parked(id,1);std::this_thread::sleep_for(std::chrono::milliseconds(25));
    require(flags.poll(id,1,1,nullptr)==0x80020001u); // No gap between guard polls.
    const auto start=std::chrono::steady_clock::now();guard.stop=true;worker.join();
    const auto stop_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    require(status==interrupted && timeout>4000000 && guard.calls>=3 && stop_ms<250);
    require(runtime_eventflag_waiters(id)==0 && flags.remove(id)==0);
    require(runtime_eventflag_live_count()==initial);

    require(flags.create("fifo",0x20,0,id)==0);
    std::array<Guard,2> guards;std::array<uint32_t,2> statuses{};std::array<uint64_t,2> results{};
    std::array<std::atomic<bool>,2> done{};
    auto wait=[&](unsigned i){uint32_t limit=5000000;statuses[i]=flags.wait_guarded(id,1,0x21,&results[i],&limit,interrupt,&guards[i]);done[i]=true;};
    std::thread first(wait,0);parked(id,1);std::thread second(wait,1);parked(id,2);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));require(guards[0].calls>=3 && guards[1].calls>=3);
    require(flags.update("sceKernelSetEventFlag",id,1)==0);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(!done[0] && std::chrono::steady_clock::now()<end)std::this_thread::yield();
    require(done[0] && !done[1]);first.join();guards[1].stop=true;second.join();
    require(statuses[0]==0 && results[0]==1 && statuses[1]==interrupted);
    require(flags.poll(id,1,1,&result)==0x80020010u && result==0); // First clear applies exactly once.
    require(flags.remove(id)==0 && runtime_eventflag_live_count()==initial);

    for(bool remove:{false,true}){
        require(flags.create(remove?"delete":"cancel",0x10,0,id)==0);
        Guard current;timeout=5000000;result=0;
        worker=std::thread([&]{status=flags.wait_guarded(id,1,1,&result,&timeout,interrupt,&current);});parked(id,1);
        if(remove)require(flags.remove(id)==0);
        else {int32_t count=0;require(flags.cancel(id,8,&count)==0 && count==1);}
        worker.join();require(status==(remove?MobileEventFlags::missing:canceled));
        if(!remove){require(result==8 && flags.poll(id,8,1,nullptr)==0);require(flags.remove(id)==0);}
        require(runtime_eventflag_live_count()==initial);
    }
    require(flags.create("timeout",0x10,0,id)==0);Guard never;timeout=30000;result=7;
    require(flags.wait_guarded(id,1,1,&result,&timeout,interrupt,&never)==MobileEventFlags::timed_out && timeout==0 && result==0 && never.calls>=3);
    require(flags.remove(id)==0);

    // The guest bridge must stop outside native locks and preserve caller
    // result/timeout bytes rather than fabricate a guest EINTR/timeout return.
    require(flags.create("guest-stop",0x10,0,id)==0);
    Memory memory;State state{};std::array<uint8_t,512> bytes;bytes.fill(0x7b);
    constexpr uint64_t base=0x20000000;mapRegion(memory,base,bytes.data(),bytes.size());
    const uint64_t marker=0x123456789abcdef0ull;const uint32_t requested=5000000;
    std::memcpy(bytes.data()+0x101,&marker,8);std::memcpy(bytes.data()+0x121,&requested,4);const auto before=bytes;
    std::atomic<bool> stop{false};memory.stop_requested=&stop;memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    state.gpr.rdi.qword=id;state.gpr.rsi.qword=1;state.gpr.rdx.qword=1;state.gpr.rcx.qword=base+0x101;state.gpr.r8.qword=base+0x121;
    bool handled=false;uint64_t native_result=0;
    worker=std::thread([&]{handled=GuestEvents::call("sceKernelWaitEventFlag",state,memory,flags,native_result);});parked(id,1);
    const auto guest_start=std::chrono::steady_clock::now();stop=true;worker.join();
    const auto guest_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-guest_start).count();
    require(handled && memory.fault=="guest execution interrupted" && guest_ms<250 && bytes==before);
    require(runtime_eventflag_waiters(id)==0 && flags.remove(id)==0 && runtime_eventflag_live_count()==initial);
    std::printf("{\"interruptible_event_checks\":%u,\"native_stop_ms\":%.3f,\"guest_stop_ms\":%.3f,\"fifo_and_clear_preserved\":true,\"guest_outputs_preserved_on_host_stop\":true,\"failed\":0,\"game_frames\":false}\n",checks,stop_ms,guest_ms);
}
