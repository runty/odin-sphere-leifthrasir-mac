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
#include "../services/gpu.h"
#include "../services/system.h"
#include "../services/condition.h"
#include "../services/thread_storage.h"
#include "../services/workers.h"
#include "../services/guest_clock.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <exception>
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
using ThreadAttr=GuestThreadAttributes;

struct FreeBytes { void operator()(uint8_t *p) const { std::free(p); } };
using AlignedBytes = std::unique_ptr<uint8_t, FreeBytes>;

struct Session;
static void releaseThreadMutexes(Session &,uint64_t);
struct Session {
    std::recursive_mutex services;
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
    const MobileGpuServices *gpu=nullptr;
    unsigned gpu_service_calls=0;
    explicit Session(const MobileGpuServices *renderer=nullptr) : gpu(renderer) {
        memory.external_pointer = DirectMemory::pointer;
        memory.stop_requested = &stopping;
        osl_aot_user_reset();
#ifdef OSL_GAME_CLI
        if (const char *path=std::getenv("OSL_STARTUP_DISPATCH_TRACE")) {
            dispatch_trace.reset(std::fopen(path,"w"));
            if (!dispatch_trace) throw std::runtime_error("Cannot open logical dispatch trace");
        }
#endif
    }
    ~Session() {
        stopping=true;
        releaseThreadMutexes(*this,0x40000000);
        if(workers)workers->stopAndJoin();
        if (gpu) gpu->finish(gpu->context);
        direct_memory.reset();
    }
    State state{};
    std::vector<uint8_t> image, stack;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> owned;
    std::vector<AlignedBytes> gpu_allocations;
    ModuleTlsTemplates tls_templates;
    std::unique_ptr<ThreadStorage> primary_storage;
    std::atomic<bool> stopping{false};
    std::unique_ptr<MobileWorkers> workers;
    std::map<uint16_t, unsigned> compatibility_modules;
    unsigned gesture_setup_calls = 0;
    bool save_dialog_initialized = false, common_dialog_initialized = false, live_streaming_initialized = false, error_dialog_initialized = false;
    int32_t error_dialog_code = 0;
    osl::trophies::TrophyService trophies;
    const char *stage = "prepare", *missing_service = "";
    unsigned completed_modules = 0;
    std::atomic<unsigned> calls{0};
    uint64_t heap_api = 0, next_allocation = 0x30000000;
    uint64_t image_address = image_base, stack_address = 0x20000000;
    bool identity = false;
    std::unordered_map<uint64_t, int> mutex_attributes;
    std::unordered_map<uint64_t, std::shared_ptr<HostMutex>> mutexes;
    std::unordered_map<uint64_t,std::unordered_map<uint64_t,std::pair<std::shared_ptr<HostMutex>,unsigned>>> held_mutexes;
    MobileConditions conditions;
    uint64_t flexible_bytes = 0, flexible_reserved_bytes = 0;
    std::atomic<uint64_t> native_memset_calls{0},native_memset_bytes{0};
    bool users_initialized = false;
    bool pad_initialized = false;
    bool saves_initialized = false;
    ThreadAttr main_attributes;
    std::unordered_map<uint64_t, ThreadAttr> thread_attributes;
};
static thread_local Session *active;
static thread_local MobileWorkers::Worker *active_worker;
static Memory *gameDispatch(State &,uint64_t,Memory *);
static void runWorker(Session &,MobileWorkers::Worker &);
static uint64_t currentThread(){return active_worker ? active_worker->id : 0x40000000;}
static const char *&missingService(Session &s){return active_worker ? active_worker->missing_service : s.missing_service;}
struct ServiceScope {
    std::unique_lock<std::recursive_mutex> state;
    GuestMemoryScope memory;
    std::shared_ptr<HostMutex> retained_mutex;
    explicit ServiceScope(Session &session):state(session.services){}
    void release(){memory.unlock();state.unlock();}
    void acquire(){state.lock();memory.lock();}
    GuestWaitScope wait(){return {this,[](void *p){static_cast<ServiceScope *>(p)->release();},[](void *p){static_cast<ServiceScope *>(p)->acquire();}};}
};
static void releaseThreadMutexes(Session &session,uint64_t id) {
    ServiceScope scope(session);auto found=session.held_mutexes.find(id);if(found==session.held_mutexes.end())return;
    for(auto &entry:found->second)while(entry.second.second){if(pthread_mutex_unlock(&entry.second.first->mutex))break;--entry.second.second;}
    session.held_mutexes.erase(found);
}



struct WorkerAttachment {
    Session &session;MobileWorkers::Worker &worker;int exceptions=std::uncaught_exceptions();
    WorkerAttachment(Session &owner,MobileWorkers::Worker &cpu):session(owner),worker(cpu){active=&session;active_worker=&worker;}
    ~WorkerAttachment() {
        if(std::uncaught_exceptions()>exceptions)session.stopping=true;
        worker.memory.recovery=nullptr;worker.memory.cancel_requested=nullptr;
        if(worker.memory.atomic_held)__remill_atomic_end(&worker.memory);
        releaseThreadMutexes(session,worker.id);active_worker=nullptr;active=nullptr;
    }
};

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
        stopGuest(active_worker ? active_worker->memory : s.memory, "unsupported allocation size");
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
        stopGuest(active_worker ? active_worker->memory : s.memory, "host allocation exceeds guest GPU address range");
        return 0;
    }
    if (aligned)
        s.gpu_allocations.push_back(std::move(aligned));
    else
        s.owned.push_back(std::move(bytes));
    mapRegion(s.memory,address,data,size);
    return address;
}
static int initializeMutex(Session &session,Memory &memory,uint64_t slot,int type) {
    auto output=guestPointer(memory,slot,8,true);if(!output)return EINVAL;
    if(session.mutexes.size()>=4096)return ENOMEM;
    auto mutex=std::make_shared<HostMutex>();pthread_mutexattr_t attributes;
    int error=pthread_mutexattr_init(&attributes);if(error)return error;
    error=pthread_mutexattr_settype(&attributes,type==2 ? PTHREAD_MUTEX_RECURSIVE : type==3 ? PTHREAD_MUTEX_NORMAL : PTHREAD_MUTEX_ERRORCHECK);
    if(!error)error=pthread_mutex_init(&mutex->mutex,&attributes);
    pthread_mutexattr_destroy(&attributes);if(error)return error;
    mutex->initialized=true;
    const uint64_t address=allocate(session,256);if(!memory.fault.empty())return ENOMEM;
    session.mutexes.emplace(address,std::move(mutex));std::memcpy(output,&address,8);return 0;
}
static uint64_t orbisError(int e) { return e ? uint32_t(0x80020000u | unsigned(e)) : 0; }
static Memory *hostReturn(State &s, Memory *m, uint64_t value) {
    s.gpr.rax.qword = value;
    s.gpr.rip.qword = load64(*m, s.gpr.rsp.qword);
    s.gpr.rsp.qword += 8;
    return m;
}
extern "C" Memory *__osl_libc_memset(State &s, uint64_t pc, Memory *m) {
    const uint64_t size=s.gpr.rdx.qword,destination=s.gpr.rdi.qword;
    auto recovery=m->recovery;m->recovery=nullptr;
    {
        GuestMemoryScope scope;
        if(size>DirectMemory::capacity())stopGuest(*m,"libc memset exceeds direct pool capacity",pc);
        else if(size){if(auto bytes=guestPointer(*m,destination,size,true))std::memset(bytes,static_cast<uint8_t>(s.gpr.rsi.qword),size);}
        if(m->fault.empty() && active){++active->native_memset_calls;active->native_memset_bytes+=size;}
    }
    m->recovery=recovery;
    if(!m->fault.empty()){resumeGuestFault(*m);return m;}
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
    ServiceScope scope(session);
    const auto &import = imports[(pc - hle_base) / 0x1000];
    const char *name = import.symbol;
    uint64_t clock_result=0;
    if(GuestClock::call(name,s,*m,clock_result))return m->fault.empty() ? hostReturn(s,m,clock_result) : m;
    uint64_t system_result=0;
    if (GuestSystem::call(name,s,*m,system_result))
        return m->fault.empty() ? hostReturn(s,m,system_result) : m;
    uint64_t tls_result=0;
    if (auto *tls=active_worker ? active_worker->storage.get() : session.primary_storage.get(); tls && GuestTls::call(name,s,*m,*tls,tls_result))
        return m->fault.empty() ? hostReturn(s,m,tls_result) : m;
    const bool condition_wait=!std::strcmp(name,"scePthreadCondWait") || !std::strcmp(name,"scePthreadCondTimedwait");
    if(condition_wait && active_worker)m->cancel_requested=&active_worker->cancel_pending;
    uint64_t condition_result=0;
    if (GuestConditions::call(name,s,*m,session.conditions,[&](uint64_t id) -> pthread_mutex_t * {
            const auto found=session.mutexes.find(id);
            if(found==session.mutexes.end())return nullptr;
            scope.retained_mutex=found->second;return &found->second->mutex;
        },condition_result,scope.wait())) {
        m->cancel_requested=nullptr;
        return m->fault.empty() ? hostReturn(s,m,condition_result) : m;
    }
    if (session.gpu) {
        const uint64_t args[]={s.gpr.rdi.qword,s.gpr.rsi.qword,s.gpr.rdx.qword,
                               s.gpr.rcx.qword,s.gpr.r8.qword,s.gpr.r9.qword};
        uint64_t result=0;
        if (session.gpu->call(session.gpu->context,name,import.nid,args,s.gpr.rsp.qword,m,&result)) {
            ++session.gpu_service_calls;
            return m->fault.empty() ? hostReturn(s,m,result) : m;
        }
    }
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
    if (GuestEvents::call(name,s,*m,session.event_flags,event_result,scope.wait()))
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
            missingService(session) = name;
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
            missingService(session) = name;
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
            missingService(session) = name;
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
        missingService(session) = name;
        stopGuest(*m, "save dialog presenter unavailable", pc);
        return m;
    }
    if (trophyFunction(name)) {
        if (!session.compatibility_modules[173]) {
            missingService(session) = name;
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
            missingService(session) = name;
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
            missingService(session) = name; stopGuest(*m, "ErrorDialog provider is not loaded", pc); return m;
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
        missingService(session) = name; stopGuest(*m, "error dialog presenter unavailable", pc); return m;
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
        return hostReturn(s, m, currentThread());
    if(!std::strcmp(name,"scePthreadSetprio") || !std::strcmp(name,"scePthreadGetprio") ||
       !std::strcmp(name,"scePthreadSetaffinity") || !std::strcmp(name,"scePthreadGetaffinity")) {
        auto worker=session.workers ? session.workers->find(s.gpr.rdi.qword) : nullptr;
        ThreadAttr *attributes=s.gpr.rdi.qword==0x40000000 ? &session.main_attributes : worker ? &worker->attributes : nullptr;
        if(!attributes)return hostReturn(s,m,orbisError(3));
        if(!std::strcmp(name,"scePthreadSetprio"))attributes->priority=s.gpr.rsi.dword;
        else if(!std::strcmp(name,"scePthreadSetaffinity"))attributes->affinity=s.gpr.rsi.qword;
        else {
            if(!s.gpr.rsi.qword)return hostReturn(s,m,orbisError(22));
            const bool priority=!std::strcmp(name,"scePthreadGetprio");
            auto output=guestPointer(*m,s.gpr.rsi.qword,priority ? 4 : 8,true);if(!output)return m;
            if(priority)std::memcpy(output,&attributes->priority,4);else std::memcpy(output,&attributes->affinity,8);
        }
        return hostReturn(s,m,0);
    }
    if(!std::strcmp(name,"scePthreadCreate")) {
        if(!s.gpr.rdi.qword || !session.workers)return hostReturn(s,m,orbisError(22));
        auto output=guestPointer(*m,s.gpr.rdi.qword,8,true);if(!output)return m;
        ThreadAttr attributes;
        if(s.gpr.rsi.qword){const uint64_t id=load64(*m,s.gpr.rsi.qword);if(!m->fault.empty())return m;auto found=session.thread_attributes.find(id);if(found==session.thread_attributes.end())return hostReturn(s,m,orbisError(22));attributes=found->second;}
        const uint64_t entry=s.gpr.rdx.qword;
        const uint64_t normalized=entry>=session.image_address && entry-session.image_address<image_size ? entry-session.image_address : UINT64_MAX;
        bool executable=false;for(const auto &segment:segments)if((segment.flags&1) && normalized>=segment.address && normalized-segment.address<segment.size)executable=true;
        if(!executable)return hostReturn(s,m,orbisError(22));
        if(s.gpr.r8.qword)for(unsigned i=0;i<31;++i){auto byte=guestPointer(*m,s.gpr.r8.qword+i,1);if(!byte)return m;if(!*byte)break;}
        uint64_t id=0;const int error=session.workers->spawn(entry,s.gpr.rcx.qword,attributes,[&session](auto &worker){runWorker(session,worker);},id);
        if(!error)std::memcpy(output,&id,8);
        uint64_t result=0;GuestConditions::error(*m,error,result);return m->fault.empty() ? hostReturn(s,m,result) : m;
    }
    if(!std::strcmp(name,"scePthreadExit")) {
        if(!active_worker){stopGuest(*m,"main guest thread exit");return m;}
        active_worker->exit_requested=true;active_worker->result=s.gpr.rdi.qword;stopGuest(*m,"guest thread exited");return m;
    }
    if(!std::strcmp(name,"scePthreadEqual"))return hostReturn(s,m,s.gpr.rdi.qword==s.gpr.rsi.qword);
    if(!std::strcmp(name,"scePthreadYield")){scope.release();std::this_thread::yield();scope.acquire();return hostReturn(s,m,0);}
    if(!std::strcmp(name,"scePthreadCancel") || !std::strcmp(name,"scePthreadDetach")) {
        const int error=!session.workers ? ESRCH : !std::strcmp(name,"scePthreadCancel") ? session.workers->cancel(s.gpr.rdi.qword) : session.workers->detach(s.gpr.rdi.qword);
        uint64_t result=0;GuestConditions::error(*m,error,result);return m->fault.empty() ? hostReturn(s,m,result) : m;
    }
    if(!std::strcmp(name,"scePthreadJoin")) {
        if(!session.workers)return hostReturn(s,m,orbisError(3));
        if(s.gpr.rsi.qword && !guestPointer(*m,s.gpr.rsi.qword,8,true))return m;
        if(active_worker)m->cancel_requested=&active_worker->cancel_pending;
        scope.release();const auto joined=session.workers->join(s.gpr.rdi.qword,currentThread(),*m);scope.acquire();m->cancel_requested=nullptr;
        if(joined.stop){stopGuest(*m,joined.stop);return m;}
        uint64_t result=0;GuestConditions::error(*m,joined.error,result);
        if(!joined.error && s.gpr.rsi.qword)store64(*m,s.gpr.rsi.qword,joined.result);
        return m->fault.empty() ? hostReturn(s,m,result) : m;
    }
    if(!std::strcmp(name,"sceKernelUsleep")) {
        using Clock=std::chrono::steady_clock;const auto now=Clock::now();
        const uint64_t maximum=std::chrono::duration_cast<std::chrono::microseconds>(Clock::time_point::max()-now).count();
        const auto end=s.gpr.rdi.qword>maximum ? Clock::time_point::max() : now+std::chrono::microseconds(s.gpr.rdi.qword);
        if(active_worker)m->cancel_requested=&active_worker->cancel_pending;
        const char *stopped=nullptr;scope.release();
        while(Clock::now()<end){if((stopped=guestInterruption(*m)))break;const auto time=Clock::now();if(time>=m->deadline){stopped="guest sleep exceeded host execution deadline";break;}std::this_thread::sleep_until(std::min(std::min(end,m->deadline),time+std::chrono::milliseconds(1)));}
        scope.acquire();m->cancel_requested=nullptr;if(stopped){stopGuest(*m,stopped);return m;}return hostReturn(s,m,0);
    }
    if (!std::strcmp(name, "scePthreadAttrInit")) {
        if (!s.gpr.rdi.qword)
            return hostReturn(s, m, orbisError(22));
        auto output=guestPointer(*m,s.gpr.rdi.qword,8,true);if(!output)return m;
        if(session.thread_attributes.size()>=4096)return hostReturn(s,m,orbisError(12));
        const uint64_t address = allocate(session, 64);
        if(!m->fault.empty())return m;
        session.thread_attributes[address] = ThreadAttr{};
        std::memcpy(output,&address,8);
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name,"scePthreadAttrSetdetachstate") || !std::strcmp(name,"scePthreadAttrSetschedpolicy") ||
        !std::strcmp(name,"scePthreadAttrSetschedparam") || !std::strcmp(name,"scePthreadAttrSetaffinity") ||
        !std::strcmp(name,"scePthreadAttrSetstacksize") || !std::strcmp(name,"scePthreadAttrSetinheritsched")) {
        if(!s.gpr.rdi.qword)return hostReturn(s,m,orbisError(22));
        const uint64_t address=load64(*m,s.gpr.rdi.qword);if(!m->fault.empty())return m;
        auto found=session.thread_attributes.find(address);
        if(found==session.thread_attributes.end())return hostReturn(s,m,orbisError(22));
        auto &attr=found->second;const int32_t value=s.gpr.rsi.dword;
        // Retain the virtual-console scheduling policy for eventual worker
        // creation. Recording it does not impose a PS4 scheduler on Darwin.
        if(!std::strcmp(name,"scePthreadAttrSetdetachstate")) {
            if(value!=0 && value!=1)return hostReturn(s,m,orbisError(22));
            attr.detached=value;
        } else if(!std::strcmp(name,"scePthreadAttrSetschedpolicy")) {
            if(value<1 || value>3)return hostReturn(s,m,orbisError(22));
            attr.policy=value;
        } else if(!std::strcmp(name,"scePthreadAttrSetinheritsched")) {
            if(value!=0 && value!=4)return hostReturn(s,m,orbisError(22));
            attr.inherit=value;
        } else if(!std::strcmp(name,"scePthreadAttrSetstacksize")) {
            if(s.gpr.rsi.qword<16384)return hostReturn(s,m,orbisError(22));
            attr.stack=s.gpr.rsi.qword;
        } else if(!std::strcmp(name,"scePthreadAttrSetaffinity"))attr.affinity=s.gpr.rsi.qword;
        else {
            if(!s.gpr.rsi.qword)return hostReturn(s,m,orbisError(22));
            auto input=guestPointer(*m,s.gpr.rsi.qword,4);if(!input)return m;
            std::memcpy(&attr.priority,input,4);
        }
        return hostReturn(s,m,0);
    }
    if (!std::strcmp(name, "scePthreadAttrGet")) {
        if(!s.gpr.rsi.qword)return hostReturn(s,m,orbisError(22));
        const uint64_t address = load64(*m, s.gpr.rsi.qword);
        if(!m->fault.empty())return m;
        auto attr = session.thread_attributes.find(address);
        if (attr == session.thread_attributes.end())
            return hostReturn(s, m, orbisError(22));
        if(s.gpr.rdi.qword==0x40000000){attr->second.stack=session.stack.size();attr->second.stack_address=session.stack_address;}
        else {
            auto thread=session.workers ? session.workers->find(s.gpr.rdi.qword) : nullptr;
            if(!thread)return hostReturn(s,m,orbisError(3));
            attr->second=thread->attributes;
        }
        return hostReturn(s, m, 0);
    }
    if (!std::strcmp(name, "scePthreadAttrGetaffinity") ||
        !std::strcmp(name, "scePthreadAttrDestroy")) {
        if(!s.gpr.rdi.qword)return hostReturn(s,m,orbisError(22));
        auto slot=guestPointer(*m,s.gpr.rdi.qword,8,!std::strcmp(name,"scePthreadAttrDestroy"));if(!slot)return m;
        const uint64_t address = load64(*m, s.gpr.rdi.qword);
        auto attr = session.thread_attributes.find(address);
        if (attr == session.thread_attributes.end())
            return hostReturn(s, m, orbisError(22));
        if (!std::strcmp(name, "scePthreadAttrGetaffinity")) {
            if(!s.gpr.rsi.qword)return hostReturn(s,m,orbisError(22));
            store64(*m, s.gpr.rsi.qword, attr->second.affinity);
        } else {
            session.thread_attributes.erase(attr);
            unmapRegion(*m,address);
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
            unmapRegion(*m,address);
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
        if(!s.gpr.rdi.qword)return hostReturn(s,m,orbisError(22));
        const int error=initializeMutex(session,*m,s.gpr.rdi.qword,type);
        uint64_t result=0;GuestConditions::error(*m,error,result);return m->fault.empty() ? hostReturn(s,m,result) : m;
    }

    if (!std::strcmp(name,"scePthreadMutexLock") || !std::strcmp(name,"scePthreadMutexTrylock") ||
        !std::strcmp(name,"scePthreadMutexUnlock") || !std::strcmp(name,"scePthreadMutexDestroy")) {
        if(!s.gpr.rdi.qword)return hostReturn(s,m,orbisError(22));
        uint64_t id=load64(*m,s.gpr.rdi.qword);if(!m->fault.empty())return m;
        if(id<2) {
            if(!std::strcmp(name,"scePthreadMutexDestroy"))return hostReturn(s,m,0);
            if(!std::strcmp(name,"scePthreadMutexUnlock"))return hostReturn(s,m,orbisError(1));
            const int error=initializeMutex(session,*m,s.gpr.rdi.qword,1);
            if(error || !m->fault.empty()){uint64_t result=0;GuestConditions::error(*m,error,result);return m->fault.empty() ? hostReturn(s,m,result) : m;}
            id=load64(*m,s.gpr.rdi.qword);
        }
        auto found=session.mutexes.find(id);if(found==session.mutexes.end())return hostReturn(s,m,orbisError(22));
        scope.retained_mutex=found->second;
        if(!std::strcmp(name,"scePthreadMutexDestroy")) {
            auto output=guestPointer(*m,s.gpr.rdi.qword,8,true);if(!output)return m;
            if(scope.retained_mutex.use_count()>2)return hostReturn(s,m,orbisError(16));
            const int error=pthread_mutex_destroy(&scope.retained_mutex->mutex);
            uint64_t result=0;GuestConditions::error(*m,error,result);
            if(!error){scope.retained_mutex->initialized=false;session.mutexes.erase(found);unmapRegion(*m,id);const uint64_t zero=0;std::memcpy(output,&zero,8);}
            return m->fault.empty() ? hostReturn(s,m,result) : m;
        }
        auto &held=session.held_mutexes[currentThread()];
        if(!std::strcmp(name,"scePthreadMutexUnlock")) {
            const int error=pthread_mutex_unlock(&scope.retained_mutex->mutex);uint64_t result=0;GuestConditions::error(*m,error,result);
            if(!error){auto own=held.find(id);if(own!=held.end() && own->second.second && !--own->second.second)held.erase(own);}
            return m->fault.empty() ? hostReturn(s,m,result) : m;
        }
        held.try_emplace(id,std::make_pair(scope.retained_mutex,0u));
        int error=0;const char *stopped=nullptr;
        if(!std::strcmp(name,"scePthreadMutexTrylock"))error=pthread_mutex_trylock(&scope.retained_mutex->mutex);
        else {
            scope.release();
            for(;;) {
                if((stopped=guestInterruption(*m)))break;
                if(std::chrono::steady_clock::now()>=m->deadline){stopped="guest mutex lock exceeded host execution deadline";break;}
                error=pthread_mutex_trylock(&scope.retained_mutex->mutex);if(error!=EBUSY)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            scope.acquire();
        }
        if(!error && !stopped)++held.at(id).second;
        else if(!held.at(id).second)held.erase(id);
        if(stopped){stopGuest(*m,stopped);return m;}
        uint64_t result=0;GuestConditions::error(*m,error,result);return m->fault.empty() ? hostReturn(s,m,result) : m;
    }
    missingService(session) = name;
    stopGuest(*m, "missing host service", pc);
    return m;
}
static Memory *gameDispatch(State &s, uint64_t pc, Memory *m) {
    Session &session = *active;
    const uint64_t lookup = pc >= session.image_address && pc - session.image_address < image_size
                                ? pc - session.image_address + image_base : pc;
    if(const char *reason=guestInterruption(*m)){stopGuest(*m,reason,lookup);return m;}
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
        if (!m->fault.empty())resumeGuestFault(*m);
        return result;
    }
    for (const auto &entry : traces)
        if (entry.pc == lookup)
            return entry.function(s, pc, m);
    stopGuest(*m, "missing translated block", lookup);
    return m;
}


static void runWorker(Session &session,MobileWorkers::Worker &worker) {
    WorkerAttachment attachment(session,worker);worker.memory.dispatcher=gameDispatch;
    std::jmp_buf checkpoint;worker.memory.recovery=&checkpoint;
    if(setjmp(checkpoint)==0) {
        worker.state.gpr.rsp.qword=worker.storage->stackAddress()+worker.storage->stackSize()-0xf8;
        worker.state.gpr.rdi.qword=worker.argument;store64(worker.memory,worker.state.gpr.rsp.qword,0xfeedface);
        dispatch(worker.state,worker.entry,&worker.memory);
        if(worker.memory.fault.empty()) {
            if(worker.state.gpr.rip.qword!=0xfeedface)worker.memory.fault="guest worker did not return to its exit sentinel";
            else worker.result=worker.state.gpr.rax.qword;
        }
    }
    worker.memory.recovery=nullptr;worker.memory.cancel_requested=nullptr;
    if(worker.exit_requested && worker.memory.fault=="guest thread exited")worker.memory.fault={};
    if(worker.memory.fault=="guest thread cancellation requested"){worker.result=UINT64_MAX;worker.memory.fault={};}
    if(!worker.memory.fault.empty() && worker.memory.fault!="guest execution interrupted")session.stopping=true;
}

static std::string bootstrap(const char *path,const MobileGpuServices *gpu=nullptr) {
    if (gpu && (!gpu->call || !gpu->finish)) throw std::runtime_error("Invalid native GPU service table");
    auto owner = std::make_unique<Session>(gpu);
    Session *session = owner.get();
    session->memory.dispatcher = gameDispatch;
    if (!readExact(path, image_size, session->image))
        return "{\"error\":\"incorrect image size or unavailable file\"}";
    if (!matchesHash(session->image, image_sha256_hex))
        return "{\"error\":\"incorrect image hash\"}";
    session->stack.resize(8 * 1024 * 1024);
    session->identity = gpu ||
        (std::getenv("OSL_GUEST_IDENTITY") && !std::strcmp(std::getenv("OSL_GUEST_IDENTITY"), "1"));
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
    for(const auto &module:modules) if(module.id) {
        if(module.tls>image_size || module.memsz>image_size-module.tls || module.filesz>module.memsz ||
           !session->tls_templates.define(module.id,session->image.data()+module.tls,module.filesz,module.memsz))
            return "{\"error\":\"unsupported module TLS metadata\"}";
    }
    session->primary_storage=std::make_unique<ThreadStorage>(session->memory,session->tls_templates,session->identity,0x80000000);
    session->workers=std::make_unique<MobileWorkers>(session->memory,session->tls_templates,session->identity,session->stopping);
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
    active_worker = nullptr;
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
    session->stopping=true;
    releaseThreadMutexes(*session,0x40000000);
    session->workers->stopAndJoin();
    auto worker_snapshot=session->workers->snapshot();
    const Memory *failure=&session->memory;
    const State *failed_state=&session->state;
    const char *missing_service=session->missing_service;
    uint64_t failed_thread=0x40000000,total_checkpoints=session->memory.checkpoints;
    unsigned finished_workers=0;
    for(const auto &worker:worker_snapshot) {
        total_checkpoints+=worker->memory.checkpoints;
        if(worker->finished.load())++finished_workers;
        if((failure->fault.empty() || failure->fault=="guest execution interrupted") &&
           !worker->memory.fault.empty() && worker->memory.fault!="guest execution interrupted") {
            failure=&worker->memory;failed_state=&worker->state;missing_service=worker->missing_service;failed_thread=worker->id;
        }
    }
    const uint64_t missing =
        failure->missing_pc >= session->image_address && failure->missing_pc-session->image_address<image_size
            ? failure->missing_pc-session->image_address+image_base : failure->missing_pc;
    // Expose only a normalized executable code location, never general
    // registers, stack, credentials or an arbitrary guest data value.
    const auto executable_pc=[&](uint64_t raw) -> uint64_t {
        if(raw>=session->image_address && raw-session->image_address<image_size) {
            const uint64_t offset=raw-session->image_address;
            for(const auto &segment:segments)if((segment.flags&1) && offset>=segment.address && offset-segment.address<segment.size)
                return image_base+offset;
        }
        return 0;
    };
    const uint64_t stopped_pc=executable_pc(failed_state->gpr.rip.qword);
    uint64_t memset_caller_pc=0;
    if(stopped_pc==0x10cd0ce0) {
        // The verified C ABI helper leaves its return slot untouched on a
        // fault. Read only that checked slot, publish only a validated code PC.
        // Do not call guestPointer after a fault or replace its diagnosis.
        const uint64_t slot=failed_state->gpr.rsp.qword;
        std::shared_lock<std::shared_mutex> lock(failure->mappings->mutex);
        for(const auto &region:failure->mappings->regions)
            if(region.size>=8 && slot>=region.base && slot-region.base<=region.size-8) {
                uint64_t candidate=0;std::memcpy(&candidate,region.data+(slot-region.base),8);
                memset_caller_pc=executable_pc(candidate);break;
            }
    }
    const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - guest_started).count();
    std::ostringstream out;
    out << "{\"stage\":\"" << session->stage
        << "\",\"completed_modules\":" << session->completed_modules
        << ",\"trace_count\":" << std::size(traces)
        << ",\"checkpoints\":" << failure->checkpoints << ",\"guest_elapsed_ms\":" << elapsed_ms
        << ",\"game_booted\":false,\"dispatches\":" << session->calls << ",\"error\":\""
        << failure->fault << "\",\"missing_pc\":" << missing
        << ",\"identity_mapping\":" << (session->identity ? "true" : "false")
        << ",\"image_address\":" << session->image_address
        << ",\"stack_address\":" << session->stack_address
        << ",\"fault_address\":" << failure->fault_address
        << ",\"stopped_pc\":" << stopped_pc
        << ",\"memset_caller_pc\":" << memset_caller_pc
        << ",\"failed_thread\":" << failed_thread << ",\"workers_started\":" << session->workers->started()
        << ",\"workers_finished\":" << finished_workers << ",\"checkpoints_total\":" << total_checkpoints
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
        << ",\"primary_tls_reserved_bytes\":" << session->primary_storage->allocatedBytes()
        << ",\"condition_variables\":" << session->conditions.count()
        << ",\"gpu_adapter_enabled\":" << (session->gpu ? "true" : "false")
        << ",\"gpu_service_calls\":" << session->gpu_service_calls
        << ",\"np_title_configured\":" << (session->offline.title_configured ? "true" : "false")
        << ",\"network_connected\":false,\"psn_signed_in\":false"
        << ",\"save_dialog_initialized\":" << (session->save_dialog_initialized ? "true" : "false")
        << ",\"gesture_setup_calls\":" << session->gesture_setup_calls
        << ",\"gpu_allocations_aligned\":" << (std::all_of(session->gpu_allocations.begin(), session->gpu_allocations.end(), [](const auto &p) { return reinterpret_cast<uint64_t>(p.get()) % 16384 == 0; }) ? "true" : "false")
        << ",\"flexible_reserved_bytes\":" << session->flexible_reserved_bytes
        << ",\"flexible_bytes\":" << session->flexible_bytes << ",\"missing_service\":\""
        << missing_service << "\",\"heap_api_registered\":" << (session->heap_api != 0 ? "true" : "false") << "}";
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
    modules_out << "},\"worker_reports\":[";
    bool first_worker=true;
    for(const auto &worker:worker_snapshot) {
        if(!first_worker)modules_out << ",";first_worker=false;
        const uint64_t raw=worker->memory.missing_pc;
        const uint64_t logical=raw>=session->image_address && raw-session->image_address<image_size ? raw-session->image_address+image_base : raw;
        modules_out << "{\"id\":" << worker->id << ",\"error\":\"" << worker->memory.fault
                    << "\",\"missing_pc\":" << logical << ",\"missing_service\":\"" << worker->missing_service
                    << "\",\"fault_address\":" << worker->memory.fault_address << ",\"checkpoints\":" << worker->memory.checkpoints
                    << ",\"thread_storage_reserved_bytes\":" << worker->storage->allocatedBytes() << "}";
    }
    modules_out << "]}";
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
extern "C" const char *osl_mobile_game_report_with_gpu(const char *path,const MobileGpuServices *gpu) {
    static thread_local std::string report;
    try { report=bootstrap(path,gpu); }
    catch (const std::exception &) { report="{\"error\":\"host GPU bootstrap failure\",\"game_booted\":false}"; }
    return report.c_str();
}
#ifdef OSL_GAME_CLI
extern "C" Memory *__remill_atomic_begin(Memory *);
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
    require(call("scePthreadCondInit",0x20000101)==0 && session.conditions.count()==1);
    const uint64_t condition=load64(session.memory,0x20000101);
    require(condition>1 && call("scePthreadCondBroadcast",0x20000101)==0);
    require(call("scePthreadMutexInit",0x20000121)==0);
    require(call("scePthreadMutexLock",0x20000121)==0);
    session.memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    require(call("scePthreadCondTimedwait",0x20000101,0x20000121,1000)==0x8002003cu);
    require(call("scePthreadMutexUnlock",0x20000121)==0);
    require(call("scePthreadCondDestroy",0x20000101)==0 && session.conditions.count()==0);
    require(load64(session.memory,0x20000101)==0);
    const auto attribute_count=session.thread_attributes.size();
    call("scePthreadAttrInit",0x20000ffc);
    require(!session.memory.fault.empty() && session.thread_attributes.size()==attribute_count);
    require(call("scePthreadAttrInit",0)==0x80020016u && session.thread_attributes.size()==attribute_count);
    session.stack[0x180]=0x7b;session.stack[0x189]=0x7b;
    require(call("scePthreadAttrInit",0x20000181)==0);
    const uint64_t attr_id=load64(session.memory,0x20000181);
    require(session.stack[0x180]==0x7b && session.stack[0x189]==0x7b);
    auto &thread_attr=session.thread_attributes.at(attr_id);
    require(call("scePthreadAttrSetdetachstate",0x20000181,1)==0 && thread_attr.detached==1);
    require(call("scePthreadAttrSetdetachstate",0x20000181,2)==0x80020016u && thread_attr.detached==1);
    require(call("scePthreadAttrSetschedpolicy",0x20000181,3)==0 && thread_attr.policy==3);
    require(call("scePthreadAttrSetschedpolicy",0x20000181,0)==0x80020016u && thread_attr.policy==3);
    require(call("scePthreadAttrSetinheritsched",0x20000181,0)==0 && thread_attr.inherit==0);
    require(call("scePthreadAttrSetinheritsched",0x20000181,1)==0x80020016u && thread_attr.inherit==0);
    require(call("scePthreadAttrSetstacksize",0x20000181,16384)==0 && thread_attr.stack==16384);
    require(call("scePthreadAttrSetstacksize",0x20000181,16383)==0x80020016u && thread_attr.stack==16384);
    const int32_t priority=725;std::memcpy(session.stack.data()+0x1a1,&priority,4);
    require(call("scePthreadAttrSetschedparam",0x20000181,0x200001a1)==0 && thread_attr.priority==priority);
    call("scePthreadAttrSetschedparam",0x20000181,0x20000fff);
    require(!session.memory.fault.empty() && thread_attr.priority==priority);
    require(call("scePthreadAttrSetaffinity",0x20000181,0x5a)==0 && thread_attr.affinity==0x5a);
    require(call("scePthreadAttrGetaffinity",0x20000181,0x200001a1)==0 && load64(session.memory,0x200001a1)==0x5a);
    require(call("scePthreadAttrDestroy",0x20000181)==0 && session.thread_attributes.size()==attribute_count);
    require(load64(session.memory,0x20000181)==0 && session.stack[0x180]==0x7b && session.stack[0x189]==0x7b);
    require(call("scePthreadAttrSetdetachstate",0x20000181,0)==0x80020016u);
    std::memset(session.stack.data()+0x300,0x51,16);
    require(session.tls_templates.define(2,session.stack.data()+0x300,16,32));
    session.primary_storage=std::make_unique<ThreadStorage>(session.memory,session.tls_templates,false,0x90000000);
    store64(session.memory,0x200001a1,2);store64(session.memory,0x200001a9,0);
    const uint64_t tls=call("__tls_get_addr",0x200001a1);
    auto tls_data=guestPointer(session.memory,tls,32);
    require(tls==0x90000000 && tls_data && std::all_of(tls_data,tls_data+16,[](uint8_t x){return x==0x51;}));
    require(std::all_of(tls_data+16,tls_data+32,[](uint8_t x){return x==0;}));
    tls_data[31]=0x7b;store64(session.memory,0x200001a9,31);
    require(call("__tls_get_addr",0x200001a1)==tls+31 && tls_data[31]==0x7b);
    store64(session.memory,0x200001a9,32);call("__tls_get_addr",0x200001a1);
    require(session.memory.fault=="unsupported TLS module or offset");
    call("__tls_get_addr",0x20000ff1);require(!session.memory.fault.empty());
    store64(session.memory,0x200001a1,3);store64(session.memory,0x200001a9,0);call("__tls_get_addr",0x200001a1);
    require(session.memory.fault=="unsupported TLS module or offset");
    session.primary_storage.reset();session.memory.fault={};
    require(!guestPointer(session.memory,tls,1));session.memory.fault={};
    // Both optimized library helpers and HLE faults must release a guest
    // atomic region before the outer checkpoint resumes on another CPU.
    std::jmp_buf host_fault;
    for(unsigned kind=0;kind<2;++kind) {
        session.memory.fault={};session.memory.recovery=&host_fault;
        if(setjmp(host_fault)==0) {
            __remill_atomic_begin(&session.memory);
            if(!kind){session.state=State{};session.state.gpr.rdx.qword=DirectMemory::capacity()+1;__osl_libc_memset(session.state,0x10cd0ce0,&session.memory);}
            else {
                session.state=State{};session.state.gpr.rdi.qword=0x20000ff1;session.state.gpr.rsp.qword=0x20000ff8;
                for(size_t i=0;i<std::size(imports);++i)if(!std::strcmp(imports[i].symbol,"sceKernelGettimeofday")){gameDispatch(session.state,hle_base+i*0x1000,&session.memory);break;}
            }
            throw std::runtime_error("Host fault did not reach its checkpoint");
        }
        session.memory.recovery=nullptr;require(!session.memory.atomic_held && !session.memory.fault.empty());
        std::atomic<bool> available{false};std::thread next_cpu([&]{GuestMemoryScope memory;available=true;});next_cpu.join();require(available);
    }
    session.memory.fault={};
    // Real concurrent import dispatch: waits and joins must release frontend
    // leases so another guest CPU can signal, unlock and finish.
    session.memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    session.workers=std::make_unique<MobileWorkers>(session.memory,session.tls_templates,false,session.stopping);
    require(call("sceKernelCreateEventFlag",0x20000401,0,0x20,0)==0);
    const uint64_t shared_flag=load64(session.memory,0x20000401);
    std::atomic<bool> event_entered{false},event_passed{false};
    auto worker_call=[&](MobileWorkers::Worker &node,const char *name,uint64_t first=0,uint64_t second=0,uint64_t third=0,uint64_t fourth=0,uint64_t fifth=0) {
        if(!node.memory.fault.empty())return UINT64_MAX;
        node.state=State{};node.state.gpr.rsp.qword=node.storage->stackAddress()+node.storage->stackSize()-0xf8;
        store64(node.memory,node.state.gpr.rsp.qword,0xfeedface);
        node.state.gpr.rdi.qword=first;node.state.gpr.rsi.qword=second;node.state.gpr.rdx.qword=third;node.state.gpr.rcx.qword=fourth;node.state.gpr.r8.qword=fifth;
        for(size_t i=0;i<std::size(imports);++i)if(!std::strcmp(imports[i].symbol,name)){hostService(node.state,hle_base+i*0x1000,&node.memory);return node.state.gpr.rax.qword;}
        return UINT64_MAX;
    };
    ThreadAttr small;small.stack=65536;uint64_t event_thread=0;
    require(session.workers->spawn(1,0,small,[&](auto &node) {
        WorkerAttachment attachment(session,node);event_entered=true;
        const bool ok=worker_call(node,"sceKernelWaitEventFlag",shared_flag,1,1,0x20000411)==0 && node.memory.fault.empty();
        event_passed=ok;
    },event_thread)==0);
    while(!event_entered)std::this_thread::yield();
    require(call("sceKernelSetEventFlag",shared_flag,1)==0);
    require(call("scePthreadJoin",event_thread,0x20000421)==0 && event_passed);
    require(load64(session.memory,0x20000411)==1);
    require(call("sceKernelDeleteEventFlag",shared_flag)==0);
    require(call("scePthreadMutexInit",0x20000431)==0 && call("scePthreadCondInit",0x20000441)==0);
    std::atomic<bool> condition_entered{false},condition_passed{false};uint64_t condition_thread=0;
    session.stack[0x451]=0;
    require(session.workers->spawn(1,0,small,[&](auto &node) {
        WorkerAttachment attachment(session,node);
        bool ok=worker_call(node,"scePthreadMutexLock",0x20000431)==0;
        condition_entered=true;
        while(ok && !session.stack[0x451])ok=worker_call(node,"scePthreadCondWait",0x20000441,0x20000431)==0 && node.memory.fault.empty();
        ok=ok && worker_call(node,"scePthreadMutexUnlock",0x20000431)==0;
        const auto address=worker_call(node,"__tls_get_addr",0x20000501);
        if(node.memory.fault.empty()){GuestMemoryScope memory;auto data=guestPointer(node.memory,address,32);ok=ok && data && data[0]==0x51 && data[31]==0;}
        condition_passed=ok && node.memory.fault.empty();
    },condition_thread)==0);
    store64(session.memory,0x20000501,2);store64(session.memory,0x20000509,0);
    while(!condition_entered)std::this_thread::yield();
    require(call("scePthreadMutexLock",0x20000431)==0);
    {GuestMemoryScope memory;session.stack[0x451]=1;}
    require(call("scePthreadCondBroadcast",0x20000441)==0 && call("scePthreadMutexUnlock",0x20000431)==0);
    require(call("scePthreadJoin",condition_thread,0)==0 && condition_passed);
    require(call("scePthreadCondDestroy",0x20000441)==0 && call("scePthreadMutexDestroy",0x20000431)==0);
    require(call("scePthreadJoin",condition_thread,0)==0x80020016u);
    require(call("scePthreadGetprio",UINT64_MAX,0x20000461)==0x80020003u);
    require(call("scePthreadSetprio",0x40000000,725)==0 && call("scePthreadGetprio",0x40000000,0x20000461)==0);
    int32_t observed_priority=0;std::memcpy(&observed_priority,session.stack.data()+0x461,4);require(observed_priority==725);
    call("scePthreadGetprio",0x40000000,0x20000fff);require(!session.memory.fault.empty());
    require(call("scePthreadSetaffinity",0x40000000,0x55)==0 && call("scePthreadGetaffinity",0x40000000,0x20000461)==0);
    require(load64(session.memory,0x20000461)==0x55);
    require(call("sceKernelGetTscFrequency")==1000000000);
    const uint64_t ticks=call("sceKernelReadTsc"),process_time=call("sceKernelGetProcessTime");
    require(call("sceKernelReadTsc")>=ticks && call("sceKernelGetProcessTime")>=process_time);
    require(call("sceKernelClockGettime",0,0x20000481)==0 && session.state.gpr.rsp.qword==0x20001000);
    require(call("sceKernelClockGettime",3,UINT64_MAX)==0x80020016u && session.memory.fault.empty());
    require(call("sceKernelGettimeofday",0x200004a1)==0 && call("sceKernelGettimeofday",0)==0x80020016u);
    call("sceKernelGettimeofday",0x20000ff1);require(!session.memory.fault.empty());
    session.workers.reset();session.memory.fault={};
    active = nullptr;
    std::printf("{\"service_checks\":%u,\"failed\":0,\"game_booted\":false}\n", checks);
    return true;
}
static bool workerCleanupCheck() {
    std::shared_ptr<HostMutex> retained;
    std::atomic<bool> entered{false},finished{false};bool expected_failure=false;
    try {
        auto owner=std::make_unique<Session>();auto &session=*owner;session.stack.resize(4096);
        mapRegion(session.memory,0x20000000,session.stack.data(),session.stack.size());
        session.memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        session.workers=std::make_unique<MobileWorkers>(session.memory,session.tls_templates,false,session.stopping);
        active=&session;active_worker=nullptr;
        auto invoke=[&](State &state,Memory &memory,uint64_t stack,const char *name,uint64_t first,uint64_t second=0) {
            state=State{};state.gpr.rsp.qword=stack;store64(memory,stack,0xfeedface);
            state.gpr.rdi.qword=first;state.gpr.rsi.qword=second;
            for(size_t i=0;i<std::size(imports);++i)if(!std::strcmp(imports[i].symbol,name)){hostService(state,hle_base+i*0x1000,&memory);return state.gpr.rax.qword;}
            return UINT64_MAX;
        };
        auto call=[&](const char *name,uint64_t first,uint64_t second=0){return invoke(session.state,session.memory,0x20000ff8,name,first,second);};
        if(call("scePthreadMutexInit",0x20000101) || call("scePthreadCondInit",0x20000111))throw std::logic_error("cleanup fixture setup failed");
        retained=session.mutexes.at(load64(session.memory,0x20000101));
        ThreadAttr attributes;attributes.stack=65536;uint64_t id=0;
        if(session.workers->spawn(1,0,attributes,[&](auto &node) {
            WorkerAttachment attachment(session,node);
            const uint64_t stack=node.storage->stackAddress()+node.storage->stackSize()-0xf8;
            bool ok=invoke(node.state,node.memory,stack,"scePthreadMutexLock",0x20000101)==0;
            entered=true;
            if(ok)invoke(node.state,node.memory,stack,"scePthreadCondWait",0x20000111,0x20000101);
            finished=ok && node.memory.fault=="guest execution interrupted";
        },id))throw std::logic_error("cleanup fixture spawn failed");
        while(!entered)std::this_thread::yield();
        if(call("scePthreadMutexLock",0x20000101))throw std::logic_error("cleanup fixture parent lock failed");
        // Simulate a native failure while the parent owns the guest mutex and
        // the child must reacquire it to leave its condition wait.
        throw std::runtime_error("owned worker cleanup fixture");
    } catch(const std::logic_error &){throw;}
      catch(const std::runtime_error &){expected_failure=true;}
    active=nullptr;active_worker=nullptr;
    if(!expected_failure || !finished || !retained || pthread_mutex_trylock(&retained->mutex))throw std::runtime_error("worker exception cleanup did not release ownership");
    if(pthread_mutex_unlock(&retained->mutex))throw std::runtime_error("worker cleanup mutex unlock failed");
    retained.reset();
    std::atomic<bool> child_locked{false};std::shared_ptr<HostMutex> child_mutex;
    {
        auto owner=std::make_unique<Session>();auto &session=*owner;session.stack.resize(4096);
        mapRegion(session.memory,0x20000000,session.stack.data(),session.stack.size());
        session.memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        session.workers=std::make_unique<MobileWorkers>(session.memory,session.tls_templates,false,session.stopping);
        active=&session;active_worker=nullptr;
        if(initializeMutex(session,session.memory,0x20000101,1))throw std::logic_error("worker-unwind fixture setup failed");
        child_mutex=session.mutexes.at(load64(session.memory,0x20000101));
        ThreadAttr attributes;attributes.stack=65536;uint64_t id=0;
        if(session.workers->spawn(1,0,attributes,[&](auto &node) {
            WorkerAttachment attachment(session,node);
            node.state.gpr.rsp.qword=node.storage->stackAddress()+node.storage->stackSize()-0xf8;
            store64(node.memory,node.state.gpr.rsp.qword,0xfeedface);node.state.gpr.rdi.qword=0x20000101;
            for(size_t i=0;i<std::size(imports);++i)if(!std::strcmp(imports[i].symbol,"scePthreadMutexLock")){hostService(node.state,hle_base+i*0x1000,&node.memory);break;}
            child_locked=node.state.gpr.rax.qword==0 && node.memory.fault.empty();
            throw std::runtime_error("owned child-unwind fixture");
        },id))throw std::logic_error("worker-unwind fixture spawn failed");
        auto worker=session.workers->find(id);
        while(!child_locked && !worker->finished.load())std::this_thread::yield();
        session.workers->stopAndJoin();
        if(!child_locked || worker->memory.fault!="native worker callback failed")throw std::runtime_error("child failure did not unwind its ownership");
    }
    active=nullptr;active_worker=nullptr;
    if(!child_mutex || pthread_mutex_trylock(&child_mutex->mutex))throw std::runtime_error("child failure retained its mutex");
    if(pthread_mutex_unlock(&child_mutex->mutex))throw std::runtime_error("child failure cleanup unlock failed");
    child_mutex.reset();
    std::puts("{\"worker_cleanup_checks\":2,\"failure_injected\":true,\"child_joined\":true,\"parent_mutex_released\":true,\"child_exception_released_mutex\":true,\"failed\":0,\"game_frames\":false}");
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    try {
        if (!std::strcmp(argv[1], "--service-check")) return serviceChecks() ? 0 : 1;
        if (!std::strcmp(argv[1], "--worker-cleanup-check")) return workerCleanupCheck() ? 0 : 1;
        std::printf("OSL_STARTUP_RESULT:%s\n", bootstrap(argv[1]).c_str());
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return 3;
    }
}
#endif
