#include "../../gpu/shadps4/core/libraries/videoout/idle_redraw_policy.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    OslIdleRedrawPolicy policy;
    assert(policy.allow_closed_redraw()); // Initial launch may show a blank frame.
    for (unsigned round=0;round<256;++round) {
        policy.on_open();
        std::atomic<bool> port_open{true},observed{false},bad{false};
        std::thread present([&] {
            while (port_open.load(std::memory_order_acquire)) std::this_thread::yield();
            if (policy.allow_closed_redraw()) bad=true;
            observed=true;
        });
        policy.on_close();
        port_open.store(false,std::memory_order_release);
        present.join();assert(observed && !bad && !policy.allow_closed_redraw());
    }
    policy.on_open();assert(policy.allow_closed_redraw());
    std::puts("Idle redraw initial, close, concurrent observation and reopen: passed");
}
