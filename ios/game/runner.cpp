// Discovery runner: real guest code, explicit gaps, no synthetic boot success.
#include "../cpu/runtime.h"
#include "image-layout.h"
#include "traces.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <pthread.h>
#include <sstream>
#include <unordered_map>

static bool readExact(const char *path, size_t size, std::vector<uint8_t> &bytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(size))
        return false;
    bytes.resize(size);
    input.seekg(0);
    return bool(input.read(reinterpret_cast<char *>(bytes.data()), size));
}

static bool matchesHash(const std::vector<uint8_t> &bytes, const char *expected) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
    char hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (unsigned i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i)
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return !std::strcmp(hex, expected);
}

struct HostMutex {
    pthread_mutex_t mutex;
    bool initialized = false;
    ~HostMutex() {
        if (initialized)
            pthread_mutex_destroy(&mutex);
    }
};
struct ThreadAttr {
    uint64_t stack = 1024 * 1024, stack_address = 0, affinity = 0x7f;
    int priority = 700;
};

struct Session {
    Memory memory;
    State state{};
    std::vector<uint8_t> image, stack;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> owned;
    const char *stage = "prepare", *missing_service = "";
    unsigned completed_modules = 0, calls = 0;
    uint64_t heap_api = 0, next_allocation = 0x30000000;
    uint64_t image_address = image_base, stack_address = 0x20000000;
    bool identity = false;
    std::unordered_map<uint64_t, int> mutex_attributes;
    std::unordered_map<uint64_t, std::unique_ptr<HostMutex>> mutexes;
    uint64_t flexible_bytes = 0;
    std::unordered_map<uint64_t, ThreadAttr> thread_attributes;
};
static thread_local Session *active;

static uint64_t load64(Memory &m, uint64_t address) {
    uint64_t value = 0;
    if (auto p = guestPointer(m, address, 8))
        std::memcpy(&value, p, 8);
    return value;
}
static void store64(Memory &m, uint64_t address, uint64_t value) {
    if (auto p = guestPointer(m, address, 8))
        std::memcpy(p, &value, 8);
}
static uint64_t allocate(Session &s, uint64_t size, bool gpu_visible = false) {
    if (!size || size > 128 * 1024 * 1024) {
        stopGuest(s.memory, "unsupported allocation size");
        return 0;
    }
    auto bytes = std::make_unique<std::vector<uint8_t>>(size);
    uint64_t address = s.next_allocation;
    if (s.identity && gpu_visible)
        address = reinterpret_cast<uint64_t>(bytes->data());
    else
        s.next_allocation += (size + 16383) & ~uint64_t(16383);
    if (address >= (uint64_t(1) << 40) || size > (uint64_t(1) << 40) - address) {
        stopGuest(s.memory, "host allocation exceeds guest GPU address range");
        return 0;
    }
    mapRegion(s.memory, address, bytes->data(), bytes->size());
    s.owned.push_back(std::move(bytes));
    return address;
}
static uint64_t orbisError(int e) { return e ? uint32_t(0x80020000u | unsigned(e)) : 0; }
static Memory *hostReturn(State &s, Memory *m, uint64_t value) {
    s.gpr.rax.qword = value;
    s.gpr.rip.qword = load64(*m, s.gpr.rsp.qword);
    s.gpr.rsp.qword += 8;
    return m;
}
static Memory *hostService(State &s, uint64_t pc, Memory *m) {
    Session &session = *active;
    const auto &import = imports[(pc - hle_base) / 0x1000];
    const char *name = import.symbol;
    if (!std::strcmp(name, "sceKernelGetProcParam"))
        return hostReturn(s, m, session.image_address + procparam);
    if (!std::strcmp(name, "scePthreadSelf"))
        return hostReturn(s, m, 0x40000000);
    if (!std::strcmp(name, "scePthreadAttrInit")) {
        if (!s.gpr.rdi.qword)
            return hostReturn(s, m, orbisError(22));
        const uint64_t address = allocate(session, 64);
        session.thread_attributes[address] = ThreadAttr{};
        store64(*m, s.gpr.rdi.qword, address);
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadAttrGet")) {
        const uint64_t address = load64(*m, s.gpr.rsi.qword);
        auto attr = session.thread_attributes.find(address);
        if (attr == session.thread_attributes.end())
            return hostReturn(s, m, orbisError(22));
        if (s.gpr.rdi.qword != 0x40000000)
            return hostReturn(s, m, orbisError(3));
        attr->second.stack = session.stack.size();
        attr->second.stack_address = session.stack_address;
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadAttrGetaffinity") ||
        !std::strcmp(name, "scePthreadAttrDestroy")) {
        const uint64_t address = load64(*m, s.gpr.rdi.qword);
        auto attr = session.thread_attributes.find(address);
        if (attr == session.thread_attributes.end())
            return hostReturn(s, m, orbisError(22));
        if (!std::strcmp(name, "scePthreadAttrGetaffinity"))
            store64(*m, s.gpr.rsi.qword, attr->second.affinity);
        else {
            session.thread_attributes.erase(attr);
            auto &regions = m->regions;
            regions.erase(std::remove_if(regions.begin(), regions.end(),
                                         [address](const auto &r) { return r.base == address; }),
                          regions.end());
            store64(*m, s.gpr.rdi.qword, 0);
        }
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "sceKernelMapFlexibleMemory") ||
        !std::strcmp(name, "sceKernelMapNamedFlexibleMemory")) {
        const uint64_t size = s.gpr.rsi.qword;
        if (!s.gpr.rdi.qword || !size || size % 4096 || (s.gpr.rdx.qword & ~uint64_t(0x37)))
            return hostReturn(s, m, orbisError(22));
        if (s.gpr.rdx.qword != 3 || (s.gpr.rcx.qword & 0x10)) {
            stopGuest(*m, "unsupported flexible-map protection or fixed address", pc);
            return m;
        }
        if (size > 128 * 1024 * 1024 || session.flexible_bytes + size > 256 * 1024 * 1024)
            return hostReturn(s, m, orbisError(12));
        const uint64_t address = allocate(session, size, true);
        session.flexible_bytes += size;
        store64(*m, s.gpr.rdi.qword, address);
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "_sceKernelRtldSetApplicationHeapAPI")) {
        if (!guestPointer(*m, s.gpr.rdi.qword, 8))
            return m;
        session.heap_api = s.gpr.rdi.qword;
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadMutexattrInit")) {
        const uint64_t address = allocate(session, 4);
        session.mutex_attributes[address] = 1;
        store64(*m, s.gpr.rdi.qword, address);
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadMutexattrSettype") ||
        !std::strcmp(name, "scePthreadMutexattrSetprotocol") ||
        !std::strcmp(name, "scePthreadMutexattrDestroy")) {
        const uint64_t address = load64(*m, s.gpr.rdi.qword);
        auto attr = session.mutex_attributes.find(address);
        if (attr == session.mutex_attributes.end())
            return hostReturn(s, m, orbisError(22));
        const int value = int(s.gpr.rsi.qword);
        if (!std::strcmp(name, "scePthreadMutexattrSettype")) {
            if (value < 1 || value > 4)
                return hostReturn(s, m, orbisError(22));
            attr->second = value;
        } else if (!std::strcmp(name, "scePthreadMutexattrSetprotocol")) {
            if (value < 0 || value > 2)
                return hostReturn(s, m, orbisError(22));
            // Like the Mac runtime: validate policy; no console priority scheduler.
        } else {
            session.mutex_attributes.erase(attr);
            auto &regions = m->regions;
            regions.erase(std::remove_if(regions.begin(), regions.end(),
                                         [address](const auto &r) { return r.base == address; }),
                          regions.end());
            store64(*m, s.gpr.rdi.qword, 0);
        }
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadMutexInit")) {
        int type = 1;
        if (s.gpr.rsi.qword) {
            auto attr = session.mutex_attributes.find(load64(*m, s.gpr.rsi.qword));
            if (attr == session.mutex_attributes.end())
                return hostReturn(s, m, orbisError(22));
            type = attr->second;
        }
        auto mutex = std::make_unique<HostMutex>();
        pthread_mutexattr_t attr;
        int result = pthread_mutexattr_init(&attr);
        if (result)
            return hostReturn(s, m, orbisError(result));
        result = pthread_mutexattr_settype(&attr, type == 2   ? PTHREAD_MUTEX_RECURSIVE
                                                  : type == 3 ? PTHREAD_MUTEX_NORMAL
                                                              : PTHREAD_MUTEX_ERRORCHECK);
        if (!result)
            result = pthread_mutex_init(&mutex->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        if (result)
            return hostReturn(s, m, orbisError(result));
        mutex->initialized = true;
        const uint64_t address = allocate(session, 256);
        session.mutexes[address] = std::move(mutex);
        store64(*m, s.gpr.rdi.qword, address);
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadMutexLock") || !std::strcmp(name, "scePthreadMutexUnlock")) {
        auto mutex = session.mutexes.find(load64(*m, s.gpr.rdi.qword));
        if (mutex == session.mutexes.end())
            return hostReturn(s, m, orbisError(22));
        const int result = !std::strcmp(name, "scePthreadMutexLock")
                               ? pthread_mutex_lock(&mutex->second->mutex)
                               : pthread_mutex_unlock(&mutex->second->mutex);
        return hostReturn(s, m, orbisError(result));
    }
    session.missing_service = name;
    stopGuest(*m, "missing host service", pc);
    return m;
}
static Memory *gameDispatch(State &s, uint64_t pc, Memory *m) {
    Session &session = *active;
    if (++session.calls > 100000) {
        stopGuest(*m, "guest dispatch budget exhausted", pc);
        return m;
    }
    if (pc >= hle_base && pc - hle_base < std::size(imports) * 0x1000 &&
        (pc - hle_base) % 0x1000 == 0) {
        // Finish C++ host scopes before returning to the outer guest checkpoint.
        auto recovery = m->recovery;
        m->recovery = nullptr;
        Memory *result = m;
        try {
            result = hostService(s, pc, m);
        } catch (const std::exception &) {
            m->fault = "host service allocation or mapping failed";
        }
        m->recovery = recovery;
        if (!m->fault.empty() && recovery)
            std::longjmp(*recovery, 1);
        return result;
    }
    const uint64_t lookup = pc >= session.image_address && pc - session.image_address < image_size
                                ? pc - session.image_address + image_base
                                : pc;
    for (const auto &entry : traces)
        if (entry.pc == lookup)
            return entry.function(s, pc, m);
    stopGuest(*m, "missing translated block", lookup);
    return m;
}

static std::string bootstrap(const char *path) {
    auto owner = std::make_unique<Session>();
    Session *session = owner.get();
    session->memory.dispatcher = gameDispatch;
    if (!readExact(path, image_size, session->image))
        return "{\"error\":\"incorrect image size or unavailable file\"}";
    if (!matchesHash(session->image, image_sha256_hex))
        return "{\"error\":\"incorrect image hash\"}";
    session->stack.resize(8 * 1024 * 1024);
    session->identity =
        std::getenv("OSL_GUEST_IDENTITY") && !std::strcmp(std::getenv("OSL_GUEST_IDENTITY"), "1");
    if (session->identity) {
        session->image_address = reinterpret_cast<uint64_t>(session->image.data());
        session->stack_address = reinterpret_cast<uint64_t>(session->stack.data());
        if (session->image_address + image_size >= (uint64_t(1) << 40) ||
            session->stack_address + session->stack.size() >= (uint64_t(1) << 40))
            return "{\"error\":\"host image/stack exceed guest GPU address range\"}";
        std::string fixup_path(path);
        fixup_path = fixup_path.substr(0, fixup_path.find_last_of('/') + 1) + "image-pointers.bin";
        std::vector<uint8_t> pointer_bytes;
        if (!readExact(fixup_path.c_str(), image_pointer_count * 8, pointer_bytes) ||
            !matchesHash(pointer_bytes, pointer_sha256_hex))
            return "{\"error\":\"incorrect image pointer table hash\"}";
        for (uint64_t i = 0; i < image_pointer_count; ++i) {
            uint64_t offset = 0, value = 0;
            std::memcpy(&offset, pointer_bytes.data() + i * 8, 8);
            if (offset > image_size - 8)
                return "{\"error\":\"invalid image pointer table\"}";
            std::memcpy(&value, session->image.data() + offset, 8);
            if (value < image_base || value - image_base >= image_size)
                return "{\"error\":\"unsupported image pointer relocation\"}";
            value = value - image_base + session->image_address;
            std::memcpy(session->image.data() + offset, &value, 8);
        }
    }
    for (const auto &segment : segments)
        mapRegion(session->memory, session->image_address + segment.address,
                  session->image.data() + segment.address, segment.size);
    mapRegion(session->memory, session->stack_address, session->stack.data(),
              session->stack.size());
    for (size_t i = 0; i < std::size(imports); ++i) {
        if (!imports[i].data)
            continue;
        auto bytes = std::make_unique<std::vector<uint8_t>>(512);
        const uint64_t address = hle_base + i * 0x1000 + 0x100;
        if (!std::strcmp(imports[i].symbol, "Need_sceLibcInternal")) {
            (*bytes)[0] = 1;
        } else if (!std::strcmp(imports[i].symbol, "__stack_chk_guard")) {
            arc4random_buf(bytes->data(), 8);
            (*bytes)[0] = 0;
        } else if (!std::strcmp(imports[i].symbol, "__progname")) {
            uint64_t string = address + 64;
            std::memcpy(bytes->data(), &string, 8);
            std::memcpy(bytes->data() + 64, "eboot.bin", 10);
        } else {
            continue;
        }
        mapRegion(session->memory, address, bytes->data(), bytes->size());
        session->owned.push_back(std::move(bytes));
    }
    std::jmp_buf recovery;
    active = session;
    session->memory.recovery = &recovery;
    if (setjmp(recovery) == 0) {
        for (const auto &module : modules) {
            session->stage =
                session->completed_modules == 0 ? "libc initializer" : "Fios2 initializer";
            session->state = State{};
            session->state.gpr.rsp.qword = session->stack_address + session->stack.size() - 0xf8;
            store64(session->memory, session->state.gpr.rsp.qword, 0xfeedface);
            dispatch(session->state, session->image_address + module.init, &session->memory);
            if (!session->memory.fault.empty())
                break;
            if (session->state.gpr.rip.qword != 0xfeedface ||
                uint32_t(session->state.gpr.rax.qword) != 0) {
                stopGuest(session->memory, "module initializer did not return zero");
                break;
            }
            ++session->completed_modules;
        }
        if (session->completed_modules == std::size(modules)) {
            session->stage = "eboot entry";
            session->state = State{};
            session->state.gpr.rsp.qword = session->stack_address + session->stack.size() - 0xf8;
            store64(session->memory, session->state.gpr.rsp.qword, 0xfeedface);
            store64(session->memory, session->stack_address + 0x100, 1);
            store64(session->memory, session->stack_address + 0x108,
                    session->stack_address + 0x140);
            std::memcpy(session->stack.data() + 0x140, "/app0/eboot.bin", 15);
            session->state.gpr.rdi.qword = session->stack_address + 0x100;
            session->state.gpr.rsi.qword = 0x70001000;
            dispatch(session->state, session->image_address + guest_entry, &session->memory);
        }
    }
    session->memory.recovery = nullptr;
    const uint64_t missing =
        session->memory.missing_pc >= session->image_address &&
                session->memory.missing_pc - session->image_address < image_size
            ? session->memory.missing_pc - session->image_address + image_base
            : session->memory.missing_pc;
    std::ostringstream out;
    out << "{\"stage\":\"" << session->stage
        << "\",\"completed_modules\":" << session->completed_modules
        << ",\"trace_count\":" << std::size(traces)
        << ",\"game_booted\":false,\"dispatches\":" << session->calls << ",\"error\":\""
        << session->memory.fault << "\",\"missing_pc\":" << missing
        << ",\"identity_mapping\":" << (session->identity ? "true" : "false")
        << ",\"image_address\":" << session->image_address
        << ",\"stack_address\":" << session->stack_address
        << ",\"fault_address\":" << session->memory.fault_address
        << ",\"flexible_bytes\":" << session->flexible_bytes << ",\"missing_service\":\""
        << session->missing_service
        << "\",\"heap_api_registered\":" << (session->heap_api != 0 ? "true" : "false") << "}";
    active = nullptr;
    return out.str();
}
extern "C" const char *osl_mobile_game_report(const char *path) {
    static thread_local std::string report;
    try {
        report = bootstrap(path);
    } catch (const std::exception &) {
        report = "{\"error\":\"host bootstrap failure\",\"game_booted\":false}";
    }
    return report.c_str();
}
#ifdef OSL_GAME_CLI
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    try {
        puts(bootstrap(argv[1]).c_str());
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 3;
    }
}
#endif
