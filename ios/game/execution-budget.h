#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

// Zero is an explicit developer-only continuous session, not a silent default.
// It is valid only when the frontend supplies a cancellation signal.
struct OslExecutionBudget {
    using Clock=std::chrono::steady_clock;
    Clock::time_point deadline;
    uint64_t checkpoints,dispatches;
};
inline bool osl_execution_budget_valid(uint32_t ms,bool cancellable) {
    return ms==0 ? cancellable : ms>=100 && ms<=600000;
}
inline OslExecutionBudget osl_execution_budget(uint32_t ms,OslExecutionBudget::Clock::time_point now) {
    using Clock=OslExecutionBudget::Clock;
    if (!ms) return {Clock::time_point::max(),UINT64_MAX,UINT64_MAX};
    const auto delta=std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(ms));
    const auto end=now>Clock::time_point::max()-delta ? Clock::time_point::max() : now+delta;
    return {end,std::max<uint64_t>(1000000,uint64_t(ms)*500),std::max<uint64_t>(100000,uint64_t(ms)*50)};
}
