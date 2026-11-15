// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace Shader::PortableSrt {
enum class Op : uint32_t {
    Constant, FlatWord, Add, Subtract, Multiply, ShiftLeft, ShiftRight,
    And, Or, Xor, Not, Minimum, Maximum, Extract, PushPointer, PopPointer, Read
};
struct Instruction {
    Op op;
    uint32_t a = 0, b = 0, c = 0, destination = 0;
};
struct Header {
    uint32_t magic = 0x31545253, version = 1, count = 0, flat_words = 16;
};
static_assert(sizeof(Header) == 16 && sizeof(Instruction) == 20);
constexpr uint32_t max_instructions = 65536;
constexpr uint32_t max_words = 65536;

inline void Validate(std::span<const Instruction> code, uint32_t words) {
    if (code.size() > max_instructions || words < 16 || words > max_words)
        throw std::runtime_error("SRT program exceeds bounds");
    unsigned depth = 0;
    for (size_t pc = 0; pc < code.size(); ++pc) {
        const auto& i = code[pc];
        auto ref = [&](uint32_t index) {
            if (index >= pc) throw std::runtime_error("SRT forward or invalid reference");
        };
        switch (i.op) {
        case Op::Constant: break;
        case Op::FlatWord:
            if (i.a >= words) throw std::runtime_error("SRT flat input exceeds buffer");
            break;
        case Op::Read:
            ref(i.a);
            if (i.destination < 16 || i.destination >= words)
                throw std::runtime_error("SRT flat output exceeds buffer");
            break;
        case Op::PushPointer:
            ref(i.a);
            if (++depth > 64) throw std::runtime_error("SRT pointer nesting exceeds bounds");
            break;
        case Op::PopPointer:
            if (!depth) throw std::runtime_error("SRT pointer stack underflow");
            --depth;
            break;
        case Op::Not: ref(i.a); break;
        case Op::Extract: ref(i.a); ref(i.b); ref(i.c); break;
        case Op::Add: case Op::Subtract: case Op::Multiply:
        case Op::ShiftLeft: case Op::ShiftRight:
        case Op::And: case Op::Or: case Op::Xor: case Op::Minimum: case Op::Maximum:
            ref(i.a); ref(i.b); break;
        default: throw std::runtime_error("Unknown SRT operation");
        }
    }
    if (depth) throw std::runtime_error("Unbalanced SRT pointer stack");
}

inline std::vector<uint8_t> Encode(std::span<const Instruction> code, uint32_t words) {
    Validate(code, words);
    Header header;
    header.count = static_cast<uint32_t>(code.size());
    header.flat_words = words;
    std::vector<uint8_t> bytes(sizeof(header) + code.size_bytes());
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (!code.empty()) std::memcpy(bytes.data() + sizeof(header), code.data(), code.size_bytes());
    return bytes;
}

struct Program {
    Header header;
    std::vector<Instruction> code;
};
inline Program Decode(const uint8_t* bytes, size_t size) {
    if (!bytes || size < sizeof(Header)) throw std::runtime_error("Truncated SRT header");
    Program p;
    std::memcpy(&p.header, bytes, sizeof(Header));
    if (p.header.magic != Header{}.magic || p.header.version != 1 ||
        p.header.count > max_instructions ||
        size != sizeof(Header) + size_t(p.header.count) * sizeof(Instruction))
        throw std::runtime_error("Invalid SRT program header");
    p.code.resize(p.header.count);
    if (!p.code.empty()) std::memcpy(p.code.data(), bytes + sizeof(Header), p.code.size() * sizeof(Instruction));
    Validate(p.code, p.header.flat_words);
    return p;
}

// Read returns zero on an inaccessible descriptor, matching the native walker's
// faulted-load behavior. The caller supplies the platform's checked host read.
template<class Read>
inline void Execute(const Program& program, const uint32_t* user, uint32_t* flat, Read read) {
    std::vector<uint32_t> values(program.code.size());
    uintptr_t pointer = reinterpret_cast<uintptr_t>(user);
    std::vector<uintptr_t> pointers;
    pointers.reserve(64);
    auto load = [&](uint32_t offset, void* target, size_t bytes) {
        const uint32_t displacement = offset << 2;
        if (!pointer || displacement > std::numeric_limits<uintptr_t>::max() - pointer)
            return false;
        return read(pointer + displacement, target, bytes);
    };
    for (size_t pc = 0; pc < program.code.size(); ++pc) {
        const auto& i = program.code[pc];
        uint32_t result = 0;
        switch (i.op) {
        case Op::Constant: result = i.a; break;
        case Op::FlatWord: result = flat[i.a]; break;
        case Op::Add: result = values[i.a] + values[i.b]; break;
        case Op::Subtract: result = values[i.a] - values[i.b]; break;
        case Op::Multiply: result = values[i.a] * values[i.b]; break;
        case Op::ShiftLeft: result = values[i.a] << (values[i.b] & 31); break;
        case Op::ShiftRight: result = values[i.a] >> (values[i.b] & 31); break;
        case Op::And: result = values[i.a] & values[i.b]; break;
        case Op::Or: result = values[i.a] | values[i.b]; break;
        case Op::Xor: result = values[i.a] ^ values[i.b]; break;
        case Op::Not: result = ~values[i.a]; break;
        case Op::Minimum: result = std::min(values[i.a], values[i.b]); break;
        case Op::Maximum: result = std::max(values[i.a], values[i.b]); break;
        case Op::Extract:
            result = (values[i.a] >> (values[i.b] & 31)) & ((uint32_t(1) << (values[i.c] & 31)) - 1);
            break;
        case Op::PushPointer: {
            pointers.push_back(pointer);
            uint64_t next = 0;
            if (!load(values[i.a], &next, sizeof(next))) next = 0;
            pointer = next & 0xffffffffffffULL;
            break;
        }
        case Op::PopPointer: pointer = pointers.back(); pointers.pop_back(); break;
        case Op::Read:
            if (!load(values[i.a], &result, sizeof(result))) result = 0;
            flat[i.destination] = result;
            break;
        }
        values[pc] = result;
    }
}
} // namespace Shader::PortableSrt
