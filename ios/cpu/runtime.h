#pragma once
#include "remill/Arch/X86/Runtime/State.h"
#include <csetjmp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <shared_mutex>
#include <vector>

// Every transfer is checked against explicit regions, including native-address identity mappings.
struct Memory {
    struct Region {
        uint64_t base;
        uint8_t *data;
        size_t size;
        bool writable;
    };
    Memory() = default;
    Memory(const Memory &) = delete;
    Memory &operator=(const Memory &) = delete;
    // Descriptors are shared by worker CPUs. Backing storage belongs to the
    // session and must remain valid until its workers and GPU have quiesced.
    struct Mappings { std::shared_mutex mutex; std::vector<Region> regions; };
    std::shared_ptr<Mappings> mappings = std::make_shared<Mappings>();
    std::atomic<bool> *stop_requested = nullptr, *cancel_requested = nullptr;
    uint64_t checkpoints = 0, checkpoint_limit = 1000000;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    // Fault reasons are static literals: raising a fault must not allocate or
    // throw through lifted LLVM code.
    std::string_view fault;
    unsigned returns = 0;
    std::jmp_buf *recovery = nullptr;
    uint64_t missing_pc = 0, fault_address = 0;
    bool atomic_held = false;
    Memory *(*dispatcher)(State &, uint64_t, Memory *) = nullptr;
    uint8_t *(*external_pointer)(uint64_t, size_t, bool) = nullptr;
    ~Memory();
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
void mapRegion(Memory &, uint64_t, uint8_t *, size_t, bool writable = true);
bool unmapRegion(Memory &,uint64_t base);
const char *guestInterruption(const Memory &);
// Host services use the same recursive lock as checked CPU transfers. Release
// it before any blocking wait; reacquire it before copying guest outputs.
class GuestMemoryScope {
    bool held_ = false;
public:
    GuestMemoryScope();
    ~GuestMemoryScope();
    GuestMemoryScope(const GuestMemoryScope &) = delete;
    GuestMemoryScope &operator=(const GuestMemoryScope &) = delete;
    void lock();
    void unlock();
};
uint8_t *guestPointer(Memory &, uint64_t, size_t, bool writing = false);
Memory *dispatch(State &, uint64_t, Memory *);
void stopGuest(Memory &, const char *, uint64_t pc = 0);
extern "C" const char *osl_mobile_cpu_report();
