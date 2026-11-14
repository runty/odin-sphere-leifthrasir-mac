// SPDX-License-Identifier: GPL-2.0-or-later
#include "osl_triangle_fan.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

static std::int64_t Area(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    const auto x = [](std::uint32_t v) { return std::int64_t(v % 97); };
    const auto y = [](std::uint32_t v) { return std::int64_t(v % 71); };
    return (x(b)-x(a))*(y(c)-y(a)) - (y(b)-y(a))*(x(c)-x(a));
}

template <typename Index>
bool Check(unsigned& cases) {
    for (unsigned count = 3; count <= 4096; ++count) {
        std::vector<Index> input(count);
        for (unsigned i = 0; i < count; ++i) input[i] = static_cast<Index>(i * 7919u);
        input.back() = std::numeric_limits<Index>::max(); // valid index with restart disabled
        for (bool first : {false, true}) {
            constexpr std::uint32_t guard = 0xdecafbad;
            std::vector<std::uint32_t> storage((count - 2) * 3 + 2, guard);
            const std::span output{storage.data() + 1, storage.size() - 2};
            if (!OslTriangleFan::Expand<Index>(input, output, first)) return false;
            if (storage.front() != guard || storage.back() != guard) return false;
            for (unsigned i = 0; i < count - 2; ++i) {
                const auto a = output[3*i], b = output[3*i+1], c = output[3*i+2];
                std::array<std::uint32_t, 3> actual{a,b,c}, expected{input[0],input[i+1],input[i+2]};
                std::sort(actual.begin(),actual.end()); std::sort(expected.begin(),expected.end());
                if (actual != expected || Area(a,b,c) != Area(input[0],input[i+1],input[i+2])) return false;
                if ((first ? a : c) != input[first ? i+1 : i+2]) return false;
            }
            ++cases;
        }
    }
    return true;
}

int main() {
    const std::uint16_t input[] = {10,20,30,40};
    std::array<std::uint32_t,6> output{};
    if (!OslTriangleFan::Expand<std::uint16_t>(input,output,true) ||
        output != std::array<std::uint32_t,6>{20,30,10,30,40,10}) return 1;
    if (!OslTriangleFan::Expand<std::uint16_t>(input,output,false) ||
        output != std::array<std::uint32_t,6>{10,20,30,10,30,40}) return 2;
    if (OslTriangleFan::Expand<std::uint16_t>({},output,false) ||
        OslTriangleFan::Expand<std::uint16_t>(input,std::span{output.data(),5},false)) return 3;
    unsigned cases = 0;
    if (!Check<std::uint16_t>(cases) || !Check<std::uint32_t>(cases)) return 4;
    std::printf("Triangle fans: %u coverage, winding, provoking-vertex and bounds cases passed\n", cases);
}
