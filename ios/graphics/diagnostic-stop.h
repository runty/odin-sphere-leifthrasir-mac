#pragma once
#include <charconv>
#include <cstdint>
#include <cstring>

// An explicit finite foreground stop for continuous-mode diagnostics. The
// normal bounded and continuous modes are unchanged when this is absent.
inline bool osl_diagnostic_stop_parse(const char *value, uint32_t &out) {
    if (!value || !*value) return false;
    uint32_t ms=0;const char *end=value+std::strlen(value);
    auto parsed=std::from_chars(value,end,ms);
    if(parsed.ec!=std::errc{} || parsed.ptr!=end || ms<100 || ms>1800000) return false;
    out=ms;return true;
}
inline bool osl_diagnostic_stop_due(double started, uint32_t ms, double now) {
    return ms && now>=started && now-started>=double(ms)/1000.0;
}
