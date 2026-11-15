// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/container/set.hpp>
#include <boost/container/small_vector.hpp>
#include "common/types.h"

namespace Serialization {
struct Archive;
}

namespace Shader {

using PFN_SrtWalker = void PS4_SYSV_ABI (*)(const u32* /*user_data*/, u32* /*flat_dst*/);
const u8* RegisterWalkerCode(const u8* ptr, size_t size);

void RunSrtWalker(const u8* code, size_t size, u32 flat_words, const u32* user_data, u32* flat_dst);

struct PersistentSrtInfo {
    // Special case when fetch shader uses step rates.
    struct SrtSharpReservation {
        u32 sgpr_base;
        u32 dword_offset;
        u32 num_dwords;
    };

    const u8* walker_code{};
    size_t walker_code_size{};
    u32 flattened_bufsize_dw = 16; // NumUserDataRegs

    void Walk(const u32* user_data, u32* flat_dst) const {
        if (walker_code) RunSrtWalker(walker_code, walker_code_size, flattened_bufsize_dw, user_data, flat_dst);
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
