// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>

namespace OslTriangleFan {
// Vulkan drawing.html: fan p_i uses (i+1, i+2, 0) in first-provoking mode,
// and (0, i+1, i+2) in last-provoking mode. Cyclic order preserves winding.
template <typename Index>
bool Expand(std::span<const Index> input, std::span<std::uint32_t> output, bool first_provoking) {
    if (input.size() < 3 || output.size() != (input.size() - 2) * 3) return false;
    for (std::size_t i = 1; i + 1 < input.size(); ++i) {
        const auto base = (i - 1) * 3;
        output[base] = first_provoking ? input[i] : input[0];
        output[base + 1] = first_provoking ? input[i + 1] : input[i];
        output[base + 2] = first_provoking ? input[0] : input[i + 1];
    }
    return true;
}
}
