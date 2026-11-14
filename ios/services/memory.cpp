#include "memory.h"
extern "C" uintptr_t runtime_memory_resolve(const char *);
extern "C" int osl_aot_memory_access(uintptr_t, size_t, int);
extern "C" void osl_aot_memory_reset();
extern "C" uint64_t osl_aot_memory_allocated();
static std::mutex manager;
static constexpr int32_t invalid=static_cast<int32_t>(0x80020016u);
static bool validSpan(uintptr_t address, uint64_t size) {
    return address<(uint64_t(1)<<40) && size && size<=(uint64_t(1)<<40)-address;
}
DirectMemory::DirectMemory() : lease(manager) {}
DirectMemory::~DirectMemory() { osl_aot_memory_reset(); }
uint64_t DirectMemory::allocated() { return osl_aot_memory_allocated(); }
uint64_t DirectMemory::capacity() {
    return reinterpret_cast<uint64_t (*)()>(runtime_memory_resolve("sceKernelGetDirectMemorySize"))();
}
int32_t DirectMemory::allocate(int64_t low, int64_t high, uint64_t size, uint64_t alignment, int type, int64_t *out) {
    return reinterpret_cast<int32_t (*)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *)>(
        runtime_memory_resolve("sceKernelAllocateDirectMemory"))(low,high,size,alignment,type,out);
}
int32_t DirectMemory::map(void **out, uint64_t size, int prot, int flags, int64_t physical, uint64_t alignment) {
    if (physical<0 || size>capacity() || uint64_t(physical)>capacity()-size) return invalid;
    return reinterpret_cast<int32_t (*)(void **,uint64_t,int,int,int64_t,uint64_t)>(
        runtime_memory_resolve("sceKernelMapDirectMemory"))(out,size,prot,flags,physical,alignment);
}
int32_t DirectMemory::release(uint64_t address, uint64_t size) {
    if (size>capacity() || address>capacity()-size) return invalid;
    return reinterpret_cast<int32_t (*)(uint64_t,uint64_t)>(runtime_memory_resolve("sceKernelReleaseDirectMemory"))(address,size);
}
int32_t DirectMemory::unmap(void *address, uint64_t size) {
    if (!validSpan(reinterpret_cast<uintptr_t>(address),size)) return invalid;
    return reinterpret_cast<int32_t (*)(void *,uint64_t)>(runtime_memory_resolve("sceKernelMunmap"))(address,size);
}
int32_t DirectMemory::protect(void *address, uint64_t size, int prot) {
    if (!validSpan(reinterpret_cast<uintptr_t>(address),size)) return invalid;
    return reinterpret_cast<int32_t (*)(void *,uint64_t,int)>(runtime_memory_resolve("sceKernelMprotect"))(address,size,prot);
}
uint8_t *DirectMemory::pointer(uint64_t address, size_t size, bool writing) {
    return osl_aot_memory_access(address,size,writing) ? reinterpret_cast<uint8_t *>(address) : nullptr;
}
