// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace OslFrameSnapshot {
enum class Source { Game, Display };
struct Request {
    std::string path;
    uint64_t frame;
    Source source=Source::Game;
};

// A developer-only, one-shot readback. No request means no GPU allocation/wait.
inline std::optional<Request> Parse(const char* path, const char* at, const char* source=nullptr) {
    if (!path || !*path || std::strlen(path) > 4096) return std::nullopt;
    uint64_t frame = 1000;
    if (at) {
        const char* end = at + std::strlen(at);
        const auto result = std::from_chars(at, end, frame);
        if (result.ec != std::errc{} || result.ptr != end) return std::nullopt;
    }
    if (!frame || frame > 1000000) return std::nullopt;
    Source selected=Source::Game;
    if(source) {
        if(!std::strcmp(source,"display"))selected=Source::Display;
        else if(std::strcmp(source,"game"))return std::nullopt;
    }
    return Request{path, frame, selected};
}
} // namespace OslFrameSnapshot
