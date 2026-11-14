// Synthetic stress/fault tests for the shared checked-memory core.
#include "runtime.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <thread>
#include <limits>
extern "C" uint32_t __remill_read_memory_32(Memory *, uint64_t);
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *, uint64_t, uint32_t &, uint32_t);
extern "C" Memory *__remill_atomic_begin(Memory *);
extern "C" Memory *__remill_atomic_end(Memory *);

int main() {
    uint32_t counter = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
        Memory memory;
        mapRegion(memory, 0x1000, reinterpret_cast<uint8_t *>(&counter), 4);
        for (int n = 0; n < 1000; ++n) {
            uint32_t expected = __remill_read_memory_32(&memory, 0x1000);
            for (;;) {
                const uint32_t wanted = expected;
                __remill_compare_exchange_memory_32(&memory, 0x1000, expected, wanted + 1);
                if (expected == wanted) break;
            }
        }
        assert(memory.fault.empty());
    });
    for (auto &worker : workers) worker.join();
    assert(counter == 4000);
    auto memory = std::make_unique<Memory>();
    mapRegion(*memory, 0x1000, reinterpret_cast<uint8_t *>(&counter), 4);
    bool rejected = false;
    try { mapRegion(*memory, 0x1003, reinterpret_cast<uint8_t *>(&counter), 4); }
    catch (...) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { mapRegion(*memory, std::numeric_limits<uint64_t>::max(), reinterpret_cast<uint8_t *>(&counter), 4); }
    catch (...) { rejected = true; }
    assert(rejected);
    assert(guestPointer(*memory, 0x1003, 1) == reinterpret_cast<uint8_t *>(&counter) + 3);
    std::jmp_buf recovery;
    memory->recovery = &recovery;
    if (setjmp(recovery) == 0) {
        __remill_atomic_begin(memory.get());
        __remill_read_memory_32(memory.get(), 0x1001);
        assert(false && "Invalid transfer did not leave guest execution");
    }
    memory->recovery = nullptr;
    assert(!memory->atomic_held && memory->fault_address == 0x1001);
    // A leaked outer atomic lock would block this different native thread.
    auto probe = std::async(std::launch::async, [&] {
        Memory other;
        mapRegion(other, 0x1000, reinterpret_cast<uint8_t *>(&counter), 4);
        __remill_atomic_begin(&other);
        const auto value = __remill_read_memory_32(&other, 0x1000);
        __remill_atomic_end(&other);
        return value;
    });
    if (probe.wait_for(std::chrono::seconds(2)) != std::future_status::ready) std::_Exit(2);
    assert(probe.get() == 4000);
    puts("4000 concurrent CAS increments; overlap/overflow/boundary checks; atomic-fault recovery: PASS");
}
