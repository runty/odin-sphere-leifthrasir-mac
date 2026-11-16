#pragma once
#include <atomic>

// A closed game port no longer needs automatic blank redraws. Before the
// first open, blank presentation remains available; reopening restores it.
// This policy does not drain queued frames or quiesce other GPU work.
class OslIdleRedrawPolicy {
    std::atomic<bool> closed{false};
public:
    void on_open() noexcept { closed.store(false, std::memory_order_release); }
    void on_close() noexcept { closed.store(true, std::memory_order_release); }
    bool allow_closed_redraw() const noexcept { return !closed.load(std::memory_order_acquire); }
};
