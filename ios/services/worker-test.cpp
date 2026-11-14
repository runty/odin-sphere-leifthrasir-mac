#include "workers.h"
#include "fixtures.h"
#include <array>
#include <cstdio>
#include <cstdlib>
extern "C" void __osl_guest_tick(Memory *,const State *);
extern "C" Memory *__remill_atomic_begin(Memory *);
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"worker check %u failed\n",checks+1);std::abort();}++checks;}
static Memory *fixtureDispatch(State &state,uint64_t pc,Memory *memory){for(const auto &entry:entries)if(entry.pc==pc)return entry.function(state,pc,memory);stopGuest(*memory,"unresolved worker fixture",pc);return memory;}
static void routine(MobileWorkers::Worker &worker) {
    worker.memory.dispatcher=fixtureDispatch;
    std::jmp_buf checkpoint;worker.memory.recovery=&checkpoint;
    if(setjmp(checkpoint)==0) {
        worker.state.gpr.rsp.qword=worker.storage->stackAddress()+worker.storage->stackSize()-0xf8;
        const uint64_t end=0xfeedface;std::memcpy(guestPointer(worker.memory,worker.state.gpr.rsp.qword,8,true),&end,8);
        worker.state.gpr.rdi.qword=worker.argument;
        dispatch(worker.state,worker.entry,&worker.memory);
        if(worker.state.gpr.rip.qword!=end)worker.memory.fault="worker routine did not return";
        worker.result=worker.state.gpr.rax.qword;
    }
    worker.memory.recovery=nullptr;
}
static void spin(MobileWorkers::Worker &worker,std::atomic<unsigned> &progress) {
    worker.memory.checkpoint_limit=UINT64_MAX;worker.memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::jmp_buf checkpoint;worker.memory.recovery=&checkpoint;
    if(setjmp(checkpoint)==0)for(;;){__osl_guest_tick(&worker.memory,&worker.state);++progress;}
    worker.memory.recovery=nullptr;
}
int main() {
    Memory parent;ModuleTlsTemplates templates;std::atomic<bool> stop{false};
    std::array<uint8_t,8192> string{};std::fill(string.begin(),string.end(),0x55);string[4095]=0;
    mapRegion(parent,0x100000,string.data(),string.size(),false);
    MobileWorkers owner(parent,templates,false,stop);GuestThreadAttributes attributes;attributes.stack=65536;
    std::array<uint64_t,4> ids{},stacks{};
    for(unsigned i=0;i<4;++i)require(owner.spawn(image_base+0xcd0630,0x100000+i,attributes,routine,ids[i])==0);
    for(unsigned i=0;i<4;++i){auto worker=owner.find(ids[i]);require(worker && worker->storage);stacks[i]=worker->storage->stackAddress();const auto joined=owner.join(ids[i],0,parent);require(!joined.error && !joined.stop && joined.result==4095-i && worker->memory.fault.empty());}
    require(owner.started()==4);
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<i;++j)require(ids[i]!=ids[j] && stacks[i]!=stacks[j]);
    require(owner.join(ids[0],0,parent).error==EINVAL);require(owner.join(UINT64_MAX,0,parent).error==ESRCH);
    require(owner.join(ids[0],ids[0],parent).error==EDEADLK);require(owner.cancel(UINT64_MAX)==ESRCH);
    uint64_t detached=0;attributes.detached=1;require(owner.spawn(image_base+0xcd0630,0x100000,attributes,routine,detached)==0);
    require(owner.join(detached,0,parent).error==EINVAL && owner.detach(detached)==EINVAL);attributes.detached=0;
    uint64_t target=0;std::atomic<bool> release{false};
    require(owner.spawn(1,0,attributes,[&](auto &worker){while(!release && !worker.memory.stop_requested->load())std::this_thread::yield();worker.result=0x1234;},target)==0);
    parent.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(5);
    const auto bounded=owner.join(target,0,parent);require(!bounded.error && bounded.stop && std::string_view(bounded.stop)=="guest join exceeded host execution deadline");
    release=true;parent.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    const auto joined=owner.join(target,0,parent);require(!joined.error && !joined.stop && joined.result==0x1234);
    uint64_t busy=0;std::atomic<unsigned> progress{0};require(owner.spawn(1,0,attributes,[&](auto &worker){spin(worker,progress);},busy)==0);
    while(progress<1000)std::this_thread::yield();require(owner.cancel(busy)==0 && owner.find(busy)->cancel_pending);
    const auto before=progress.load();while(progress==before)std::this_thread::yield();require(!owner.find(busy)->finished.load()); // deferred, no async pthread kill
    owner.stopAndJoin();require(owner.find(busy)->finished && owner.find(busy)->memory.fault=="guest execution interrupted");
    require(owner.find(busy)->storage && guestPointer(parent,owner.find(busy)->storage->stackAddress(),1)); // retained for GPU drain
    uint64_t sentinel=0x7b;require(owner.spawn(1,0,attributes,routine,sentinel)==EAGAIN && sentinel==0x7b);
    owner.stopAndJoin();
    {Memory memory;std::atomic<bool> stopped{false};MobileWorkers throwing(memory,templates,false,stopped);uint64_t id=0;
     require(throwing.spawn(1,0,attributes,[](auto &worker){__remill_atomic_begin(&worker.memory);throw std::runtime_error("owned fixture");},id)==0);
     while(!throwing.find(id)->finished)std::this_thread::yield();throwing.stopAndJoin();
     require(throwing.find(id)->memory.fault=="native worker callback failed" && !throwing.find(id)->memory.atomic_held);
     GuestMemoryScope available;}
    uint64_t released_stack=0;Memory observer;
    {Memory memory;memory.mappings=observer.mappings;std::atomic<bool> stopped{false};MobileWorkers temporary(memory,templates,false,stopped);uint64_t id=0;
     require(temporary.spawn(1,0,attributes,[](auto &){},id)==0);released_stack=temporary.find(id)->storage->stackAddress();temporary.stopAndJoin();}
    require(!guestPointer(observer,released_stack,1));observer.fault={};
    {Memory memory;std::atomic<bool> stopped{false};MobileWorkers foreign(memory,templates,false,stopped);require(foreign.cancel(ids[0])==ESRCH);}
    {Memory memory;std::atomic<bool> stopped{false};MobileWorkers bounded(memory,templates,false,stopped);
     GuestThreadAttributes small;small.stack=16384;std::mutex lock;std::condition_variable changed;bool release_all=false;
     std::array<uint64_t,64> children{};
     for(auto &id:children)require(bounded.spawn(1,0,small,[&](auto &){std::unique_lock<std::mutex> wait(lock);changed.wait(wait,[&]{return release_all;});},id)==0);
     uint64_t untouched=0x7b;require(bounded.spawn(1,0,small,[](auto &){},untouched)==EAGAIN && untouched==0x7b);
     {std::lock_guard<std::mutex> done(lock);release_all=true;}changed.notify_all();bounded.stopAndJoin();
     for(auto id:children)require(bounded.find(id)->finished.load());
    }
    std::printf("{\"worker_checks\":%u,\"real_aot_strlen_workers\":4,\"failed\":0,\"game_worker_created\":false,\"game_frames\":false}\n",checks);
}
