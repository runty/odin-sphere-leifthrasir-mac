#include "guest_clock.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"clock check %u failed\n",checks+1);std::abort();}++checks;}
static int64_t ns(const timespec &value){return value.tv_sec*int64_t(1000000000)+value.tv_nsec;}
int main() {
    Memory memory;State state{};std::array<uint8_t,256> bytes{},readonly{};
    std::fill(bytes.begin(),bytes.end(),0x7b);std::fill(readonly.begin(),readonly.end(),0x7b);
    mapRegion(memory,0x1000,bytes.data(),bytes.size());mapRegion(memory,0x2000,readonly.data(),readonly.size(),false);
    auto call=[&](const char *name,uint64_t first=0,uint64_t second=0){state=State{};state.gpr.rdi.qword=first;state.gpr.rsi.qword=second;memory.fault={};uint64_t result=0;require(GuestClock::call(name,state,memory,result));return result;};
    require(call("sceKernelGetTscFrequency")==1000000000);
    require(call("sceKernelGetProcessTimeCounterFrequency")==1000000000);
    const auto tsc=call("sceKernelReadTsc"),counter=call("sceKernelGetProcessTimeCounter"),micro=call("sceKernelGetProcessTime");
    const auto start=std::chrono::steady_clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto ticks=call("sceKernelReadTsc")-tsc,elapsed=call("sceKernelGetProcessTimeCounter")-counter,us=call("sceKernelGetProcessTime")-micro;
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    require(double(ticks)/1e9>wall*.75 && double(ticks)/1e9<wall*1.25);
    require(double(elapsed)/1e9>wall*.75 && double(elapsed)/1e9<wall*1.25);
    require(double(us)/1e6>wall*.75 && double(us)/1e6<wall*1.25);
    for(uint32_t id:{0u,2u,4u,5u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u}) {
        clockid_t clock;require(MobileClock::hostClock(id,clock));timespec before{},after{};clock_gettime(clock,&before);
        require(call("sceKernelClockGettime",id,0x1001)==0);clock_gettime(clock,&after);
        int64_t values[2];std::memcpy(values,bytes.data()+1,16);require(values[1]>=0 && values[1]<1000000000);
        if(id==13)require(values[1]==0 && values[0]>=before.tv_sec && values[0]<=after.tv_sec);
        else require(values[0]*1000000000+values[1]>=ns(before) && values[0]*1000000000+values[1]<=ns(after));
        require(bytes[0]==0x7b && bytes[17]==0x7b);
    }
    require(call("sceKernelClockGettime",3,UINT64_MAX)==0x80020016u && memory.fault.empty());
    require(call("sceKernelClockGettime",0,0)==0x80020016u && memory.fault.empty());
    call("sceKernelClockGettime",0,0x10f1);require(!memory.fault.empty());
    call("sceKernelClockGettime",0,0x2000);require(!memory.fault.empty() && readonly[0]==0x7b);
    timeval before{},after{};gettimeofday(&before,nullptr);require(call("sceKernelGettimeofday",0x1021)==0);gettimeofday(&after,nullptr);
    int64_t values[2];std::memcpy(values,bytes.data()+0x21,16);require(values[1]>=0 && values[1]<1000000);
    require(values[0]*1000000+values[1]>=before.tv_sec*1000000+before.tv_usec && values[0]*1000000+values[1]<=after.tv_sec*1000000+after.tv_usec);
    require(bytes[0x20]==0x7b && bytes[0x31]==0x7b);
    require(call("sceKernelGettimeofday",0)==0x80020016u);
    call("sceKernelGettimeofday",0x10f1);require(!memory.fault.empty());
    call("sceKernelGettimeofday",0x2000);require(!memory.fault.empty() && readonly[0]==0x7b);
    uint64_t result=0;require(!GuestClock::call("unsupported",state,memory,result));
    std::atomic<bool> good{true};std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&]{uint64_t previous=MobileClock::tsc(),process=MobileClock::processCounter();for(unsigned j=0;j<10000;++j){const auto value=MobileClock::tsc(),counter=MobileClock::processCounter();if(value<previous || counter<process)good=false;previous=value;process=counter;}});
    for(auto &worker:workers)worker.join();require(good);
    std::printf("{\"clock_checks\":%u,\"monotonic_reads\":80000,\"frequency\":1000000000,\"failed\":0,\"game_frames\":false}\n",checks);
}
