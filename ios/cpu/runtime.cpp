#include "runtime.h"
#include <cstdlib>
#include <cstdio>
#include <cfenv>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <climits>
#include <exception>
#ifdef __APPLE__
#include <os/lock.h>
#endif

#ifdef __APPLE__
// Isolated performance study: preserve the single global recursive critical
// section, but keep this one lock's recursion count in thread-local storage.
// No ownership, mapping, alias or guest atomicity rules are relaxed.
class GuestAtomicMutex {
    os_unfair_lock lock_=OS_UNFAIR_LOCK_INIT;
    static thread_local unsigned depth_;
public:
    void lock() {
        if(depth_==UINT_MAX) std::terminate();
        if(depth_) ++depth_;
        else { os_unfair_lock_lock(&lock_); depth_=1; }
    }
    void unlock() {
        if(!depth_) std::terminate();
        if(--depth_==0) os_unfair_lock_unlock(&lock_);
    }
};
thread_local unsigned GuestAtomicMutex::depth_=0;
#else
using GuestAtomicMutex=std::recursive_mutex;
#endif
static GuestAtomicMutex guestAtomics;
namespace {
// OpenCrown's bounded, strongly-owned descriptor cache, adapted for Odin's
// removable/protected mappings. Never cache the external direct-memory table:
// it contains physical aliases and changes permissions independently.
struct RegionCache {
    std::shared_ptr<Memory::Mappings> mappings;
    uint64_t generation=0;
    std::array<Memory::Region,8> entries{};
};
thread_local RegionCache regionCache;
static bool contains(const Memory::Region &r,uint64_t address,size_t size) {
    return r.data && size<=r.size && address>=r.base && address-r.base<=r.size-size;
}
}
GuestMemoryScope::GuestMemoryScope(){lock();}
GuestMemoryScope::~GuestMemoryScope(){unlock();}
void GuestMemoryScope::lock(){if(!held_){guestAtomics.lock();held_=true;}}
void GuestMemoryScope::unlock(){if(held_){held_=false;guestAtomics.unlock();}}
const char *guestInterruption(const Memory &m) {
    if(m.cancel_requested && m.cancel_requested->load(std::memory_order_relaxed))return "guest thread cancellation requested";
    if(m.stop_requested && m.stop_requested->load(std::memory_order_relaxed))return "guest execution interrupted";
    return nullptr;
}
Memory::~Memory() {
    if (atomic_held)
        guestAtomics.unlock();
}

void resumeGuestFault(Memory &m) {
    if (m.recovery) {
        if (m.atomic_held) {
            m.atomic_held = false;
            guestAtomics.unlock();
        }
        std::longjmp(*m.recovery, 1);
    }
}
void stopGuest(Memory &m, const char *reason, uint64_t pc) {
    m.fault = reason;
    m.missing_pc = pc;
    resumeGuestFault(m);
}
extern "C" void __osl_guest_tick(Memory *m, const State *s) {
    if(const char *reason=guestInterruption(*m))stopGuest(*m,reason,s->gpr.rip.qword);
    if (++m->checkpoints > m->checkpoint_limit)
        stopGuest(*m, "guest execution budget exhausted", s->gpr.rip.qword);
    if ((m->checkpoints & 1023) == 0 && std::chrono::steady_clock::now() > m->deadline)
        stopGuest(*m, "guest execution deadline exceeded", s->gpr.rip.qword);
    if (!m->fault.empty()) {
        resumeGuestFault(*m);
        std::abort(); // A guarded guest needs an enclosing checkpoint.
    }
}
Memory *dispatch(State &s, uint64_t pc, Memory *m) {
    if (m->dispatcher)
        return m->dispatcher(s, pc, m);
    stopGuest(*m, "guest dispatcher is not configured", pc);
    return m;
}

void mapRegion(Memory &m, uint64_t base, uint8_t *data, size_t size, bool writable) {
    if (!data || !size || size > std::numeric_limits<uint64_t>::max() - base)
        throw std::runtime_error("invalid guest mapping");
    std::lock_guard<GuestAtomicMutex> access(guestAtomics);
    std::unique_lock<std::shared_mutex> lock(m.mappings->mutex);
    for (const auto &r : m.mappings->regions)
        if (base < r.base + r.size && r.base < base + size)
            throw std::runtime_error("overlapping guest mappings");
    m.mappings->regions.push_back({base, data, size, writable});
    m.mappings->generation.fetch_add(1,std::memory_order_release);
}
bool unmapRegion(Memory &m,uint64_t base) {
    std::lock_guard<GuestAtomicMutex> access(guestAtomics);
    std::unique_lock<std::shared_mutex> lock(m.mappings->mutex);
    auto &regions=m.mappings->regions;
    for(auto it=regions.begin();it!=regions.end();++it)if(it->base==base){regions.erase(it);m.mappings->generation.fetch_add(1,std::memory_order_release);return true;}
    return false;
}
uint8_t *guestPointer(Memory &m, uint64_t address, size_t size, bool writing) {
    uint8_t *pointer=nullptr;bool protected_write=false;
    auto &cache=regionCache;
    const auto generation=m.mappings->generation.load(std::memory_order_acquire);
    if(cache.mappings!=m.mappings || cache.generation!=generation) {
        cache.entries={};cache.mappings=m.mappings;cache.generation=generation;
    }
    // Empty spans can match both sides of a touching guest boundary. Preserve
    // the original first-registered descriptor and its write permissions.
    for(size_t i=0;size && i<cache.entries.size();++i)if(contains(cache.entries[i],address,size)) {
        const auto region=cache.entries[i];
        if(i)std::swap(cache.entries[0],cache.entries[i]);
        if(writing && !region.writable)protected_write=true;
        else pointer=region.data+(address-region.base);
        break;
    }
    if(!pointer && !protected_write) {
        std::shared_lock<std::shared_mutex> lock(m.mappings->mutex);
        // A mutation could race a borrowed-pointer lookup without an outer
        // GuestMemoryScope. Publish only a coherent generation/descriptor set.
        const auto locked_generation=m.mappings->generation.load(std::memory_order_relaxed);
        if(cache.generation!=locked_generation){cache.entries={};cache.generation=locked_generation;}
        for(const auto &region:m.mappings->regions)if(contains(region,address,size)) {
            if(size) {
                for(size_t i=cache.entries.size()-1;i;--i)cache.entries[i]=cache.entries[i-1];
                cache.entries[0]=region;
            }
            if(writing && !region.writable)protected_write=true;
            else pointer=region.data+(address-region.base);
            break;
        }
    } // Release the mapping lock before a fault can longjmp.
    if(protected_write){m.fault_address=address;stopGuest(m,"guest write to protected region");return nullptr;}
    if(pointer)return pointer;
    if (m.external_pointer)
        if (auto p = m.external_pointer(address, size, writing)) return p;
    m.fault_address = address;
    stopGuest(m, "unmapped guest memory");
    return nullptr;
}
static void transfer(Memory &m, uint64_t address, void *value, size_t size, bool writing) {
    auto recovery = m.recovery;
    m.recovery = nullptr;
    guestAtomics.lock();
    if (auto p = guestPointer(m, address, size, writing)) {
        if (writing)
            std::memcpy(p, value, size);
        else
            std::memcpy(value, p, size);
    }
    guestAtomics.unlock();
    m.recovery = recovery;
    if (!m.fault.empty())
        resumeGuestFault(m);
}
#define SCALAR(BITS, TYPE)                                                                         \
    extern "C" TYPE __remill_read_memory_##BITS(Memory *m, uint64_t a) {                           \
        TYPE value{};                                                                              \
        transfer(*m, a, &value, sizeof(value), false);                                             \
        return value;                                                                              \
    }                                                                                              \
    extern "C" Memory *__remill_write_memory_##BITS(Memory *m, uint64_t a, TYPE v) {               \
        transfer(*m, a, &v, sizeof(v), true);                                                      \
        return m;                                                                                  \
    }
SCALAR(8, uint8_t)
SCALAR(16, uint16_t)
SCALAR(32, uint32_t)
SCALAR(64, uint64_t)
SCALAR(f32, float)
SCALAR(f64, double)
#undef SCALAR

// Remill may emit port operations on an untaken path of a lifted trace.
// The application guest has no port I/O privileges or host port mapping.
[[noreturn]] static void rejectPortIO(Memory *memory, uint64_t port) {
    memory->fault_address = port;
    stopGuest(*memory, "unsupported guest port I/O");
    std::abort(); // No enclosing guest recovery boundary.
}
#define PORT_IO(BITS, TYPE)                                                        \
    extern "C" TYPE __remill_read_io_port_##BITS(Memory *memory, uint64_t port) {   \
        rejectPortIO(memory, port);                                                \
    }                                                                              \
    extern "C" Memory *__remill_write_io_port_##BITS(Memory *memory, uint64_t port, TYPE) { \
        rejectPortIO(memory, port);                                                \
    }
PORT_IO(8, uint8_t)
PORT_IO(16, uint16_t)
PORT_IO(32, uint32_t)
#undef PORT_IO
extern "C" Memory *__remill_function_return(State &, uint64_t, Memory *m) {
    ++m->returns;
    return m;
}
extern "C" Memory *__remill_function_call(State &s, uint64_t pc, Memory *m) {
    return dispatch(s, pc, m);
}
extern "C" Memory *__remill_jump(State &s, uint64_t pc, Memory *m) { return dispatch(s, pc, m); }
extern "C" Memory *__remill_missing_block(State &s, uint64_t pc, Memory *m) {
    return dispatch(s, pc, m);
}
extern "C" Memory *__remill_async_hyper_call(State &, uint64_t pc, Memory *m) {
    stopGuest(*m, "unsupported guest async hypercall", pc);
    return m;
}
extern "C" Memory *__remill_sync_hyper_call(State &s, Memory *m, uint32_t kind) {
    stopGuest(*m, kind == 0x101 ? "unsupported AMD64 instruction fallback"
                              : "unsupported guest synchronous hypercall", s.gpr.rip.qword);
    return m;
}
extern "C" Memory *__remill_error(State &, uint64_t pc, Memory *m) {
    stopGuest(*m, "unsupported guest instruction", pc);
    std::abort();
}
#define FLAG(NAME)                                                                                 \
    extern "C" bool __remill_flag_computation_##NAME(bool v, ...) { return v; }
FLAG(carry)
FLAG(zero) FLAG(sign) FLAG(overflow)
#undef FLAG
#define COMPARE(NAME)                                                                              \
    extern "C" bool __remill_compare_##NAME(bool v) { return v; }
    COMPARE(eq) COMPARE(neq) COMPARE(ult) COMPARE(ule) COMPARE(ugt) COMPARE(uge) COMPARE(slt)
        COMPARE(sle) COMPARE(sgt) COMPARE(sge)
#undef COMPARE
            extern "C" uint8_t __remill_undefined_8() {
    return 0;
}
extern "C" Memory *__remill_atomic_begin(Memory *m) {
    if (m->atomic_held) {
        stopGuest(*m, "nested guest atomic region");
        return m;
    }
    guestAtomics.lock();
    m->atomic_held = true;
    return m;
}
// x86 barriers must retain their ordering when AOT code runs on arm64. Use the
// strongest fence until directional weakening has independent concurrency proof.
#define BARRIER(NAME) \
    extern "C" Memory *__remill_barrier_##NAME(Memory *m) { \
        std::atomic_thread_fence(std::memory_order_seq_cst); return m; \
    }
BARRIER(load_load)
BARRIER(load_store)
BARRIER(store_load)
BARRIER(store_store)
#undef BARRIER
extern "C" Memory *__remill_atomic_end(Memory *m) {
    if (!m->atomic_held) {
        stopGuest(*m, "unbalanced guest atomic region");
        return m;
    }
    m->atomic_held = false;
    guestAtomics.unlock();
    return m;
}
template<class T> static Memory *compareExchange(Memory *m,uint64_t address,T &expected,T desired) {
    auto recovery = m->recovery;
    m->recovery = nullptr;
    guestAtomics.lock();
    if (auto pointer = guestPointer(*m, address, sizeof(T), true)) {
        T current;
        std::memcpy(&current, pointer, sizeof(T));
        if (current == expected)
            std::memcpy(pointer, &desired, sizeof(T));
        expected = current;
    }
    guestAtomics.unlock();
    m->recovery = recovery;
    if (!m->fault.empty())
        resumeGuestFault(*m);
    return m;
}

// OpenCrown's typed CAS family, retaining Odin's fault/recovery and host lock.
#define COMPARE_EXCHANGE(BITS, TYPE) \
    extern "C" Memory *__remill_compare_exchange_memory_##BITS(Memory *m,uint64_t address,TYPE &expected,TYPE desired) { \
        return compareExchange(m,address,expected,desired); \
    }
COMPARE_EXCHANGE(8,uint8_t)
COMPARE_EXCHANGE(16,uint16_t)
COMPARE_EXCHANGE(32,uint32_t)
COMPARE_EXCHANGE(64,uint64_t)
#undef COMPARE_EXCHANGE
extern "C" Memory *__remill_compare_exchange_memory_128(Memory *m,uint64_t address,uint128_t &expected,uint128_t &desired) {
    // Remill's wide ABI passes desired by reference, unlike narrow operands.
    return compareExchange(m,address,expected,desired);
}

// Adapted from OpenCrown 4d90913, ios/cpu/fp80/exceptions.c. Remill uses
// x87 bit positions; Darwin arm64 FE_* values differ. Odin has no FP80
// arithmetic provider, so those operations remain explicit backend gaps.
// Preserve the two x87-only flags in thread-local storage instead of losing
// them when the host has no standard fenv representation.
namespace {
thread_local uint32_t fpu_extra_flags=0;
int host_fpu_mask(uint32_t mask) {
    return ((mask&1)?FE_INVALID:0) | ((mask&4)?FE_DIVBYZERO:0) |
           ((mask&8)?FE_OVERFLOW:0) | ((mask&16)?FE_UNDERFLOW:0) |
           ((mask&32)?FE_INEXACT:0);
}
uint32_t remill_fpu_mask(int flags) {
    return ((flags&FE_INVALID)?1u:0u) | ((flags&FE_DIVBYZERO)?4u:0u) |
           ((flags&FE_OVERFLOW)?8u:0u) | ((flags&FE_UNDERFLOW)?16u:0u) |
           ((flags&FE_INEXACT)?32u:0u);
}
}
extern "C" int32_t __remill_fpu_exception_test(int32_t mask) {
    return int32_t((remill_fpu_mask(std::fetestexcept(host_fpu_mask(uint32_t(mask)))) |
                    fpu_extra_flags) & uint32_t(mask) & 127u);
}
extern "C" void __remill_fpu_exception_clear(int32_t mask) {
    std::feclearexcept(host_fpu_mask(uint32_t(mask)));
    fpu_extra_flags &= ~uint32_t(mask);
}
extern "C" void __remill_fpu_exception_raise(int32_t mask) {
    std::feraiseexcept(host_fpu_mask(uint32_t(mask)));
    fpu_extra_flags |= uint32_t(mask)&0x42u;
}

// Remill's 0..3 rounding control is independent of the host FE_* encoding.
extern "C" int32_t __remill_fpu_get_rounding() {
    switch(std::fegetround()) {
    case FE_TONEAREST:return 0;
    case FE_DOWNWARD:return 1;
    case FE_UPWARD:return 2;
    case FE_TOWARDZERO:return 3;
    default:std::fputs("AOT: unsupported host FP rounding mode\n",stderr);std::abort();
    }
}
extern "C" void __remill_fpu_set_rounding(int32_t mode) {
    static const int native[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    // Invalid intrinsic arguments are an internal ABI error, not a mode to
    // silently mask. Never throw a C++ exception through lifted LLVM frames.
    if(mode<0 || mode>3 || std::fesetround(native[mode])) {
        std::fputs("AOT: invalid FP rounding mode\n",stderr);std::abort();
    }
}
