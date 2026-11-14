#pragma once
#include "../cpu/runtime.h"
#include "clock.h"
#include <cerrno>
#include <cstring>
#include <sys/time.h>
extern "C" int32_t runtime_guest_errno(int);
namespace GuestClock {
inline bool call(const char *name,State &state,Memory &memory,uint64_t &result) {
    result=0;
    if(!std::strcmp(name,"sceKernelReadTsc")){result=MobileClock::tsc();return true;}
    if(!std::strcmp(name,"sceKernelGetTscFrequency") || !std::strcmp(name,"sceKernelGetProcessTimeCounterFrequency")){result=MobileClock::frequency;return true;}
    if(!std::strcmp(name,"sceKernelGetProcessTime")){result=MobileClock::processTime();return true;}
    if(!std::strcmp(name,"sceKernelGetProcessTimeCounter")){result=MobileClock::processCounter();return true;}
    const bool clock=!std::strcmp(name,"sceKernelClockGettime"),wall=!std::strcmp(name,"sceKernelGettimeofday");
    if(!(clock || wall))return false;
    const uint64_t target=clock ? state.gpr.rsi.qword : state.gpr.rdi.qword;
    clockid_t host=CLOCK_REALTIME;
    if(!target || (clock && !MobileClock::hostClock(state.gpr.rdi.dword,host))){result=0x80020016u;return true;}
    auto output=guestPointer(memory,target,16,true);if(!output)return true;
    int64_t values[2]{};int error=0;
    if(clock){timespec value{};error=clock_gettime(host,&value);values[0]=value.tv_sec;values[1]=state.gpr.rdi.dword==13 ? 0 : value.tv_nsec;}
    else {timeval value{};error=gettimeofday(&value,nullptr);values[0]=value.tv_sec;values[1]=value.tv_usec;}
    if(error)result=0x80020000u|uint32_t(runtime_guest_errno(errno));
    else std::memcpy(output,values,sizeof(values));
    return true;
}
}
