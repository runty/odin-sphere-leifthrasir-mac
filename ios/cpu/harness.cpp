#include "runtime.h"
#include "fixtures.h"
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <sstream>
#include <stdexcept>

Memory *dispatch(State &state, uint64_t pc, Memory *m) {
    for (const auto &entry : entries) if (entry.pc == pc) return entry.function(state, pc, m);
    m->fault = "unresolved translated target";
    return m;
}
static uint64_t flags(const State &s) {
    return uint64_t(s.aflag.cf) | (uint64_t(s.aflag.pf) << 2) | (uint64_t(s.aflag.zf) << 6) |
           (uint64_t(s.aflag.sf) << 7) | (uint64_t(s.aflag.of) << 11);
}
static unsigned worker() {
    unsigned passed = 0;
    for (const auto &t : fixture_cases) {
        State s{};
        Memory m;
        std::vector<uint8_t> data(8192, 0x55), stack(4096), globals(4);
        mapRegion(m, 0x100000, data.data(), data.size());
        mapRegion(m, 0x200000, stack.data(), stack.size());
        mapRegion(m, image_base + fios_storage, globals.data(), globals.size());
        uint64_t arg = t.a;
        if (t.kind == 0) {
            std::memset(data.data(), 0, data.size());
            uint64_t nested = t.a == 0 ? 0 : 0x100400;
            std::memcpy(data.data() + 0x1d8, &nested, 8);
            uint32_t flag = t.a == 2 ? 1 : 0;
            std::memcpy(data.data() + 1024 + 0xa0, &flag, 4);
            arg = 0x100000;
        } else if (t.kind == 1) {
            data[t.a + t.b] = 0;
            arg = 0x100000 + t.a;
        }
        s.gpr.rdi.qword = arg;
        s.gpr.rbx.qword = 0x1234; s.gpr.rbp.qword = 0x2345;
        s.gpr.r12.qword = 0x3456; s.gpr.r13.qword = 0x4567;
        s.gpr.r14.qword = 0x5678; s.gpr.r15.qword = 0x6789;
        s.gpr.rsp.qword = 0x200800;
        s.aflag.zf = 1; s.aflag.pf = 1;
        const uint64_t sentinel = 0xfeedface;
        std::memcpy(stack.data() + 0x800, &sentinel, 8);
        const auto original = data;
        dispatch(s, image_base + t.offset, &m);
        uint32_t stored = 0;
        std::memcpy(&stored, globals.data(), 4);
        if (!m.fault.empty() || s.gpr.rax.qword != t.expected || flags(s) != t.flags ||
            (t.kind == 2 && stored != t.observed) || s.gpr.rsp.qword != 0x200808 ||
            s.gpr.rip.qword != sentinel || m.returns != 1 || data != original ||
            s.gpr.rbx.qword != 0x1234 || s.gpr.rbp.qword != 0x2345 ||
            s.gpr.r12.qword != 0x3456 || s.gpr.r13.qword != 0x4567 ||
            s.gpr.r14.qword != 0x5678 || s.gpr.r15.qword != 0x6789)
            throw std::runtime_error("real routine comparison failed: kind=" + std::to_string(t.kind) +
                                     " a=" + std::to_string(t.a) + " b=" + std::to_string(t.b) +
                                     " actual=" + std::to_string(s.gpr.rax.qword) + " flags=" + std::to_string(flags(s)) +
                                     " fault=" + m.fault);
        ++passed;
    }
    // Explicit guest addressing must reject edges, overflow and overlapping maps.
    Memory m;
    std::array<uint8_t, 16> data{};
    mapRegion(m, 0x100000, data.data(), data.size());
    for (uint64_t a : {uint64_t(0xfffff), uint64_t(0x100009), UINT64_MAX}) {
        if (guestPointer(m, a, 8)) throw std::runtime_error("invalid guest pointer accepted");
        ++passed;
    }
    if (guestPointer(m, 0x100008, 8) != data.data() + 8) throw std::runtime_error("last valid guest qword rejected");
    ++passed;
    bool rejected = false;
    try { mapRegion(m, 0x100008, data.data(), data.size()); } catch (const std::runtime_error &) { rejected = true; }
    if (!rejected) throw std::runtime_error("overlapping guest mapping accepted");
    ++passed;
    State s{}; m.fault.clear(); dispatch(s, 0xbad, &m);
    if (m.fault.empty()) throw std::runtime_error("missing trace silently accepted");
    return passed + 1;
}
extern "C" const char *osl_mobile_cpu_report() {
    static std::string report;
    auto start = std::chrono::steady_clock::now();
    try {
        auto a = std::async(std::launch::async, worker), b = std::async(std::launch::async, worker);
        unsigned passed = a.get() + b.get();
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::ostringstream out;
        out << "{\"failed\":0,\"passed\":" << passed << ",\"workers\":2,\"real_module_routines\":3,"
            << "\"game_booted\":false,\"backend\":\"Remill AOT\",\"elapsed_ms\":" << elapsed
            << ",\"corpus_sha256\":\"" << corpus_hash << "\"}";
        report = out.str();
    } catch (const std::exception &e) {
        report = std::string("{\"failed\":1,\"game_booted\":false,\"error\":\"") + e.what() + "\"}";
    }
    return report.c_str();
}
