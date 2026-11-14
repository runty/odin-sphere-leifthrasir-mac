#pragma once
#include "remill/Arch/X86/Runtime/State.h"
#include <csetjmp>
#include <cstdint>
#include <string>
#include <vector>

// Guest addresses remain guest addresses; host/device malloc placement is irrelevant.
struct Memory {
    struct Region {
        uint64_t base;
        uint8_t *data;
        size_t size;
    };
    std::vector<Region> regions;
    std::string fault;
    unsigned returns = 0;
    std::jmp_buf *recovery = nullptr;
    uint64_t missing_pc = 0, fault_address = 0;
    bool atomic_held = false;
    Memory *(*dispatcher)(State &, uint64_t, Memory *) = nullptr;
    ~Memory();
};
using Lifted = Memory *(*)(State &, uint64_t, Memory *);
void mapRegion(Memory &, uint64_t, uint8_t *, size_t);
uint8_t *guestPointer(Memory &, uint64_t, size_t);
Memory *dispatch(State &, uint64_t, Memory *);
void stopGuest(Memory &, const char *, uint64_t pc = 0);
extern "C" const char *osl_mobile_cpu_report();
