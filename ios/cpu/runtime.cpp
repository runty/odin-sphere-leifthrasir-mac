#include "runtime.h"
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>

static std::recursive_mutex guestAtomics;
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

static void resumeFault(Memory &m) {
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
    resumeFault(m);
}
extern "C" void __osl_guest_tick(Memory *m, const State *s) {
    if(const char *reason=guestInterruption(*m))stopGuest(*m,reason,s->gpr.rip.qword);
    if (++m->checkpoints > m->checkpoint_limit)
        stopGuest(*m, "guest execution budget exhausted", s->gpr.rip.qword);
    if ((m->checkpoints & 1023) == 0 && std::chrono::steady_clock::now() > m->deadline)
        stopGuest(*m, "guest execution deadline exceeded", s->gpr.rip.qword);
    if (!m->fault.empty()) {
        resumeFault(*m);
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
    std::unique_lock<std::shared_mutex> lock(m.mappings->mutex);
    for (const auto &r : m.mappings->regions)
        if (base < r.base + r.size && r.base < base + size)
            throw std::runtime_error("overlapping guest mappings");
    m.mappings->regions.push_back({base, data, size, writable});
}
bool unmapRegion(Memory &m,uint64_t base) {
    std::unique_lock<std::shared_mutex> lock(m.mappings->mutex);
    auto &regions=m.mappings->regions;
    for(auto it=regions.begin();it!=regions.end();++it)if(it->base==base){regions.erase(it);return true;}
    return false;
}
uint8_t *guestPointer(Memory &m, uint64_t address, size_t size, bool writing) {
    uint8_t *pointer=nullptr;bool protected_write=false;
    {
    std::shared_lock<std::shared_mutex> lock(m.mappings->mutex);
    for (const auto &r : m.mappings->regions)
        if (size <= r.size && address >= r.base && address - r.base <= r.size - size) {
            if (writing && !r.writable) {
                protected_write=true;break;
            }
            pointer=r.data+(address-r.base);break;
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
        resumeFault(m);
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
extern "C" Memory *__remill_compare_exchange_memory_32(Memory *m, uint64_t address,
                                                       uint32_t &expected, uint32_t desired) {
    auto recovery = m->recovery;
    m->recovery = nullptr;
    guestAtomics.lock();
    if (auto pointer = guestPointer(*m, address, 4, true)) {
        uint32_t current;
        std::memcpy(&current, pointer, 4);
        if (current == expected)
            std::memcpy(pointer, &desired, 4);
        expected = current;
    }
    guestAtomics.unlock();
    m->recovery = recovery;
    if (!m->fault.empty())
        resumeFault(*m);
    return m;
}
