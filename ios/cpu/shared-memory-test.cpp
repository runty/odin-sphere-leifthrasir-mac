#include "runtime.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
extern "C" uint32_t __remill_read_memory_32(Memory *,uint64_t);
extern "C" Memory *__remill_write_memory_32(Memory *,uint64_t,uint32_t);
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *,uint64_t,uint32_t &,uint32_t);
extern "C" void __osl_guest_tick(Memory *,const State *);
extern "C" Memory *__remill_atomic_begin(Memory *);
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"shared memory check %u failed\n",checks+1);std::abort();}++checks;}
int main() {
    Memory owner,alias;alias.mappings=owner.mappings;
    std::array<uint8_t,64> data{},protected_data{};
    mapRegion(owner,0x4000,data.data(),data.size());mapRegion(owner,0x5000,protected_data.data(),protected_data.size(),false);
    require(guestPointer(alias,0x4000,64)==data.data());
    require(!guestPointer(alias,0x403f,2) && alias.fault=="unmapped guest memory");alias.fault={};
    require(!guestPointer(alias,UINT64_MAX-3,8));alias.fault={};
    require(!guestPointer(alias,0x5000,4,true) && alias.fault=="guest write to protected region");alias.fault={};
    bool overlap=false;try{mapRegion(alias,0x403f,data.data(),2);}catch(...){overlap=true;}require(overlap);
    require(unmapRegion(alias,0x4000) && !unmapRegion(alias,0x4000));
    require(!guestPointer(owner,0x4000,1));owner.fault={};mapRegion(owner,0x4000,data.data(),data.size());
    std::atomic<bool> start{false},good{true};std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&] {
        Memory memory;memory.mappings=owner.mappings;
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned j=0;j<4000;++j) {
            auto expected=__remill_read_memory_32(&memory,0x4000);
            for(;;){const auto desired=expected+1;__remill_compare_exchange_memory_32(&memory,0x4000,expected,desired);if(expected==desired-1)break;}
            if(!memory.fault.empty())good=false;
        }
    });
    workers.emplace_back([&] {
        Memory memory;memory.mappings=owner.mappings;
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned j=0;j<4000;++j){GuestMemoryScope host;auto value=__remill_read_memory_32(&memory,0x4000);__remill_write_memory_32(&memory,0x4000,value+1);}
    });
    workers.emplace_back([&] {
        Memory memory;memory.mappings=owner.mappings;std::array<uint8_t,16> bytes{};
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned j=0;j<2000;++j){mapRegion(memory,0x6000,bytes.data(),bytes.size());if(!unmapRegion(memory,0x6000))good=false;}
    });
    start.store(true,std::memory_order_release);for(auto &thread:workers)thread.join();
    require(good && __remill_read_memory_32(&owner,0x4000)==20000);
    require(guestPointer(alias,0x4000,64)==data.data());
    auto guarded=std::make_unique<Memory>();guarded->mappings=owner.mappings;
    std::jmp_buf checkpoint;guarded->recovery=&checkpoint;
    if(setjmp(checkpoint)==0){__remill_atomic_begin(guarded.get());__remill_write_memory_32(guarded.get(),0x5000,123);std::abort();}
    guarded->recovery=nullptr;
    require(!guarded->atomic_held && guarded->fault=="guest write to protected region");
    // A fault must leave neither the transfer/atomic lock nor map lock held.
    mapRegion(*guarded,0x7000,data.data(),8);require(unmapRegion(*guarded,0x7000));
    {GuestMemoryScope host;require(__remill_read_memory_32(&alias,0x4000)==20000);host.unlock();host.lock();}
    for(bool cancel:{false,true}) {
        auto memory=std::make_unique<Memory>();std::atomic<bool> request{false},entered{false},stopped{false};
        if(cancel)memory->cancel_requested=&request;else memory->stop_requested=&request;
        memory->checkpoint_limit=UINT64_MAX;memory->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        std::thread cpu([&] {
            State state{};state.gpr.rip.qword=0x1234;std::jmp_buf end;memory->recovery=&end;
            if(setjmp(end)==0){entered=true;for(;;)__osl_guest_tick(memory.get(),&state);}
            memory->recovery=nullptr;stopped=true;
        });
        while(!entered)std::this_thread::yield();request=true;cpu.join();
        require(stopped && memory->missing_pc==0x1234 && memory->fault==(cancel ? "guest thread cancellation requested" : "guest execution interrupted"));
    }
    require(!guestInterruption(owner));
    std::printf("{\"shared_memory_checks\":%u,\"atomic_updates\":20000,\"concurrent_map_cycles\":2000,\"failed\":0,\"game_frames\":false}\n",checks);
}
