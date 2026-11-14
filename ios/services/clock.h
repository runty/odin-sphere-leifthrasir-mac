#pragma once
#include <cstdint>
#include <time.h>
// One process epoch shared by the checked guest and native renderer. TSC ticks
// are monotonic raw nanoseconds, not an ARM hardware counter of another rate.
namespace MobileClock {
inline constexpr uint64_t frequency=1000000000;
inline const uint64_t process_start=clock_gettime_nsec_np(CLOCK_MONOTONIC);
inline uint64_t tsc(){return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);}
inline uint64_t processCounter(){return clock_gettime_nsec_np(CLOCK_MONOTONIC)-process_start;}
inline uint64_t processTime(){return processCounter()/1000;}
inline bool hostClock(uint32_t id,clockid_t &host) {
    switch(id) {
    case 0:case 9:case 10:case 13:host=CLOCK_REALTIME;return true;
    case 4:case 11:case 12:case 5:case 7:case 8:case 16:case 17:case 18:case 19:host=CLOCK_MONOTONIC;return true;
    case 14:host=CLOCK_THREAD_CPUTIME_ID;return true;
    case 2:case 15:host=CLOCK_PROCESS_CPUTIME_ID;return true;
    default:return false;
    }
}
}
