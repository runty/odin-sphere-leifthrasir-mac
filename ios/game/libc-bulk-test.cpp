#include "libc-bulk.h"
#include <array>
#include <cstdio>
#include <thread>
#include <vector>
extern "C" Memory *sub_10cd0ba0(State &,uint64_t,Memory *);
extern "C" Memory *sub_10cd0c40(State &,uint64_t,Memory *);
extern "C" Memory *sub_10cd0ce0(State &,uint64_t,Memory *);
extern "C" Memory *__remill_atomic_begin(Memory *);
using Shim=Memory *(*)(State &,uint64_t,Memory *);
constexpr uint64_t pcs[]={0x10cd0ba0,0x10cd0c40,0x10cd0ce0};
Shim shims[]={sub_10cd0ba0,sub_10cd0c40,sub_10cd0ce0};
constexpr uint64_t base=0x20000000,stackBase=0x30000000,capacity=uint64_t(5056)*1024*1024;
std::atomic<uint64_t> checks{0};
void require(bool value){++checks;if(!value){std::fprintf(stderr,"FAIL at check %llu\n",checks.load());std::abort();}}
// These thin exports mirror the production per-session wrappers; the production
// LLVM instrumenter and checked/locked helper are compiled unchanged.
extern "C" Memory *__osl_libc_memcpy(State &s,uint64_t pc,Memory *m){libcBulkReturn(s,*m,LibcBulk::Copy,pc,capacity);return m;}
extern "C" Memory *__osl_libc_memmove(State &s,uint64_t pc,Memory *m){libcBulkReturn(s,*m,LibcBulk::Move,pc,capacity);return m;}
extern "C" Memory *__osl_libc_memset(State &s,uint64_t pc,Memory *m){libcBulkReturn(s,*m,LibcBulk::Fill,pc,capacity);return m;}
struct Fixture {
    Memory m;std::vector<uint8_t> data;std::array<uint8_t,4096> stack{};
    Fixture(size_t size=1024):data(size){mapRegion(m,base,data.data(),data.size());mapRegion(m,stackBase,stack.data(),stack.size());m.checkpoint_limit=UINT64_MAX;m.deadline=std::chrono::steady_clock::now()+std::chrono::hours(1);}
    State state(uint64_t dst,uint64_t src,uint64_t count,uint64_t sp=stackBase+4000){
        State s;std::memset(&s,0xa5,sizeof(s));s.gpr.rdi.qword=dst;s.gpr.rsi.qword=src;s.gpr.rdx.qword=count;s.gpr.rsp.qword=sp;
        uint64_t ret=0x123456789abcdef0;std::memcpy(stack.data()+4000,&ret,8);return s;
    }
};
State expectedReturn(const State &s,uint64_t ret=0x123456789abcdef0){State expected;std::memcpy(&expected,&s,sizeof(s));expected.gpr.rax.qword=s.gpr.rdi.qword;expected.gpr.rsp.qword+=8;expected.gpr.rip.qword=ret;return expected;}
void success(unsigned kind,State &s,Fixture &f,uint64_t ret=0x123456789abcdef0){State expected=expectedReturn(s,ret);require(shims[kind](s,pcs[kind],&f.m)==&f.m);require(f.m.fault.empty());require(!std::memcmp(&s,&expected,sizeof(s)));}
void pattern(std::vector<uint8_t>&v){for(size_t i=0;i<v.size();++i)v[i]=uint8_t(i*173+i/53);}
void copies(){
    Fixture f;std::vector<uint8_t> initial(f.data.size());pattern(initial);
    constexpr size_t sizes[]={0,1,2,3,7,8,15,16,17,31,32,33,63,64,65,127,128,129};
    for(unsigned kind=0;kind<2;++kind)for(size_t n:sizes)for(unsigned sa=0;sa<64;++sa)for(unsigned da=0;da<64;++da){
        f.data=initial;auto expected=initial;for(size_t i=0;i<n;++i)expected[512+da+i]=initial[64+sa+i];
        auto s=f.state(base+512+da,base+64+sa,n);success(kind,s,f);require(f.data==expected);
    }
    for(unsigned kind=0;kind<2;++kind)for(size_t n:sizes)for(unsigned delta=0;delta<64;++delta)for(unsigned direction=0;direction<2;++direction){
        f.data=initial;auto expected=initial;size_t src=128+(direction?delta:0),dst=128+(direction?0:delta);
        for(size_t i=0;i<n;++i)expected[dst+i]=initial[src+i];
        auto s=f.state(base+dst,base+src,n);success(kind,s,f);require(f.data==expected);
    }
    for(size_t n:sizes)for(unsigned alignment=0;alignment<64;++alignment)for(uint64_t value:{0ull,0x1234ccull,UINT64_MAX}){
        f.data=initial;auto expected=initial;for(size_t i=0;i<n;++i)expected[alignment+i]=uint8_t(value);
        auto s=f.state(base+alignment,value,n);success(2,s,f);require(f.data==expected);
    }
    Fixture large(32*1024*1024+128);pattern(large.data);const auto initialLarge=large.data;const size_t n=16*1024*1024;
    auto s=large.state(base+n+64,base+32,n);success(0,s,large);
    require(!std::memcmp(large.data.data()+n+64,initialLarge.data()+32,n));
    require(!std::memcmp(large.data.data(),initialLarge.data(),n+64));
    require(!std::memcmp(large.data.data()+2*n+64,initialLarge.data()+2*n+64,64));
}
void edges(){
    Fixture f;pattern(f.data);
    for(unsigned kind=0;kind<3;++kind){auto s=f.state(0,UINT64_MAX,0);success(kind,s,f);}
    // RET observes bytes written over its own slot.
    auto fill=f.state(stackBase+4000,0xcc,8);success(2,fill,f,0xcccccccccccccccc);
    uint64_t ret=0xaabbccddeeff1234;std::memcpy(f.data.data()+3,&ret,8);
    for(unsigned kind=0;kind<2;++kind){auto s=f.state(stackBase+4000,base+3,8);success(kind,s,f,ret);}
    // Exact end of a mapping is valid; crossing it is rejected in failures().
    auto last=f.state(base+f.data.size()-1,base,1);success(1,last,f);require(f.data.back()==f.data.front());
}
void reject(unsigned kind,uint64_t dst,uint64_t src,uint64_t n,uint64_t sp=stackBase+4000,bool cancel=false,bool atomic=false){
    Fixture f;pattern(f.data);const auto original=f.data;auto s=f.state(dst,src,n,sp);State before;std::memcpy(&before,&s,sizeof(s));
    std::atomic<bool> stop{cancel};f.m.stop_requested=&stop;std::jmp_buf recovery;f.m.recovery=&recovery;
    if(setjmp(recovery)==0){if(atomic)__remill_atomic_begin(&f.m);shims[kind](s,pcs[kind],&f.m);require(false);}
    f.m.recovery=nullptr;require(!f.m.fault.empty());require(f.data==original);require(!std::memcmp(&s,&before,sizeof(s)));require(!f.m.atomic_held);
    std::atomic<bool> acquired{false};std::thread cpu([&]{GuestMemoryScope scope;acquired=true;});cpu.join();require(acquired);
}
void failures(){
    for(unsigned kind=0;kind<3;++kind){
        reject(kind,base+1023,base,2);reject(kind,0,base,1);reject(kind,base,base,capacity+1);
        reject(kind,base,base,1,0);reject(kind,base,base,1,UINT64_MAX-4);
        reject(kind,base,base,1,stackBase+4000,true);reject(kind,base+1023,base,2,stackBase+4000,false,true);
        if(kind<2){reject(kind,base,base+1023,2);reject(kind,base,0,1);}
    }
    Fixture f;pattern(f.data);const auto original=f.data;require(unmapRegion(f.m,base));mapRegion(f.m,base,f.data.data(),f.data.size(),false);
    auto s=f.state(base,base+8,8);State before;std::memcpy(&before,&s,sizeof(s));shims[1](s,pcs[1],&f.m);
    require(f.m.fault=="guest write to protected region");require(f.data==original);require(!std::memcmp(&s,&before,sizeof(s)));
    f.m.fault={};s=f.state(stackBase+32,base,8);success(0,s,f);require(!std::memcmp(f.stack.data()+32,f.data.data(),8));
}
thread_local uint8_t *externalBytes=nullptr;
thread_local std::atomic<bool> *cancelOnResolve=nullptr;
uint8_t *external(uint64_t address,size_t size,bool){
    constexpr uint64_t alias=0x70000000;
    if(address<alias || size>64 || address-alias>64-size)return nullptr;
    if(cancelOnResolve)*cancelOnResolve=true;
    return externalBytes+address-alias;
}
void aliases(){
    Fixture f;pattern(f.data);f.m.external_pointer=external;externalBytes=f.data.data();
    auto s=f.state(base+8,0x70000000,32);const auto original=f.data;success(1,s,f);
    for(size_t i=0;i<32;++i)require(f.data[8+i]==original[i]);
    // Inject cancellation on source resolution: bytes commit, RET stays pending.
    f.data=original;std::atomic<bool> stop{false};f.m.stop_requested=&stop;cancelOnResolve=&stop;
    s=f.state(base+128,0x70000000,32);State before;std::memcpy(&before,&s,sizeof(s));shims[0](s,pcs[0],&f.m);
    require(f.m.fault=="guest execution interrupted");require(!std::memcmp(&s,&before,sizeof(s)));require(!std::memcmp(f.data.data()+128,original.data(),32));
    cancelOnResolve=nullptr;externalBytes=nullptr;
}
void concurrent(){std::vector<std::thread> cpus;for(unsigned cpu=0;cpu<8;++cpu)cpus.emplace_back([]{Fixture f;pattern(f.data);for(unsigned i=0;i<2000;++i){auto s=f.state(base+512,base,128);success(i%2,s,f);require(!std::memcmp(f.data.data()+512,f.data.data(),128));}});for(auto &cpu:cpus)cpu.join();}
int main(){copies();edges();failures();aliases();concurrent();std::printf("{\"passed\":true,\"checks\":%llu}\n",checks.load());}
