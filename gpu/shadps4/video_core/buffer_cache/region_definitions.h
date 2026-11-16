// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/bit_array.h"
#include "common/arch.h"
#include "common/types.h"

namespace VideoCore {

// Native Apple arm64 can only protect whole 16 KiB host pages. Track dirtiness
// and watcher counts at that same granularity: rounding individual 4 KiB
// protections would otherwise unprotect still-watched neighbours or leave a
// faulting subpage permanently protected. Rosetta/x86 and other hosts retain
// their existing 4 KiB tracking policy.
#if defined(__APPLE__) && defined(ARCH_ARM64)
constexpr u64 TRACKER_PAGE_BITS = 14;
#else
constexpr u64 TRACKER_PAGE_BITS = 12;
#endif
constexpr u64 TRACKER_BYTES_PER_PAGE = 1ULL << TRACKER_PAGE_BITS;

constexpr u64 TRACKER_HIGHER_PAGE_BITS = 22; // each region is 4MB
constexpr u64 TRACKER_HIGHER_PAGE_SIZE = 1ULL << TRACKER_HIGHER_PAGE_BITS;
constexpr u64 TRACKER_HIGHER_PAGE_MASK = TRACKER_HIGHER_PAGE_SIZE - 1ULL;
constexpr u64 NUM_PAGES_PER_REGION = TRACKER_HIGHER_PAGE_SIZE / TRACKER_BYTES_PER_PAGE;

enum class Type {
    CPU,
    GPU,
};

using RegionBits = Common::BitArray<NUM_PAGES_PER_REGION>;

} // namespace VideoCore
