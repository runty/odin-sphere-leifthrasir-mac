#pragma once
#include "remill/Arch/X86/Runtime/State.h"
#include <csetjmp>
#include <chrono>
#include <cstdint>
#include <string>
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
    std::vector<Region> regions;
    uint64_t checkpoints = 0, checkpoint_limit = 1000000;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::string fault;
    unsigned returns = 0;
    std::jmp_buf *recovery = nullptr;
    uint64_t missing_pc = 0, fault_address = 0;
    bool atomic_held = false;
    Memory *(*dispatcher)(State &, uint64_t, Memory *) = nullptr;
    ~Memory();
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
void mapRegion(Memory &, uint64_t, uint8_t *, size_t, bool writable = true);
uint8_t *guestPointer(Memory &, uint64_t, size_t, bool writing = false);
Memory *dispatch(State &, uint64_t, Memory *);
void stopGuest(Memory &, const char *, uint64_t pc = 0);
extern "C" const char *osl_mobile_cpu_report();
