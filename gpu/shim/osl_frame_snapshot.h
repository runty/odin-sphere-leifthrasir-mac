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
    uint64_t every=0;
    uint64_t count=1;

    bool Captures(uint64_t current) const {
        if (current < frame) return false;
        if (count == 1) return current == frame;
        if (!every) return false;
        return (current-frame) % every == 0 && (current-frame) / every < count;
    }

    std::string PathAt(uint64_t current) const {
        return count == 1 ? path : path+".frame-"+std::to_string(current)+".png";
    }
};

// Developer-only bounded readbacks. No request means no GPU allocation/wait.
// Repetition requires both interval and count; ordinary one-shot paths stay exact.
inline std::optional<Request> Parse(const char* path, const char* at, const char* source=nullptr,
                                    const char* every=nullptr, const char* count=nullptr) {
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
    uint64_t interval=0, total=1;
    if (every || count) {
        if (!every || !count) return std::nullopt;
        const auto number=[](const char* text, uint64_t& value) {
            const char* end=text+std::strlen(text);
            const auto result=std::from_chars(text,end,value);
            return result.ec==std::errc{} && result.ptr==end;
        };
        if (!number(every,interval) || !number(count,total) ||
            !interval || interval>1000000 || total<2 || total>64) return std::nullopt;
        // Division avoids overflow before checking the final requested frame.
        if (total-1 > (1000000-frame)/interval || std::strlen(path)>4064)
            return std::nullopt;
    }
    return Request{path, frame, selected, interval, total};
}
} // namespace OslFrameSnapshot
