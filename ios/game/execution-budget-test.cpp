#include "execution-budget.h"
#include "../cpu/runtime.h"
#include <atomic>
#include <cassert>
#include <csetjmp>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

extern "C" void __osl_guest_tick(Memory *,const State *);
int main() {
    using Clock=OslExecutionBudget::Clock;
    assert(!osl_execution_budget_valid(0,false));
    assert(osl_execution_budget_valid(0,true));
    for(uint32_t n:{1U,99U,600001U,UINT32_MAX}) assert(!osl_execution_budget_valid(n,true));
    for(uint32_t n:{100U,2000U,600000U}) assert(osl_execution_budget_valid(n,false));
    auto now=Clock::now();auto bounded=osl_execution_budget(2000,now);
    assert(bounded.deadline==now+std::chrono::milliseconds(2000));
    assert(bounded.checkpoints==1000000 && bounded.dispatches==100000);
    auto large=osl_execution_budget(600000,now);
    assert(large.checkpoints==300000000 && large.dispatches==30000000);
    auto overflow=osl_execution_budget(600000,Clock::time_point::max()-std::chrono::milliseconds(1));
    assert(overflow.deadline==Clock::time_point::max());
    auto unbounded=osl_execution_budget(0,now);
    assert(unbounded.deadline==Clock::time_point::max() && unbounded.checkpoints==UINT64_MAX && unbounded.dispatches==UINT64_MAX);
    std::atomic<bool> stop{false},ready{false},completed{false};
    std::thread guest([&] {
        auto memory=std::make_unique<Memory>();State state{};
        memory->deadline=unbounded.deadline;memory->checkpoint_limit=unbounded.checkpoints;memory->stop_requested=&stop;
        std::jmp_buf checkpoint;memory->recovery=&checkpoint;
        if(setjmp(checkpoint)==0) {
            for(unsigned i=0;i<100000;++i)__osl_guest_tick(memory.get(),&state);
            ready=true;
            while(true)__osl_guest_tick(memory.get(),&state);
        }
        memory->recovery=nullptr;
        assert(memory->fault=="guest execution interrupted" && memory->checkpoints>=100000);
        completed=true;
    });
    auto limit=Clock::now()+std::chrono::seconds(5);
    while(!ready && Clock::now()<limit)std::this_thread::yield();
    assert(ready);stop=true;guest.join();assert(completed);
    std::puts("Execution budget boundaries, overflow-safe deadline and cancellable continuous guest: passed");
}
