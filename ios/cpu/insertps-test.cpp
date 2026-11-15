// Built twice by test-insertps.py. Both executables run the same instruction bytes.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#ifdef __x86_64__
#include <csignal>
#include <csetjmp>
#include <sys/mman.h>
#include <unistd.h>
#include <xmmintrin.h>
extern "C" uint64_t invoke(void (*)(), const void *, void *, const void *, uint64_t);
#else
#include "runtime.h"
#endif

struct Form { bool legacy; unsigned dest; int inserted; };
#include "forms.h"
#include "functions.h"

using Vector = std::array<uint32_t, 8>;
using Registers = std::array<Vector, 3>;
using Results = std::array<Registers, 256>;
static constexpr std::array<unsigned, 3> indices{0, 3, 9};
static constexpr Registers input{{
    {0x7fc01234, 0x80000000, 0xffffffff, 1, 0x7f800000, 0xff800000, 42, 0xabcd},
    {0x7fa54321, 0x80000001, 0, 0x12345678, 0xff800000, 15, 16, 17},
    {0x87654321, 0x13579bdf, 0x2468ace0, 0xabcdef01, 0xffefffff, 19, 20, 21}
}};
static constexpr uint32_t scalar = 0xffc5c001;
static constexpr uint64_t arithmetic_flags = 0x8d5;
static_assert(sizeof(Registers) == 96);

static void verify(const Results &out, size_t form) {
    const Form &f = forms[form];
    for (unsigned imm = 0; imm < out.size(); ++imm) {
        Registers expected = input;
        Vector dest = input[0];
        dest[(imm >> 4) & 3] = f.inserted < 0 ? scalar : input[f.inserted == 0 ? 0 : 1][imm >> 6];
        for (unsigned lane = 0; lane < 4; ++lane) {
            if (imm & (1u << lane)) dest[lane] = 0;
            if (!f.legacy) dest[4 + lane] = 0;
        }
        for (size_t s = 0; s < indices.size(); ++s) {
            if (indices[s] == f.dest) expected[s] = dest;
            if (out[imm][s] != expected[s]) {
                std::fprintf(stderr, "Mismatch at form %zu immediate %u ymm%u\n", form, imm, indices[s]);
                std::abort();
            }
        }
    }
}

#ifdef __x86_64__
static sigjmp_buf recovery;
static uintptr_t guard;
static volatile sig_atomic_t expecting_fault;

static void access_fault(int signal, siginfo_t *info, void *) {
    if (expecting_fault && reinterpret_cast<uintptr_t>(info->si_addr) == guard)
        siglongjmp(recovery, 1);
    _exit(128 + signal);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = std::fopen(argv[1], "wb");
    assert(file);
    const auto page = sysconf(_SC_PAGESIZE);
    auto *buffer = static_cast<uint8_t *>(mmap(nullptr, 2 * page, PROT_READ | PROT_WRITE,
                                                MAP_ANON | MAP_PRIVATE, -1, 0));
    assert(buffer != MAP_FAILED);
    auto *end = buffer + page;
    guard = reinterpret_cast<uintptr_t>(end);
    std::memcpy(end - 4, &scalar, 4);
    assert(mprotect(buffer, page, PROT_READ) == 0 && mprotect(end, page, PROT_NONE) == 0);
    struct sigaction handler{};
    handler.sa_flags = SA_SIGINFO;
    handler.sa_sigaction = access_fault;
    sigemptyset(&handler.sa_mask);
    assert(!sigaction(SIGSEGV, &handler, nullptr) && !sigaction(SIGBUS, &handler, nullptr));
    const auto initial_csr = _mm_getcsr();
    unsigned faults = 0;
    Results out{};
    for (size_t i = 0; i < std::size(forms); ++i) {
        for (unsigned flags_set : {0u, 1u}) {
            const unsigned csr = flags_set ? 0x1f95 : 0x1f80;
            _mm_setcsr(csr);
            const uint64_t flags = invoke(native[i], &input, &out, end - 4, flags_set ? 0xad7 : 0x202);
            const auto after = _mm_getcsr();
            _mm_setcsr(initial_csr);
            assert((flags & arithmetic_flags) == (flags_set ? arithmetic_flags : 0) && after == csr);
            verify(out, i);
            assert(std::fwrite(&out, sizeof(out), 1, file) == 1);
        }
        if (forms[i].inserted < 0) {
            out = {};
            expecting_fault = 1;
            if (sigsetjmp(recovery, 1) == 0) {
                invoke(native[i], &input, &out, end - 3, 0x202);
                std::abort(); // The first INSERTPS reads one byte from the guard page.
            }
            expecting_fault = 0;
            ++faults;
            assert(out == Results{});
        }
    }
    assert(faults == 3 && std::fclose(file) == 0 && munmap(buffer, 2 * page) == 0);
    std::puts("Rosetta: 10 operand arrangements x 256 immediates x 2 flag patterns, three guard-page faults");
}
#else
static void run_case(size_t form, bool flags_set, bool truncated, Results &out) {
    State state{};
    auto memory = std::make_unique<Memory>();
    Registers regs = input;
    uint32_t word = scalar;
    uint64_t return_pc = 0xfeedface;
    constexpr uint64_t scalar_address = 0x400ffd;
    mapRegion(*memory, 0x200000, reinterpret_cast<uint8_t *>(&regs), sizeof(regs), false);
    mapRegion(*memory, 0x300000, reinterpret_cast<uint8_t *>(&out), sizeof(out));
    mapRegion(*memory, scalar_address, reinterpret_cast<uint8_t *>(&word), truncated ? 3 : 4, false);
    mapRegion(*memory, 0x500000, reinterpret_cast<uint8_t *>(&return_pc), sizeof(return_pc));
    state.gpr.rdi.qword = 0x200000;
    state.gpr.rsi.qword = 0x300000;
    state.gpr.rdx.qword = scalar_address;
    state.gpr.rsp.qword = 0x500000;
    state.aflag.cf = state.aflag.pf = state.aflag.af = state.aflag.zf =
        state.aflag.sf = state.aflag.of = flags_set;
    uint32_t csr = flags_set ? 0x1f95 : 0x1f80;
    state.x87.fxsave64.mxcsr.flat = csr;
    std::jmp_buf handler;
    memory->recovery = &handler;
    if (setjmp(handler) == 0) {
        lifted[form](state, pcs[form], memory.get());
        assert(!truncated && memory->fault.empty() && memory->returns == 1);
        assert(state.gpr.rip.qword == return_pc && state.gpr.rsp.qword == 0x500008);
        assert(state.aflag.cf == flags_set && state.aflag.pf == flags_set && state.aflag.af == flags_set &&
               state.aflag.zf == flags_set && state.aflag.sf == flags_set && state.aflag.of == flags_set);
        assert(state.x87.fxsave64.mxcsr.flat == csr);
    } else {
        assert(truncated && memory->fault == "unmapped guest memory");
        assert(memory->fault_address == scalar_address && !memory->returns && out == Results{});
    }
    memory->recovery = nullptr;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *file = std::fopen(argv[1], "rb");
    assert(file);
    Results out{}, reference{};
    for (size_t i = 0; i < std::size(forms); ++i) {
        for (bool flags_set : {false, true}) {
            assert(std::fread(&reference, sizeof(reference), 1, file) == 1);
            run_case(i, flags_set, false, out);
            verify(out, i);
            assert(out == reference);
        }
        if (forms[i].inserted < 0) {
            out = {};
            run_case(i, false, true, out);
        }
    }
    assert(std::fgetc(file) == EOF && !std::ferror(file) && std::fclose(file) == 0);
    std::puts("arm64: registers, upper lanes, flags and 4-byte memory accesses match; three truncation faults");
}
#endif
