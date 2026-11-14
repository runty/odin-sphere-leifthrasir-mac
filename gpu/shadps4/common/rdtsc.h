// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/arch.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "common/types.h"
#if defined(ARCH_X86_64) && !defined(_MSC_VER)
#include <x86intrin.h>
#endif

namespace Common {

#ifdef _MSC_VER
__forceinline static u64 FencedRDTSC() {
#ifdef ARCH_X86_64
    _mm_lfence();
    _ReadWriteBarrier();
    const u64 result = __rdtsc();
    _mm_lfence();
    _ReadWriteBarrier();
    return result;
#else
#error "Missing FencedRDTSC() implementation for target CPU architecture."
#endif
}
#else
static inline u64 FencedRDTSC() {
#ifdef ARCH_X86_64
    u64 eax;
    u64 edx;
    asm volatile("lfence\n\t"
                 "rdtsc\n\t"
                 "lfence\n\t"
                 : "=a"(eax), "=d"(edx));
    return (edx << 32) | eax;
#elif defined(ARCH_ARM64)
    u64 ret;
    asm volatile("isb\n\t"
                 "mrs %0, cntvct_el0\n\t"
                 "isb\n\t"
                 : "=r"(ret)::"memory");
    return ret;
#else
#error "Missing FencedRDTSC() implementation for target CPU architecture."
#endif
}
#endif

// Unfenced host ticks for profiling and a portable spin-loop hint.
// Ticks are compared only within the same host clock domain.
static inline u64 ReadHostCounter() {
#if defined(ARCH_X86_64)
    return __rdtsc();
#elif defined(ARCH_ARM64)
    u64 value;
    asm volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#else
#error "Missing host counter implementation"
#endif
}
static inline void SpinPause() {
#if defined(ARCH_X86_64)
    _mm_pause();
#elif defined(ARCH_ARM64) && defined(_MSC_VER)
    __yield();
#elif defined(ARCH_ARM64)
    asm volatile("yield");
#endif
}

u64 EstimateRDTSCFrequency();

} // namespace Common
