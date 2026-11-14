#include "condition.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"condition check %u failed\n",checks+1);std::abort();}++checks;}
using Clock=std::chrono::steady_clock;
static void until(MobileConditions &owner,uint64_t id,unsigned count) {
    const auto end=Clock::now()+std::chrono::seconds(2);
    while(owner.waiters(id)!=count && Clock::now()<end)std::this_thread::yield();
    require(owner.waiters(id)==count);
}
int main() {
    MobileConditions owner,other;uint64_t id=0;require(owner.create(id)==0 && id>1 && owner.count()==1);
    require(other.remove(id)==EINVAL && other.notify(id,false)==EINVAL);
    pthread_mutexattr_t attr;require(pthread_mutexattr_init(&attr)==0);
    require(pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_ERRORCHECK)==0);
    pthread_mutex_t mutex;require(pthread_mutex_init(&mutex,&attr)==0);pthread_mutexattr_destroy(&attr);
    require(pthread_mutex_lock(&mutex)==0);
    auto start=Clock::now();auto wait=owner.wait(id,&mutex,true,2000,start+std::chrono::seconds(1));
    require(wait.error==ETIMEDOUT && !wait.host_deadline && Clock::now()-start>=std::chrono::microseconds(2000));
    require(pthread_mutex_trylock(&mutex)==EBUSY);require(pthread_mutex_unlock(&mutex)==0);
    require(pthread_mutex_lock(&mutex)==0);start=Clock::now();
    wait=owner.wait(id,&mutex,false,0,start+std::chrono::milliseconds(5));
    require(wait.host_deadline && wait.error==0 && Clock::now()-start<std::chrono::seconds(1));
    require(pthread_mutex_unlock(&mutex)==0);
    require(owner.wait(UINT64_MAX,&mutex,true,0,Clock::now()+std::chrono::seconds(1)).error==EINVAL);
    std::atomic<int> arrived{0};std::atomic<bool> release{false},good{true};
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&] {
        if(pthread_mutex_lock(&mutex)){good=false;return;}
        ++arrived;
        while(!release) {
            const auto result=owner.wait(id,&mutex,false,0,Clock::now()+std::chrono::seconds(2));
            if(result.error || result.host_deadline){good=false;break;}
        }
        if(pthread_mutex_unlock(&mutex))good=false;
    });
    until(owner,id,4);require(arrived==4 && owner.remove(id)==EBUSY);
    require(pthread_mutex_lock(&mutex)==0);release=true;require(owner.notify(id,true)==0);require(pthread_mutex_unlock(&mutex)==0);
    for(auto &worker:workers)worker.join();require(good && owner.waiters(id)==0);
    require(owner.remove(id)==0 && owner.count()==0);require(owner.remove(id)==EINVAL && owner.notify(id,false)==EINVAL);
    for(unsigned i=0;i<4096;++i){uint64_t token=0;require(owner.create(token)==0);}
    uint64_t sentinel=0x1234;require(owner.create(sentinel)==ENOMEM && sentinel==0x1234 && owner.count()==4096);
    { MobileConditions concurrent;std::atomic<unsigned> passed{0};workers.clear();
      for(unsigned i=0;i<4;++i)workers.emplace_back([&]{for(unsigned j=0;j<200;++j){uint64_t token=0;if(!concurrent.create(token) && !concurrent.notify(token,j&1) && !concurrent.remove(token))++passed;}});
      for(auto &worker:workers)worker.join();require(passed==800 && concurrent.count()==0); }
    MobileConditions guest;Memory memory;State state{};std::array<uint8_t,256> bytes{},readonly{};
    std::fill(bytes.begin(),bytes.end(),0x7b);std::fill(readonly.begin(),readonly.end(),0x7b);
    mapRegion(memory,0x1000,bytes.data(),bytes.size());mapRegion(memory,0x2000,readonly.data(),readonly.size(),false);
    auto call=[&](const char *name,uint64_t first,uint64_t second=0,uint64_t third=0){
        state=State{};state.gpr.rdi.qword=first;state.gpr.rsi.qword=second;state.gpr.rdx.qword=third;memory.fault={};uint64_t result=0;
        require(GuestConditions::call(name,state,memory,guest,[&](uint64_t token){return token==0x9988 ? &mutex : nullptr;},result));return result;
    };
    require(call("scePthreadCondInit",0)==0x80020016u);
    call("scePthreadCondInit",0x10fc);require(!memory.fault.empty() && guest.count()==0);
    call("scePthreadCondInit",0x2000);require(!memory.fault.empty() && guest.count()==0 && readonly[0]==0x7b);
    require(call("scePthreadCondInit",0x1001,0x1000)==0x80020016u && bytes[1]==0x7b && guest.count()==0);
    call("scePthreadCondInit",0x1001,0,0xffffffffffffffffull);require(!memory.fault.empty() && guest.count()==0);
    std::memcpy(bytes.data()+0x40,"guest",6);
    require(call("scePthreadCondInit",0x1001,0,0x1040)==0 && guest.count()==1);
    uint64_t token=0;std::memcpy(&token,bytes.data()+1,8);require(token>1 && bytes[0]==0x7b && bytes[9]==0x7b);
    require(call("scePthreadCondSignal",0x1001)==0 && call("scePthreadCondBroadcast",0x1001)==0);
    uint64_t mutex_token=0x9988;std::memcpy(bytes.data()+0x21,&mutex_token,8);
    require(pthread_mutex_lock(&mutex)==0);memory.deadline=Clock::now()+std::chrono::seconds(1);
    require(call("scePthreadCondTimedwait",0x1001,0x1021,1000)==0x8002003cu && memory.fault.empty());
    require(pthread_mutex_unlock(&mutex)==0);
    require(pthread_mutex_lock(&mutex)==0);memory.deadline=Clock::now()+std::chrono::milliseconds(5);
    call("scePthreadCondWait",0x1001,0x1021);require(memory.fault=="condition wait exceeded host execution deadline");
    require(pthread_mutex_unlock(&mutex)==0);
    call("scePthreadCondWait",0x1001,0x10ff);require(!memory.fault.empty() && guest.count()==1);
    require(call("scePthreadCondDestroy",0x1001)==0 && guest.count()==0);
    std::memcpy(&sentinel,bytes.data()+1,8);require(sentinel==0 && bytes[0]==0x7b && bytes[9]==0x7b);
    require(call("scePthreadCondSignal",0x1001)==0 && guest.count()==1); // lazy static initialization
    require(call("scePthreadCondDestroy",0x1001)==0 && guest.count()==0);
    std::memcpy(bytes.data()+1,&token,8);require(call("scePthreadCondSignal",0x1001)==0x80020016u && guest.count()==0);
    uint64_t result=0;require(!GuestConditions::call("unsupported",state,memory,guest,[](uint64_t)->pthread_mutex_t *{return nullptr;},result));
    require(pthread_mutex_destroy(&mutex)==0);
    std::printf("{\"condition_checks\":%u,\"failed\":0,\"game_frames\":false}\n",checks);
}
