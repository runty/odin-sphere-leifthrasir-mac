#include "runtime.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
#define DECLARE(B,T) extern "C" Memory *__remill_compare_exchange_memory_##B(Memory *,uint64_t,T &,T);
DECLARE(8,uint8_t) DECLARE(16,uint16_t) DECLARE(32,uint32_t) DECLARE(64,uint64_t)
#undef DECLARE
extern "C" Memory *__remill_compare_exchange_memory_128(Memory *,uint64_t,uint128_t &,uint128_t &);
extern "C" Memory *__remill_atomic_begin(Memory *);
static unsigned checks=0;
static void check(bool value){if(!value){std::fprintf(stderr,"CAS width check %u failed\n",checks+1);std::abort();}++checks;}
template<class T> Memory *exchange(Memory *,uint64_t,T &,T &);
#define FORWARD(T,B) template<> Memory *exchange<T>(Memory *m,uint64_t a,T &e,T &d){return __remill_compare_exchange_memory_##B(m,a,e,d);}
FORWARD(uint8_t,8) FORWARD(uint16_t,16) FORWARD(uint32_t,32) FORWARD(uint64_t,64) FORWARD(uint128_t,128)
#undef FORWARD
template<class T> static T read(Memory &m,uint64_t a){GuestMemoryScope scope;T value{};auto p=guestPointer(m,a,sizeof(T));if(p)std::memcpy(&value,p,sizeof(T));return value;}
template<class T> static void test(){
    std::array<uint8_t,128> bytes;bytes.fill(0x5a);Memory m;mapRegion(m,0x1000,bytes.data(),bytes.size());
    const T initial=T(uint64_t(0xfedcba9876543210ull))|(sizeof(T)==16?T(uint128_t(0x123456789abcdef0ull)<<64):T(0));
    T desired=~initial,expected=initial;std::memcpy(bytes.data()+1,&initial,sizeof(T));
    check(exchange(&m,0x1001,expected,desired)==&m && expected==initial && desired==T(~initial));
    check(read<T>(m,0x1001)==desired && bytes[0]==0x5a && bytes[sizeof(T)+1]==0x5a);
    expected=initial;const T before=desired;T second=initial;
    exchange(&m,0x1001,expected,second);check(expected==before && read<T>(m,0x1001)==before && second==initial);
    const auto end=0x1000+bytes.size()-sizeof(T);std::memcpy(bytes.data()+bytes.size()-sizeof(T),&initial,sizeof(T));expected=initial;
    exchange(&m,end,expected,desired);check(expected==initial && read<T>(m,end)==desired);
    const auto snapshot=bytes;expected=initial;exchange(&m,end+1,expected,desired);
    check(m.fault=="unmapped guest memory" && expected==initial && bytes==snapshot);m.fault={};
    auto protectedMemory=std::make_unique<Memory>();mapRegion(*protectedMemory,0x2000,bytes.data(),bytes.size(),false);
    std::jmp_buf recovery;protectedMemory->recovery=&recovery;
    if(setjmp(recovery)==0){__remill_atomic_begin(protectedMemory.get());exchange(protectedMemory.get(),0x2001,expected,desired);std::abort();}
    protectedMemory->recovery=nullptr;check(!protectedMemory->atomic_held && protectedMemory->fault=="guest write to protected region" && bytes==snapshot);
    std::atomic<bool> available{false};std::thread probe([&]{GuestMemoryScope scope;available=true;});probe.join();check(available);
    // Each worker has its own CPU/fault context and shares only mapped bytes.
    const T base=sizeof(T)==16?T(uint128_t(0xabcdef0123456789ull)<<64):T(0);std::memcpy(bytes.data()+1,&base,sizeof(T));
    const unsigned iterations=sizeof(T)==1?40:1000;std::vector<std::thread> workers;std::atomic<bool> good{true};
    for(unsigned i=0;i<4;++i)workers.emplace_back([&]{Memory cpu;cpu.mappings=m.mappings;
        for(unsigned j=0;j<iterations;++j){T value=read<T>(cpu,0x1001);for(;;){const T old=value;T next=T(old+1);exchange(&cpu,0x1001,value,next);if(value==old)break;}
            if(!cpu.fault.empty())good=false;}});
    for(auto &worker:workers)worker.join();check(good && read<T>(m,0x1001)==T(base+4*iterations));
}
int main(){test<uint8_t>();test<uint16_t>();test<uint32_t>();test<uint64_t>();test<uint128_t>();
    std::printf("{\"checks\":%u,\"widths\":5,\"successful_concurrent_increments\":16160,\"failed\":0,\"scope\":\"checked-memory intrinsic ABI; not full x86 alignment-fault qualification\",\"game_executed\":false}\n",checks);}
