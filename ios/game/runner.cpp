// Discovery runner: real guest code, explicit gaps, no synthetic boot success.
#include "../cpu/runtime.h"
#include "image-layout.h"
#include "traces.h"
#include "trophies/trophy_service.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <map>
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

struct FreeBytes { void operator()(uint8_t *p) const { std::free(p); } };
using AlignedBytes = std::unique_ptr<uint8_t, FreeBytes>;

struct Session {
    Memory memory;
    State state{};
    std::vector<uint8_t> image, stack;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> owned;
    std::vector<AlignedBytes> gpu_allocations;
    std::map<uint16_t, unsigned> compatibility_modules;
    unsigned gesture_setup_calls = 0;
    bool save_dialog_initialized = false, common_dialog_initialized = false, live_streaming_initialized = false, error_dialog_initialized = false;
    int32_t error_dialog_code = 0;
    osl::trophies::TrophyService trophies;
    const char *stage = "prepare", *missing_service = "";
    unsigned completed_modules = 0, calls = 0;
    uint64_t heap_api = 0, next_allocation = 0x30000000;
    uint64_t image_address = image_base, stack_address = 0x20000000;
    bool identity = false;
    std::unordered_map<uint64_t, int> mutex_attributes;
    std::unordered_map<uint64_t, std::unique_ptr<HostMutex>> mutexes;
    uint64_t flexible_bytes = 0, flexible_reserved_bytes = 0;
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
    if (auto p = guestPointer(m, address, 8, true))
        std::memcpy(p, &value, 8);
}
static uint64_t allocate(Session &s, uint64_t size, bool gpu_visible = false) {
    if (!size || size > 128 * 1024 * 1024) {
        stopGuest(s.memory, "unsupported allocation size");
        return 0;
    }
    std::unique_ptr<std::vector<uint8_t>> bytes;
    AlignedBytes aligned;
    uint8_t *data = nullptr;
    if (gpu_visible) {
        const uint64_t reserved_size = (size + 16383) & ~uint64_t(16383);
        void *pointer = nullptr;
        if (posix_memalign(&pointer, 16384, reserved_size))
            throw std::bad_alloc();
        aligned.reset(static_cast<uint8_t *>(pointer));
        data = aligned.get();
        std::memset(data, 0, reserved_size);
    } else {
        bytes = std::make_unique<std::vector<uint8_t>>(size);
        data = bytes->data();
    }
    uint64_t address = s.next_allocation;
    if (s.identity && gpu_visible)
        address = reinterpret_cast<uint64_t>(data);
    else
        s.next_allocation += (size + 16383) & ~uint64_t(16383);
    if (address >= (uint64_t(1) << 40) || size > (uint64_t(1) << 40) - address) {
        stopGuest(s.memory, "host allocation exceeds guest GPU address range");
        return 0;
    }
    mapRegion(s.memory, address, data, size);
    if (aligned)
        s.gpu_allocations.push_back(std::move(aligned));
    else
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
static bool gestureSetupFunction(const char *name) {
    return !std::strcmp(name, "sceSystemGestureOpen") ||
           !std::strcmp(name, "sceSystemGestureClose") ||
           !std::strcmp(name, "sceSystemGestureCreateTouchRecognizer") ||
           !std::strcmp(name, "sceSystemGestureInitializePrimitiveTouchRecognizer") ||
           !std::strcmp(name, "sceSystemGestureFinalizePrimitiveTouchRecognizer");
}
static bool saveDialogFunction(const char *name) {
    return !std::strcmp(name, "sceSaveDataDialogInitialize") ||
           !std::strcmp(name, "sceSaveDataDialogTerminate") ||
           !std::strcmp(name, "sceSaveDataDialogUpdateStatus") ||
           !std::strcmp(name, "sceSaveDataDialogGetResult") ||
           !std::strcmp(name, "sceSaveDataDialogOpen");
}
static bool trophyFunction(const char *name) {
    return !std::strcmp(name, "sceNpTrophyCreateContext") ||
           !std::strcmp(name, "sceNpTrophyCreateHandle") ||
           !std::strcmp(name, "sceNpTrophyRegisterContext") ||
           !std::strcmp(name, "sceNpTrophyDestroyHandle") ||
           !std::strcmp(name, "sceNpTrophyGetTrophyUnlockState") ||
           !std::strcmp(name, "sceNpTrophyUnlockTrophy");
}
static bool streamingLifecycle(const char *name) {
    return !std::strcmp(name, "sceGameLiveStreamingInitialize") ||
           !std::strcmp(name, "sceGameLiveStreamingTerminate");
}
static bool errorDialogFunction(const char *name) {
    return !std::strcmp(name, "sceErrorDialogInitialize") || !std::strcmp(name, "sceErrorDialogTerminate") ||
           !std::strcmp(name, "sceErrorDialogUpdateStatus") || !std::strcmp(name, "sceErrorDialogOpen");
}
static Memory *hostService(State &s, uint64_t pc, Memory *m) {
    Session &session = *active;
    const auto &import = imports[(pc - hle_base) / 0x1000];
    const char *name = import.symbol;
    if (!std::strcmp(name, "sceSysmoduleLoadModule") ||
        !std::strcmp(name, "sceSysmoduleUnloadModule")) {
        const uint16_t id = uint16_t(s.gpr.rdi.qword);
        if (!id)
            return hostReturn(s, m, 0x805a1000u);
        // Fixed-profile compatibility provider. The Mac reference treats these
        // SDK modules as resident. Ult/Fiber have no imported API in this
        // linked image. Other providers below expose only the explicit supported
        // lifecycle methods; requests for a presenter still stop. Never accept
        // an unknown export or introduce dynamic module/symbol resolution.
        const char *library = id == 6 ? "#libSceFiber" : id == 7 ? "#libSceUlt" :
                              id == 206 ? "#libSceSystemGesture" : id == 160 ? "#libSceSaveDataDialog" : id == 173 ? "#libSceNpTrophy" : id == 129 ? "#libSceGameLiveStreaming" : id == 164 ? "#libSceMsgDialog" :
                              id == 172 ? "#libSceErrorDialog" : nullptr;
        bool empty_provider = library != nullptr;
        for (const auto &item : imports) {
            if ((library && std::strstr(item.nid, library) &&
                 !(id == 206 && gestureSetupFunction(item.symbol)) &&
                 !(id == 160 && saveDialogFunction(item.symbol)) &&
                 !(id == 173 && trophyFunction(item.symbol)) &&
                 !(id == 129 && streamingLifecycle(item.symbol)) &&
                 !(id == 172 && errorDialogFunction(item.symbol))) ||
                std::strstr(item.symbol, "Dlsym") || std::strstr(item.symbol, "LoadStartModule"))
                empty_provider = false;
        }
        if (!empty_provider) {
            session.missing_service = name;
            stopGuest(*m, "module provider unavailable", pc);
            return m;
        }
        auto &references = session.compatibility_modules[id];
        if (!std::strcmp(name, "sceSysmoduleUnloadModule")) {
            if (!references)
                return hostReturn(s, m, 0x805a1001u);
            --references;
        } else {
            if (references >= 1024) {
                stopGuest(*m, "module reference budget exhausted", pc);
                return m;
            }
            ++references;
        }
        return hostReturn(s, m, 0);
    }
    if (gestureSetupFunction(name)) {
        if (!session.compatibility_modules[206]) {
            session.missing_service = name;
            stopGuest(*m, "SystemGesture provider is not loaded", pc);
            return m;
        }
        // Match the working Mac port's setup-only compatibility: this exact
        // profile imports no touch-gesture update/result consumer. This does
        // not provide touch controls or pretend to recognize gesture events.
        ++session.gesture_setup_calls;
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "sceCommonDialogInitialize")) {
        session.common_dialog_initialized = true;
        return hostReturn(s, m, 0);
    }
    if (saveDialogFunction(name)) {
        if (!session.compatibility_modules[160]) {
            session.missing_service = name;
            stopGuest(*m, "SaveDataDialog provider is not loaded", pc);
            return m;
        }
        if (!std::strcmp(name, "sceSaveDataDialogInitialize")) {
            if (session.save_dialog_initialized) return hostReturn(s, m, 0x80b80004u);
            session.save_dialog_initialized = true;
            return hostReturn(s, m, 0);
        }
        if (!std::strcmp(name, "sceSaveDataDialogTerminate")) {
            if (!session.save_dialog_initialized) return hostReturn(s, m, 0x80b80003u);
            session.save_dialog_initialized = false;
            return hostReturn(s, m, 0);
        }
        if (!std::strcmp(name, "sceSaveDataDialogUpdateStatus"))
            return hostReturn(s, m, session.save_dialog_initialized ? 1 : 0);
        if (!std::strcmp(name, "sceSaveDataDialogGetResult"))
            return hostReturn(s, m, 0x80b80005u); // No completed selection exists.
        if (!session.save_dialog_initialized) return hostReturn(s, m, 0x80b80003u);
        if (!s.gpr.rdi.qword) return hostReturn(s, m, 0x80b8000du);
        if (!guestPointer(*m, s.gpr.rdi.qword, 56)) return m;
        session.missing_service = name;
        stopGuest(*m, "save dialog presenter unavailable", pc);
        return m;
    }
    if (trophyFunction(name)) {
        if (!session.compatibility_modules[173]) {
            session.missing_service = name;
            stopGuest(*m, "Trophy provider is not loaded", pc);
            return m;
        }
        auto &service = session.trophies;
        int32_t value = 0, result = 0;
        if (!std::strcmp(name, "sceNpTrophyCreateContext") ||
            !std::strcmp(name, "sceNpTrophyCreateHandle")) {
            auto output = s.gpr.rdi.qword ? guestPointer(*m, s.gpr.rdi.qword, 4, true) : nullptr;
            if (!m->fault.empty()) return m;
            if (!std::strcmp(name, "sceNpTrophyCreateHandle"))
                result = service.CreateHandle(output ? &value : nullptr);
            else
                result = service.CreateContext(output ? &value : nullptr, int32_t(s.gpr.rsi.qword),
                                               uint32_t(s.gpr.rdx.qword), s.gpr.rcx.qword);
            if (!result) std::memcpy(output, &value, 4);
        } else if (!std::strcmp(name, "sceNpTrophyRegisterContext")) {
            result = service.RegisterContext(int32_t(s.gpr.rdi.qword), int32_t(s.gpr.rsi.qword), s.gpr.rdx.qword);
        } else if (!std::strcmp(name, "sceNpTrophyDestroyHandle")) {
            result = service.DestroyHandle(int32_t(s.gpr.rdi.qword));
        } else if (!std::strcmp(name, "sceNpTrophyUnlockTrophy")) {
            auto output = s.gpr.rcx.qword ? guestPointer(*m, s.gpr.rcx.qword, 4, true) : nullptr;
            if (!m->fault.empty()) return m;
            result = service.UnlockTrophy(int32_t(s.gpr.rdi.qword), int32_t(s.gpr.rsi.qword),
                                          int32_t(s.gpr.rdx.qword), output ? &value : nullptr);
            if (!result) std::memcpy(output, &value, 4);
        } else {
            osl::trophies::FlagArray flags{};
            uint32_t count = 0;
            auto output = s.gpr.rdx.qword ? guestPointer(*m, s.gpr.rdx.qword, sizeof(flags), true) : nullptr;
            auto count_output = s.gpr.rcx.qword ? guestPointer(*m, s.gpr.rcx.qword, 4, true) : nullptr;
            if (!m->fault.empty()) return m;
            result = service.GetUnlockState(int32_t(s.gpr.rdi.qword), int32_t(s.gpr.rsi.qword),
                                             output ? &flags : nullptr, count_output ? &count : nullptr);
            if (!result) { std::memcpy(output, &flags, sizeof(flags)); std::memcpy(count_output, &count, 4); }
        }
        return hostReturn(s, m, uint32_t(result));
    }
    if (streamingLifecycle(name)) {
        if (!session.compatibility_modules[129]) {
            session.missing_service = name;
            stopGuest(*m, "GameLiveStreaming provider is not loaded", pc);
            return m;
        }
        // This fixed profile imports only lifecycle entrypoints, not recording
        // or broadcast operations. No capture, network stream or permission is started.
        session.live_streaming_initialized = !std::strcmp(name, "sceGameLiveStreamingInitialize");
        return hostReturn(s, m, 0);
    }
    if (errorDialogFunction(name)) {
        if (!session.compatibility_modules[172]) {
            session.missing_service = name; stopGuest(*m, "ErrorDialog provider is not loaded", pc); return m;
        }
        if (!std::strcmp(name, "sceErrorDialogInitialize")) {
            if (session.error_dialog_initialized) return hostReturn(s, m, 0x80b80004u);
            session.error_dialog_initialized = true; return hostReturn(s, m, 0);
        }
        if (!std::strcmp(name, "sceErrorDialogTerminate")) {
            if (!session.error_dialog_initialized) return hostReturn(s, m, 0x80b80003u);
            session.error_dialog_initialized = false; return hostReturn(s, m, 0);
        }
        if (!std::strcmp(name, "sceErrorDialogUpdateStatus"))
            return hostReturn(s, m, session.error_dialog_initialized ? 1 : 0);
        if (!session.error_dialog_initialized) return hostReturn(s, m, 0x80b80003u);
        if (s.gpr.rdi.qword) {
            auto param = guestPointer(*m, s.gpr.rdi.qword, 8);
            if (!param) return m;
            std::memcpy(&session.error_dialog_code, param + 4, 4);
        }
        session.missing_service = name; stopGuest(*m, "error dialog presenter unavailable", pc); return m;
    }
    if (!std::strcmp(name, "sceKernelGetDirectMemorySize"))
        return hostReturn(s, m, uint64_t(5056) * 1024 * 1024);
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
        const uint64_t reserved_size = (size + 16383) & ~uint64_t(16383);
        if (size > 128 * 1024 * 1024 || session.flexible_reserved_bytes + reserved_size > 256 * 1024 * 1024)
            return hostReturn(s, m, orbisError(12));
        const uint64_t address = allocate(session, size, true);
        session.flexible_bytes += size;
        session.flexible_reserved_bytes += reserved_size;
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
                  session->image.data() + segment.address, segment.size, (segment.flags & 2) != 0 && (segment.flags & 1) == 0);
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
    const auto guest_started = std::chrono::steady_clock::now();
    session->memory.deadline = guest_started + std::chrono::seconds(2);
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
    const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - guest_started).count();
    std::ostringstream out;
    out << "{\"stage\":\"" << session->stage
        << "\",\"completed_modules\":" << session->completed_modules
        << ",\"trace_count\":" << std::size(traces)
        << ",\"checkpoints\":" << session->memory.checkpoints << ",\"guest_elapsed_ms\":" << elapsed_ms
        << ",\"game_booted\":false,\"dispatches\":" << session->calls << ",\"error\":\""
        << session->memory.fault << "\",\"missing_pc\":" << missing
        << ",\"identity_mapping\":" << (session->identity ? "true" : "false")
        << ",\"image_address\":" << session->image_address
        << ",\"stack_address\":" << session->stack_address
        << ",\"fault_address\":" << session->memory.fault_address
        << ",\"error_dialog_code\":" << uint32_t(session->error_dialog_code)
        << ",\"broadcasting\":false,\"live_streaming_initialized\":" << (session->live_streaming_initialized ? "true" : "false")
        << ",\"trophy_persistence\":false"
        << ",\"save_dialog_initialized\":" << (session->save_dialog_initialized ? "true" : "false")
        << ",\"gesture_setup_calls\":" << session->gesture_setup_calls
        << ",\"gpu_allocations_aligned\":" << (std::all_of(session->gpu_allocations.begin(), session->gpu_allocations.end(), [](const auto &p) { return reinterpret_cast<uint64_t>(p.get()) % 16384 == 0; }) ? "true" : "false")
        << ",\"flexible_reserved_bytes\":" << session->flexible_reserved_bytes
        << ",\"flexible_bytes\":" << session->flexible_bytes << ",\"missing_service\":\""
        << session->missing_service << "\",\"service_args\":[" << session->state.gpr.rdi.qword << ","
        << session->state.gpr.rsi.qword << "," << session->state.gpr.rdx.qword
        << "],\"heap_api_registered\":" << (session->heap_api != 0 ? "true" : "false") << "}";
    std::string result = out.str();
    result.pop_back();
    std::ostringstream modules_out;
    modules_out << ",\"compatibility_modules\":{";
    bool first_module = true;
    for (const auto &[id, references] : session->compatibility_modules) {
        if (!first_module) modules_out << ",";
        modules_out << "\"" << id << "\":" << references;
        first_module = false;
    }
    modules_out << "}}";
    active = nullptr;
    return result + modules_out.str();
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
static bool serviceChecks() {
    auto owner = std::make_unique<Session>();
    auto &session = *owner;
    session.stack.resize(4096);
    mapRegion(session.memory, 0x20000000, session.stack.data(), session.stack.size());
    active = &session;
    unsigned checks = 0;
    auto call = [&](const char *name, uint64_t argument = 0, uint64_t second = 0, uint64_t third = 0, uint64_t fourth = 0) {
        session.memory.fault = {};
        session.state = State{};
        session.state.gpr.rsp.qword = 0x20000ff8;
        session.state.gpr.rdi.qword = argument;
        session.state.gpr.rsi.qword = second;
        session.state.gpr.rdx.qword = third;
        session.state.gpr.rcx.qword = fourth;
        store64(session.memory, session.state.gpr.rsp.qword, 0xfeedface);
        for (size_t i = 0; i < std::size(imports); ++i)
            if (!std::strcmp(imports[i].symbol, name)) {
                hostService(session.state, hle_base + i * 0x1000, &session.memory);
                return session.state.gpr.rax.qword;
            }
        throw std::runtime_error("Service fixture name is not imported");
    };
    auto require = [&](bool value) {
        if (!value) throw std::runtime_error("Service contract check " + std::to_string(checks + 1) + " failed");
        ++checks;
    };
    require(call("sceSysmoduleLoadModule", 0) == 0x805a1000u);
    require(call("sceSysmoduleLoadModule", 7) == 0 && session.memory.fault.empty());
    require(call("sceSysmoduleUnloadModule", 7) == 0);
    require(call("sceSysmoduleUnloadModule", 7) == 0x805a1001u);
    require(call("sceSysmoduleLoadModule", 173) == 0 && session.memory.fault.empty());
    require(session.compatibility_modules.find(160) == session.compatibility_modules.end());
    require(call("sceSysmoduleLoadModule", 160) == 0 && session.memory.fault.empty());
    require(call("sceSaveDataDialogInitialize") == 0);
    require(call("sceSaveDataDialogInitialize") == 0x80b80004u);
    require(call("sceSaveDataDialogUpdateStatus") == 1);
    require(call("sceSaveDataDialogGetResult") == 0x80b80005u);
    require(call("sceSaveDataDialogOpen") == 0x80b8000du);
    call("sceSaveDataDialogOpen", 0x20000080);
    require(session.memory.fault == "save dialog presenter unavailable");
    require(call("sceSaveDataDialogTerminate") == 0);
    require(call("sceSaveDataDialogTerminate") == 0x80b80003u);
    require(call("sceSysmoduleLoadModule", 206) == 0 && session.memory.fault.empty());
    require(call("sceSystemGestureOpen") == 0);
    require(session.gesture_setup_calls == 1);
    call("sceSysmoduleLoadModule", 65535);
    require(session.memory.fault == "module provider unavailable");
    require(call("sceNpTrophyCreateContext") == 0x80551604u);
    require(call("sceNpTrophyCreateContext", 0x20000081, 1000) == 0);
    int32_t context = 0, handle = 0, platinum = 0;
    std::memcpy(&context, session.stack.data() + 0x81, 4);
    require(context == 1);
    require(call("sceNpTrophyCreateContext", 0x20000089, 1000) == 0x80551613u);
    require(call("sceNpTrophyCreateHandle", 0x20000085) == 0);
    std::memcpy(&handle, session.stack.data() + 0x85, 4);
    require(call("sceNpTrophyRegisterContext", context, handle) == 0);
    require(call("sceNpTrophyRegisterContext", context, handle) == 0x80551610u);
    require(call("sceNpTrophyGetTrophyUnlockState", context, handle, 0x20000100, 0x20000120) == 0);
    uint32_t count = 0, flags = 0;
    std::memcpy(&count, session.stack.data() + 0x120, 4);
    require(count == 0);
    require(call("sceNpTrophyUnlockTrophy", context, handle, 3, 0x20000124) == 0);
    std::memcpy(&platinum, session.stack.data() + 0x124, 4);
    require(platinum == -1);
    require(call("sceNpTrophyGetTrophyUnlockState", context, handle, 0x20000100, 0x20000120) == 0);
    std::memcpy(&count, session.stack.data() + 0x120, 4);
    std::memcpy(&flags, session.stack.data() + 0x100, 4);
    require(count == 0 && flags == 8); // Count is metadata total, unavailable in this RAM-only fixture.
    require(call("sceNpTrophyUnlockTrophy", context, handle, 3, 0x20000124) == 0x8055160cu);
    require(call("sceNpTrophyDestroyHandle", handle) == 0);
    require(call("sceNpTrophyGetTrophyUnlockState", context, handle, 0x20000100, 0x20000120) == 0x80551608u);
    session.memory.fault = {};
    uint64_t allocation = allocate(session, 4096, true);
    auto bytes = guestPointer(session.memory, allocation, 4096);
    require(bytes && reinterpret_cast<uint64_t>(bytes) % 16384 == 0);
    require(std::all_of(bytes, bytes + 4096, [](uint8_t byte) { return byte == 0; }));
    require(std::all_of(bytes + 4096, bytes + 16384, [](uint8_t byte) { return byte == 0; }));
    active = nullptr;
    std::printf("{\"service_checks\":%u,\"failed\":0,\"game_booted\":false}\n", checks);
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    try {
        if (!std::strcmp(argv[1], "--service-check")) return serviceChecks() ? 0 : 1;
        std::printf("OSL_STARTUP_RESULT:%s\n", bootstrap(argv[1]).c_str());
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 3;
    }
}
#endif
