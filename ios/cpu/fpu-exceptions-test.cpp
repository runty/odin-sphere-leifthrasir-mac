#include "runtime.h"
#include <array>
#include <atomic>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#pragma STDC FENV_ACCESS ON
extern "C" int32_t __remill_fpu_exception_test(int32_t);
extern "C" void __remill_fpu_exception_clear(int32_t);
extern "C" void __remill_fpu_exception_raise(int32_t);
extern "C" int32_t __remill_fpu_get_rounding();
extern "C" void __remill_fpu_set_rounding(int32_t);
struct Flag {int host;uint32_t guest;};
static constexpr Flag flags[]={{FE_INVALID,kFPUExceptionInvalid},{FE_DIVBYZERO,kFPUExceptionDivByZero},
    {FE_OVERFLOW,kFPUExceptionOverflow},{FE_UNDERFLOW,kFPUExceptionUnderflow},{FE_INEXACT,kFPUExceptionPrecision}};
static std::atomic<unsigned> checks{0},failed{0};
static void check(bool ok){++checks;if(!ok)++failed;}
static uint32_t observedHostFlags(){
    const int observed=std::fetestexcept(FE_ALL_EXCEPT);uint32_t result=0;
    for(const auto &flag:flags)if(observed&flag.host)result|=flag.guest;
    return result;
}
int main(){
    const int originalRound=std::fegetround();
    const int nativeModes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(int mode=0;mode<4;++mode){
        __remill_fpu_exception_clear(127);__remill_fpu_exception_raise(0x43);
        const auto before=__remill_fpu_exception_test(127);
        __remill_fpu_set_rounding(mode);
        check(std::fegetround()==nativeModes[mode] && __remill_fpu_get_rounding()==mode);
        check(__remill_fpu_exception_test(127)==before);
        volatile double positive=1.0,negative=-1.0,half=0x1p-53;
        volatile double up=positive+half,down=negative-half;uint64_t upBits=0,downBits=0;
        double a=up,b=down;std::memcpy(&upBits,&a,8);std::memcpy(&downBits,&b,8);
        check(upBits==(0x3ff0000000000000ull+(mode==2)));
        check(downBits==(0xbff0000000000000ull+(mode==1)));
    }
    std::fesetround(originalRound);__remill_fpu_exception_clear(127);
    // Independent host operations must be visible through Remill's enum bits.
    for(const auto &flag:flags){
        __remill_fpu_exception_clear(kFPUExceptionAll);
        std::feraiseexcept(flag.host);
        check((__remill_fpu_exception_test(kFPUExceptionAll)&flag.guest)==flag.guest);
        __remill_fpu_exception_clear(flag.guest);
        check(!(std::fetestexcept(flag.host)&flag.host));
    }
    __remill_fpu_exception_clear(kFPUExceptionAll);
    volatile double one=1.0,zero=0.0;volatile double infinity=one/zero;(void)infinity;
    check(__remill_fpu_exception_test(kFPUExceptionDivByZero)==kFPUExceptionDivByZero);
    __remill_fpu_exception_clear(kFPUExceptionAll);
    volatile double invalid=zero/zero;(void)invalid;
    check(__remill_fpu_exception_test(kFPUExceptionInvalid)==kFPUExceptionInvalid);
    __remill_fpu_exception_clear(kFPUExceptionAll);
    std::atomic<unsigned> ready{0};std::atomic<bool> go{false};std::vector<std::thread> threads;
    const uint32_t isolated[]={1,2,4,8,16,32,64,0};const int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned worker=0;worker<8;++worker)threads.emplace_back([&,worker]{
        const int saved=std::fegetround();std::fesetround(rounds[worker%4]);
        __remill_fpu_exception_clear(kFPUExceptionAll);__remill_fpu_exception_raise(isolated[worker]);
        ++ready;while(!go.load(std::memory_order_acquire))std::this_thread::yield();
        check(uint32_t(__remill_fpu_exception_test(0x42))==(isolated[worker]&0x42));
        check((uint32_t(__remill_fpu_exception_test(127))&isolated[worker])==isolated[worker]);
        for(uint32_t raised=0;raised<128;++raised){
            __remill_fpu_exception_clear(kFPUExceptionAll);__remill_fpu_exception_raise(raised);
            const uint32_t expected=observedHostFlags()|(raised&0x42);
            for(unsigned read=0;read<256;++read)check(uint32_t(__remill_fpu_exception_test(read))==(expected&read&127));
            check(uint32_t(__remill_fpu_exception_test(-1))==expected);
            const uint32_t cleared=(raised*37)&127;
            __remill_fpu_exception_clear(cleared);
            check(!__remill_fpu_exception_test(cleared));
            check(uint32_t(__remill_fpu_exception_test(127))==(expected&~cleared));
            check(std::fegetround()==rounds[worker%4]);
        }
        __remill_fpu_exception_clear(-1);check(!__remill_fpu_exception_test(-1));std::fesetround(saved);
    });
    while(ready!=8)std::this_thread::yield();go.store(true,std::memory_order_release);
    for(auto &thread:threads)thread.join();check(std::fegetround()==originalRound);
    std::printf("{\"checks\":%u,\"failed\":%u,\"threads\":8,\"host_oracle\":true,\"fp80_arithmetic\":false,\"game_executed\":false}\n",checks.load(),failed.load());
    return failed?1:0;
}
