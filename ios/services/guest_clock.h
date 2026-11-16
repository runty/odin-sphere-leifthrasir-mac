#pragma once
#include "../cpu/runtime.h"
#include "clock.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sys/time.h>
extern "C" int32_t runtime_guest_errno(int);
namespace GuestClock {
// OpenCrown's portable local-clock helper, with Odin's checked pointer boundary
// below. SceRtcDateTime is six u16 fields followed by u32 microseconds (16 bytes).
inline int32_t rtcLocalClock(uint16_t *date) {
    if (!date) return int32_t(0x80b50002);
    const auto now=std::chrono::system_clock::now();
    const std::time_t seconds=std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (!localtime_r(&seconds,&local)) return int32_t(0x80b50002);
    date[0]=uint16_t(local.tm_year+1900);date[1]=uint16_t(local.tm_mon+1);date[2]=uint16_t(local.tm_mday);
    date[3]=uint16_t(local.tm_hour);date[4]=uint16_t(local.tm_min);date[5]=uint16_t(local.tm_sec);
    const uint32_t usec=uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000);
    std::memcpy(date+6,&usec,sizeof(usec));
    return 0;
}
inline bool call(const char *name,State &state,Memory &memory,uint64_t &result) {
    result=0;
    if(!std::strcmp(name,"sceRtcGetCurrentClockLocalTime")) {
        if(!state.gpr.rdi.qword){result=0x80b50002u;return true;}
        auto output=guestPointer(memory,state.gpr.rdi.qword,16,true);if(!output)return true;
        uint16_t date[8]{};const int32_t status=rtcLocalClock(date);
        if(!status)std::memcpy(output,date,sizeof(date));
        result=uint32_t(status);return true;
    }
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
