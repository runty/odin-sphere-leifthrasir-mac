// Discovery runner: real guest code, explicit gaps, no synthetic boot success.
#include "../cpu/runtime.h"
#include "image-layout.h"
#include "traces.h"
#include "trophies/trophy_service.h"
#include "../services/memory.h"
#include "../services/user.h"
#include "../services/pad.h"
#include "../services/guest_storage.h"
#include "../services/offline.h"
#include "../services/eventflag.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
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
    DirectMemory direct_memory;
    Memory memory;
    MobilePad pad;
    MobileStorage storage;
    OfflineServices offline;
    MobileEventFlags event_flags;
#ifdef OSL_GAME_CLI
    struct TraceCloser { void operator()(FILE *stream) const { std::fclose(stream); } };
    std::unique_ptr<FILE,TraceCloser> dispatch_trace;
#endif
    Session() {
        memory.external_pointer = DirectMemory::pointer;
        osl_aot_user_reset();
#ifdef OSL_GAME_CLI
        if (const char *path=std::getenv("OSL_STARTUP_DISPATCH_TRACE")) {
            dispatch_trace.reset(std::fopen(path,"w"));
            if (!dispatch_trace) throw std::runtime_error("Cannot open logical dispatch trace");
        }
#endif
    }
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
    uint64_t native_memset_calls = 0, native_memset_bytes = 0;
    bool users_initialized = false;
    bool pad_initialized = false;
    bool saves_initialized = false;
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
extern "C" Memory *__osl_libc_memset(State &s, uint64_t pc, Memory *m) {
    const uint64_t size=s.gpr.rdx.qword, destination=s.gpr.rdi.qword;
    if (size>DirectMemory::capacity()) {
        stopGuest(*m,"libc memset exceeds direct pool capacity",pc); return m;
    }
    if (size) {
        auto bytes=guestPointer(*m,destination,size,true);
        if (!bytes) return m;
        std::memset(bytes,static_cast<uint8_t>(s.gpr.rsi.qword),size);
    }
    if (active) { ++active->native_memset_calls; active->native_memset_bytes+=size; }
    return hostReturn(s,m,destination);
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
    if (!std::strcmp(name,"sceNpSetNpTitleId"))
        return hostReturn(s,m,session.offline.configureTitle());
    if (!std::strcmp(name,"sceNpSetContentRestriction"))
        return hostReturn(s,m,session.offline.restriction());
    if (!std::strcmp(name,"sceNpGetState")) {
        if (!s.gpr.rsi.qword) return hostReturn(s,m,OfflineServices::invalid);
        auto output=guestPointer(*m,s.gpr.rsi.qword,4,true); if (!output) return m;
        const int32_t state=1; std::memcpy(output,&state,4); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNpGetNpId") || !std::strcmp(name,"sceNpTusGetData") || !std::strcmp(name,"sceNpTusSetData"))
        return hostReturn(s,m,OfflineServices::signed_out);
    if (!std::strcmp(name,"sceNpTusCreateNpTitleCtx") || !std::strcmp(name,"sceNpTusCreateRequest")) {
        const uint32_t id=!std::strcmp(name,"sceNpTusCreateNpTitleCtx") ? session.offline.context() : session.offline.request(s.gpr.rdi.dword);
        if (!id) { stopGuest(*m,"offline handle budget exhausted",pc); return m; }
        return hostReturn(s,m,id);
    }
    if (!std::strcmp(name,"sceNpTusDeleteNpTitleCtx")) {
        session.offline.removeContext(s.gpr.rdi.dword); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNpTusDeleteRequest")) {
        session.offline.requests.erase(s.gpr.rdi.dword); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNpRegisterStateCallback")) {
        const uint32_t id=session.offline.callback(false,s.gpr.rdi.qword,s.gpr.rsi.qword);
        if (!id) { stopGuest(*m,"offline callback budget exhausted",pc); return m; }
        return hostReturn(s,m,id);
    }
    if (!std::strcmp(name,"sceNpUnregisterStateCallback")) {
        session.offline.np_callbacks.erase(s.gpr.rdi.dword); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNpCheckCallback") || !std::strcmp(name,"sceNetCtlCheckCallback"))
        return hostReturn(s,m,0); // No state transitions occur while offline.
    if (!std::strcmp(name,"sceNetCtlGetInfo")) return hostReturn(s,m,0x80412108u);
    if (!std::strcmp(name,"sceNetCtlGetState")) {
        if (!s.gpr.rdi.qword) return hostReturn(s,m,0x80412107u);
        auto output=guestPointer(*m,s.gpr.rdi.qword,4,true); if (!output) return m;
        const int32_t state=0; std::memcpy(output,&state,4); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNetCtlRegisterCallback")) {
        if (!s.gpr.rdx.qword) return hostReturn(s,m,0x80412107u);
        auto output=guestPointer(*m,s.gpr.rdx.qword,4,true); if (!output) return m;
        const uint32_t id=session.offline.callback(true,s.gpr.rdi.qword,s.gpr.rsi.qword);
        if (!id) { stopGuest(*m,"offline callback budget exhausted",pc); return m; }
        std::memcpy(output,&id,4); return hostReturn(s,m,0);
    }
    if (!std::strcmp(name,"sceNetCtlUnregisterCallback")) {
        session.offline.net_callbacks.erase(s.gpr.rdi.dword); return hostReturn(s,m,0);
    }
    uint64_t event_result=0;
    if (GuestEvents::call(name,s,*m,session.event_flags,event_result))
        return m->fault.empty() ? hostReturn(s,m,event_result) : m;
    uint64_t storage_result=0;
    if (GuestStorage::save(name,s,*m,session.storage,session.saves_initialized,storage_result) ||
        GuestStorage::file(name,s,*m,session.storage,storage_result))
        return m->fault.empty() ? hostReturn(s,m,storage_result) : m;
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
    if (!std::strcmp(name, "sceUserServiceInitialize")) {
        const int32_t result=Users::initialize(nullptr); // Production service does not consume params.
        if (!result) session.users_initialized=true;
        return hostReturn(s,m,uint32_t(result));
    }
    if (!std::strcmp(name, "sceUserServiceTerminate")) {
        const int32_t result=Users::terminate();
        if (!result) session.users_initialized=false;
        return hostReturn(s,m,uint32_t(result));
    }
    if (!std::strcmp(name, "sceUserServiceGetInitialUser") || !std::strcmp(name, "sceUserServiceGetLoginUserIdList")) {
        const bool list=!std::strcmp(name,"sceUserServiceGetLoginUserIdList");
        if (!s.gpr.rdi.qword)
            return hostReturn(s,m,uint32_t(list ? Users::list(nullptr) : Users::initial(nullptr)));
        const size_t size=list ? 16 : 4;
        auto output=guestPointer(*m,s.gpr.rdi.qword,size,true);
        if (!output) return m;
        int32_t ids[4]={};
        const int32_t result=list ? Users::list(ids) : Users::initial(ids);
        if (!result) std::memcpy(output,ids,size);
        return hostReturn(s,m,uint32_t(result));
    }
    if (!std::strcmp(name, "sceUserServiceGetUserName")) {
        const int32_t id=static_cast<int32_t>(s.gpr.rdi.dword);
        if (!runtime_user_player(id) || !s.gpr.rsi.qword)
            return hostReturn(s,m,uint32_t(Users::name(id,nullptr,s.gpr.rdx.qword)));
        char dummy=0;
        if (!s.gpr.rdx.qword) return hostReturn(s,m,uint32_t(Users::name(id,&dummy,0)));
        auto output=guestPointer(*m,s.gpr.rsi.qword,s.gpr.rdx.qword,true);
        if (!output) return m;
        return hostReturn(s,m,uint32_t(Users::name(id,reinterpret_cast<char *>(output),s.gpr.rdx.qword)));
    }
    if (!std::strcmp(name,"scePadInit")) {
        const int32_t result=session.pad.initialize();
        if (!result) session.pad_initialized=true;
        return hostReturn(s,m,uint32_t(result));
    }
    if (!std::strcmp(name,"scePadOpen"))
        return hostReturn(s,m,uint32_t(session.pad.open(s.gpr.rdi.dword,s.gpr.rsi.dword,s.gpr.rdx.dword)));
    if (!std::strcmp(name,"scePadReadState") || !std::strcmp(name,"scePadGetControllerInformation")) {
        const int32_t handle=static_cast<int32_t>(s.gpr.rdi.dword);
        const int32_t invalid_handle=session.pad.check(handle);
        if (invalid_handle) return hostReturn(s,m,uint32_t(invalid_handle));
        const bool info=!std::strcmp(name,"scePadGetControllerInformation");
        if (!s.gpr.rsi.qword)
            return hostReturn(s,m,uint32_t(info ? session.pad.info(handle,nullptr) : session.pad.read(handle,nullptr)));
        auto output=guestPointer(*m,s.gpr.rsi.qword,info ? sizeof(ControllerInfo) : sizeof(PadData),true);
        if (!output) return m;
        int32_t result;
        if (info) {
            ControllerInfo value{}; result=session.pad.info(handle,&value);
            if (!result) std::memcpy(output,&value,sizeof(value));
        } else {
            PadData value{}; result=session.pad.read(handle,&value);
            if (!result) std::memcpy(output,&value,sizeof(value));
        }
        return hostReturn(s,m,uint32_t(result));
    }
    if (!std::strcmp(name,"scePadSetVibration")) {
        const int32_t handle=static_cast<int32_t>(s.gpr.rdi.dword);
        const int32_t invalid_handle=session.pad.check(handle);
        if (invalid_handle) return hostReturn(s,m,uint32_t(invalid_handle));
        if (!s.gpr.rsi.qword) return hostReturn(s,m,uint32_t(session.pad.vibration(handle,nullptr)));
        auto input=guestPointer(*m,s.gpr.rsi.qword,2);
        if (!input) return m;
        uint8_t motors[2]; std::memcpy(motors,input,2);
        return hostReturn(s,m,uint32_t(session.pad.vibration(handle,motors)));
    }
    if (!std::strcmp(name, "sceKernelGetDirectMemorySize"))
        return hostReturn(s, m, DirectMemory::capacity());
    if (!std::strcmp(name, "sceKernelAllocateDirectMemory")) {
        if (!s.gpr.r9.qword) return hostReturn(s, m, orbisError(22));
        auto output = guestPointer(*m, s.gpr.r9.qword, 8, true);
        if (!output) return m;
        int64_t physical = 0;
        const int32_t result = DirectMemory::allocate(s.gpr.rdi.qword, s.gpr.rsi.qword, s.gpr.rdx.qword,
                                                     s.gpr.rcx.qword, s.gpr.r8.dword, &physical);
        if (!result) std::memcpy(output, &physical, 8);
        return hostReturn(s, m, uint32_t(result));
    }
    if (!std::strcmp(name, "sceKernelMapDirectMemory") || !std::strcmp(name, "sceKernelMapNamedDirectMemory")) {
        if (!s.gpr.rdi.qword) return hostReturn(s, m, orbisError(22));
        auto output = guestPointer(*m, s.gpr.rdi.qword, 8, true);
        if (!output) return m;
        void *address = nullptr;
        std::memcpy(&address, output, 8);
        const int32_t result = DirectMemory::map(&address, s.gpr.rsi.qword, s.gpr.rdx.dword,
                                                 s.gpr.rcx.dword, s.gpr.r8.qword, s.gpr.r9.qword);
        if (!result) store64(*m, s.gpr.rdi.qword, reinterpret_cast<uint64_t>(address));
        return hostReturn(s, m, uint32_t(result));
    }
    if (!std::strcmp(name, "sceKernelReleaseDirectMemory"))
        return hostReturn(s, m, uint32_t(DirectMemory::release(s.gpr.rdi.qword, s.gpr.rsi.qword)));
    if (!std::strcmp(name, "sceKernelMunmap"))
        return hostReturn(s, m, uint32_t(DirectMemory::unmap(reinterpret_cast<void *>(s.gpr.rdi.qword), s.gpr.rsi.qword)));
    if (!std::strcmp(name, "sceKernelMprotect"))
        return hostReturn(s, m, uint32_t(DirectMemory::protect(reinterpret_cast<void *>(s.gpr.rdi.qword), s.gpr.rsi.qword, s.gpr.rdx.dword)));
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
    const uint64_t lookup = pc >= session.image_address && pc - session.image_address < image_size
                                ? pc - session.image_address + image_base : pc;
#ifdef OSL_GAME_CLI
    // Code targets only: never record guest registers, stack or credentials.
    if (session.dispatch_trace) std::fprintf(session.dispatch_trace.get(),"%llx\n",(unsigned long long)lookup);
#endif
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
    const auto private_root=std::filesystem::absolute(std::filesystem::path(path)).parent_path();
    session->storage.configure((private_root/"App0").string(),(private_root/"MobileProfile").string());
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
        << ",\"native_memset_calls\":" << session->native_memset_calls << ",\"native_memset_bytes\":" << session->native_memset_bytes
        << ",\"direct_allocated_bytes\":" << DirectMemory::allocated()
        << ",\"offline_users_initialized\":" << (session->users_initialized ? "true" : "false")
        << ",\"pad_backend\":\"GameController\",\"pad_initialized\":" << (session->pad_initialized ? "true" : "false")
        << ",\"local_storage_configured\":" << (session->storage.configured() ? "true" : "false")
        << ",\"save_backend_initialized\":" << (session->saves_initialized ? "true" : "false")
        << ",\"event_flags\":" << session->event_flags.count()
        << ",\"np_title_configured\":" << (session->offline.title_configured ? "true" : "false")
        << ",\"network_connected\":false,\"psn_signed_in\":false"
        << ",\"save_dialog_initialized\":" << (session->save_dialog_initialized ? "true" : "false")
        << ",\"gesture_setup_calls\":" << session->gesture_setup_calls
        << ",\"gpu_allocations_aligned\":" << (std::all_of(session->gpu_allocations.begin(), session->gpu_allocations.end(), [](const auto &p) { return reinterpret_cast<uint64_t>(p.get()) % 16384 == 0; }) ? "true" : "false")
        << ",\"flexible_reserved_bytes\":" << session->flexible_reserved_bytes
        << ",\"flexible_bytes\":" << session->flexible_bytes << ",\"missing_service\":\""
        << session->missing_service << "\",\"service_args\":[" << session->state.gpr.rdi.qword << ","
        << session->state.gpr.rsi.qword << "," << session->state.gpr.rdx.qword << "," << session->state.gpr.rcx.qword
        << "," << session->state.gpr.r8.qword << "," << session->state.gpr.r9.qword
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
    auto call = [&](const char *name, uint64_t argument = 0, uint64_t second = 0, uint64_t third = 0, uint64_t fourth = 0, uint64_t fifth = 0, uint64_t sixth = 0) {
        session.memory.fault = {};
        session.state = State{};
        session.state.gpr.rsp.qword = 0x20000ff8;
        session.state.gpr.rdi.qword = argument;
        session.state.gpr.rsi.qword = second;
        session.state.gpr.rdx.qword = third;
        session.state.gpr.rcx.qword = fourth;
        session.state.gpr.r8.qword = fifth;
        session.state.gpr.r9.qword = sixth;
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
    // Immutable offline state: no guest credential pointers are dereferenced.
    require(call("sceNpSetNpTitleId",UINT64_MAX,UINT64_MAX)==0 && session.offline.title_configured);
    require(call("sceNpSetContentRestriction",UINT64_MAX)==0 && session.offline.restriction_updates==1);
    require(call("sceNpGetState",1,0)==OfflineServices::invalid);
    std::fill(session.stack.begin()+0x100,session.stack.begin()+0x130,0x7b);
    require(call("sceNpGetState",1,0x20000101)==0);
    int32_t offline_state=0; std::memcpy(&offline_state,session.stack.data()+0x101,4);
    require(offline_state==1 && session.stack[0x100]==0x7b && session.stack[0x105]==0x7b);
    require(call("sceNpGetState",1,0x20000fff)==0 && !session.memory.fault.empty());
    require(call("sceNpGetNpId",1,UINT64_MAX)==OfflineServices::signed_out && session.memory.fault.empty());
    require(call("sceNpTusGetData",UINT64_MAX,UINT64_MAX)==OfflineServices::signed_out);
    require(call("sceNpTusSetData",UINT64_MAX,UINT64_MAX)==OfflineServices::signed_out);
    const auto np_context=call("sceNpTusCreateNpTitleCtx",UINT64_MAX,UINT64_MAX);
    require(np_context>0 && np_context<=4096 && session.offline.contexts.count(np_context)==1);
    require(call("sceNpTusCreateRequest",np_context+1)==OfflineServices::invalid);
    const auto request=call("sceNpTusCreateRequest",np_context);
    require(request>0 && session.offline.requests.at(request)==np_context);
    require(call("sceNpTusDeleteRequest",request)==0 && session.offline.requests.empty());
    const auto child=call("sceNpTusCreateRequest",np_context);
    require(child>0 && session.offline.requests.count(child)==1);
    require(call("sceNpTusDeleteNpTitleCtx",np_context)==0 && session.offline.contexts.empty() && session.offline.requests.empty());
    const auto np_callback=call("sceNpRegisterStateCallback",UINT64_MAX,0x1234);
    require(np_callback>0 && session.offline.np_callbacks.at(np_callback).function==UINT64_MAX);
    require(call("sceNpCheckCallback")==0 && session.memory.fault.empty());
    require(call("sceNpUnregisterStateCallback",np_callback)==0 && session.offline.np_callbacks.empty());
    require(call("sceNetCtlGetState",0)==0x80412107u);
    require(call("sceNetCtlGetState",0x20000109)==0);
    std::memcpy(&offline_state,session.stack.data()+0x109,4);
    require(offline_state==0 && session.stack[0x108]==0x7b && session.stack[0x10d]==0x7b);
    require(call("sceNetCtlGetInfo",1,UINT64_MAX)==0x80412108u && session.memory.fault.empty());
    require(call("sceNetCtlRegisterCallback",UINT64_MAX,0x9876,0)==0x80412107u);
    require(call("sceNetCtlRegisterCallback",UINT64_MAX,0x9876,0x20000111)==0);
    uint32_t net_callback=0; std::memcpy(&net_callback,session.stack.data()+0x111,4);
    require(net_callback>0 && session.offline.net_callbacks.at(net_callback).argument==0x9876);
    require(session.stack[0x110]==0x7b && session.stack[0x115]==0x7b);
    require(call("sceNetCtlCheckCallback")==0 && session.memory.fault.empty());
    require(call("sceNetCtlUnregisterCallback",net_callback)==0 && session.offline.net_callbacks.empty());
    require(call("sceNetCtlRegisterCallback",1,0,0x20000fff)==0 && !session.memory.fault.empty() && session.offline.net_callbacks.empty());
    OfflineServices exhausted;
    for(unsigned i=1;i<=4096;++i) require(exhausted.context()==i);
    require(exhausted.context()==0 && exhausted.contexts.size()==4096);
    setenv("OSL_USER_NAME","Odin Test",1);
    require(call("sceKernelCreateEventFlag",0)==MobileEventFlags::invalid);
    require(call("sceKernelCreateEventFlag",0x20000081,0,0x10,0)==0);
    const uint64_t kernel_flag=load64(session.memory,0x20000081);
    require(kernel_flag && session.event_flags.count()==1);
    require(call("sceKernelSetEventFlag",kernel_flag,3)==0);
    require(call("sceKernelPollEventFlag",kernel_flag,2,0x21,0x20000091)==0);
    require(load64(session.memory,0x20000091)==3);
    require(session.state.gpr.rip.qword==0xfeedface && session.state.gpr.rsp.qword==0x20001000);
    require(call("sceKernelClearEventFlag",kernel_flag,0)==0);
    require(call("sceKernelPollEventFlag",kernel_flag,1,1,0)==0x80020010u);
    require(call("sceKernelDeleteEventFlag",kernel_flag)==0 && session.event_flags.count()==0);
    require(call("sceKernelSetEventFlag",kernel_flag,1)==MobileEventFlags::missing);
    require(call("sceUserServiceInitialize")==0 && session.users_initialized);
    require(call("sceUserServiceInitialize")==0);
    require(call("sceUserServiceGetInitialUser")==0x80960005u);
    require(call("sceUserServiceGetInitialUser",0x20000151)==0);
    int32_t user=0;
    std::memcpy(&user,session.stack.data()+0x151,4);
    require(user==1);
    require(call("sceUserServiceGetLoginUserIdList")==0x80960005u);
    require(call("sceUserServiceGetLoginUserIdList",0x20000155)==0);
    int32_t users[4]={};
    std::memcpy(users,session.stack.data()+0x155,16);
    require(users[0]==1 && users[1]==-1 && users[2]==-1 && users[3]==-1);
    require(call("sceUserServiceGetUserName",1,0,16)==0x80960005u);
    require(call("sceUserServiceGetUserName",2,0x20000170,16)==0x80960005u);
    std::fill(session.stack.begin()+0x170,session.stack.begin()+0x180,0x7b);
    require(call("sceUserServiceGetUserName",1,0x20000171,0)==0x8096000au);
    require(call("sceUserServiceGetUserName",1,0x20000171,8)==0x8096000au && session.stack[0x171]==0x7b);
    require(call("sceUserServiceGetUserName",1,0x20000171,10)==0);
    require(!std::strcmp(reinterpret_cast<char *>(session.stack.data()+0x171),"Odin Test"));
    require(session.stack[0x170]==0x7b && session.stack[0x17b]==0x7b);
    require(call("sceUserServiceTerminate")==0 && !session.users_initialized);
    require(call("scePadOpen",1,0,0)==0x80920005u);
    require(call("scePadInit")==0 && session.pad_initialized);
    require(call("scePadInit")==0);
    require(call("scePadOpen",1,3,0)==0x80920001u);
    require(call("scePadOpen",1,0,1)==0x80920001u);
    require(call("scePadOpen",1,0,0)==1);
    require(call("scePadOpen",1,0,0)==0x80920004u);
    require(call("scePadReadState",99,UINT64_MAX)==0x80920003u && session.memory.fault.empty());
    require(call("scePadReadState",1,0)==0x80920001u);
    std::fill(session.stack.begin()+0x300,session.stack.begin()+0x3c0,0x7b);
    require(call("scePadGetControllerInformation",1,0x20000301)==0);
    ControllerInfo pad_info{}; std::memcpy(&pad_info,session.stack.data()+0x301,sizeof(pad_info));
    require(pad_info.resolution_x==1920 && pad_info.resolution_y==943);
    require(session.stack[0x300]==0x7b && session.stack[0x31d]==0x7b);
    require(call("scePadReadState",1,0x20000331)==0);
    PadData pad_data{}; std::memcpy(&pad_data,session.stack.data()+0x331,sizeof(pad_data));
    require(pad_data.timestamp>0 && pad_data.orientation[3]==1);
    require(session.stack[0x330]==0x7b && session.stack[0x3a9]==0x7b);
    require(call("scePadSetVibration",1,0)==0x80920001u);
    session.stack[0x3b1]=session.stack[0x3b2]=0;
    require(call("scePadSetVibration",1,0x200003b1)==0);
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
    require(call("sceKernelGetDirectMemorySize") == uint64_t(5056)*1024*1024);
    require(call("sceKernelAllocateDirectMemory", 0, DirectMemory::capacity(), 16384, 16384, 0, 0) == 0x80020016u);
    require(call("sceKernelAllocateDirectMemory", 0, DirectMemory::capacity(), 16384, 16384, 0, 0x20000181) == 0);
    const uint64_t physical = load64(session.memory, 0x20000181);
    require(physical == 0);
    store64(session.memory, 0x20000189, 0);
    require(call("sceKernelMapDirectMemory", 0x20000189, 16384, 3, 0, physical, 16384) == 0);
    const uint64_t mapped = load64(session.memory, 0x20000189);
    store64(session.memory, mapped, 0x5a3cc3a5);
    require(load64(session.memory, mapped) == 0x5a3cc3a5);
    require(call("sceKernelReleaseDirectMemory", physical, 16384) == 0);
    require(DirectMemory::pointer(mapped,8,false) == nullptr);
    for (uint64_t size : {0,1,2,3,15,16,17,255,256,257}) {
        session.memory.fault={};
        std::fill(session.stack.begin()+0x200,session.stack.begin()+0x400,0x7b);
        session.state=State{};
        session.state.gpr.rsp.qword=0x20000ff8;
        store64(session.memory,session.state.gpr.rsp.qword,0xfeedface);
        session.state.gpr.rdi.qword=0x20000201;
        session.state.gpr.rsi.qword=0x1234cc;
        session.state.gpr.rdx.qword=size;
        session.state.gpr.rbx.qword=0xabcdef;
        session.state.gpr.r12.qword=0x123456;
        __osl_libc_memset(session.state,0x10cd0ce0,&session.memory);
        require(session.memory.fault.empty() && session.state.gpr.rax.qword==0x20000201);
        require(session.state.gpr.rsp.qword==0x20001000 && session.state.gpr.rip.qword==0xfeedface);
        require(session.state.gpr.rbx.qword==0xabcdef && session.state.gpr.r12.qword==0x123456);
        bool correct=true;
        for (uint64_t i=0;i<512;++i)
            correct &= session.stack[0x200+i]==(i>=1 && i<1+size ? 0xcc : 0x7b);
        require(correct);
    }
    session.state.gpr.rdi.qword=0;
    session.state.gpr.rdx.qword=0;
    session.state.gpr.rsp.qword=0x20000ff8;
    __osl_libc_memset(session.state,0x10cd0ce0,&session.memory);
    require(session.memory.fault.empty() && session.state.gpr.rax.qword==0);
    session.state.gpr.rdi.qword=0x20000fff;
    session.state.gpr.rdx.qword=2;
    __osl_libc_memset(session.state,0x10cd0ce0,&session.memory);
    require(!session.memory.fault.empty() && session.stack.back()==0);
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
