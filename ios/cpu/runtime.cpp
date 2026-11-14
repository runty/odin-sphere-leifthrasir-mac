#include "runtime.h"
#include <cstring>
#include <limits>
#include <stdexcept>

void mapRegion(Memory &m, uint64_t base, uint8_t *data, size_t size) {
    if (!size || size > std::numeric_limits<uint64_t>::max() - base)
        throw std::runtime_error("invalid guest mapping");
    for (const auto &r : m.regions)
        if (base < r.base + r.size && r.base < base + size)
            throw std::runtime_error("overlapping guest mappings");
    m.regions.push_back({base, data, size});
}
uint8_t *guestPointer(Memory &m, uint64_t address, size_t size) {
    for (const auto &r : m.regions)
        if (size <= r.size && address >= r.base && address - r.base <= r.size - size)
            return r.data + (address - r.base);
    m.fault = "unmapped guest memory";
    return nullptr;
}
#define SCALAR(BITS, TYPE) \
extern "C" TYPE __remill_read_memory_##BITS(Memory *m, uint64_t a) { \
    TYPE value{}; if (auto p = guestPointer(*m, a, sizeof(value))) std::memcpy(&value, p, sizeof(value)); return value; } \
extern "C" Memory *__remill_write_memory_##BITS(Memory *m, uint64_t a, TYPE v) { \
    if (auto p = guestPointer(*m, a, sizeof(v))) std::memcpy(p, &v, sizeof(v)); return m; }
SCALAR(8, uint8_t)
SCALAR(16, uint16_t)
SCALAR(32, uint32_t)
SCALAR(64, uint64_t)
SCALAR(f32, float)
SCALAR(f64, double)
#undef SCALAR
extern "C" Memory *__remill_function_return(State &, uint64_t, Memory *m) { ++m->returns; return m; }
extern "C" Memory *__remill_function_call(State &s, uint64_t pc, Memory *m) { return dispatch(s, pc, m); }
extern "C" Memory *__remill_jump(State &s, uint64_t pc, Memory *m) { return dispatch(s, pc, m); }
extern "C" Memory *__remill_missing_block(State &s, uint64_t pc, Memory *m) { return dispatch(s, pc, m); }
#define FLAG(NAME) extern "C" bool __remill_flag_computation_##NAME(bool v, ...) { return v; }
FLAG(carry) FLAG(zero) FLAG(sign) FLAG(overflow)
#undef FLAG
#define COMPARE(NAME) extern "C" bool __remill_compare_##NAME(bool v) { return v; }
COMPARE(eq) COMPARE(neq) COMPARE(ult) COMPARE(ule) COMPARE(ugt) COMPARE(uge)
COMPARE(slt) COMPARE(sle) COMPARE(sgt) COMPARE(sge)
#undef COMPARE
extern "C" uint8_t __remill_undefined_8() { return 0; }
