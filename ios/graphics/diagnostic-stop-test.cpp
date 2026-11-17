#include "diagnostic-stop.h"
#include <cassert>
#include <cstdio>
int main() {
    uint32_t ms=42;
    for(const char *s:{"", "0", "99", "1800001", "-100", "+100", "100x", "4294967296"}) {
        assert(!osl_diagnostic_stop_parse(s,ms));assert(ms==42);
    }
    assert(!osl_diagnostic_stop_parse(nullptr,ms));
    assert(osl_diagnostic_stop_parse("100",ms) && ms==100);
    assert(!osl_diagnostic_stop_due(10,ms,10.099));
    assert(osl_diagnostic_stop_due(10,ms,10.101));
    assert(!osl_diagnostic_stop_due(10,ms,9));
    assert(!osl_diagnostic_stop_due(10,0,100000));
    assert(osl_diagnostic_stop_parse("1800000",ms) && ms==1800000);
    assert(!osl_diagnostic_stop_due(10,ms,1809.99));
    assert(osl_diagnostic_stop_due(10,ms,1810));
    std::puts("bounded continuous diagnostic stop: parser and clock cases passed");
}
