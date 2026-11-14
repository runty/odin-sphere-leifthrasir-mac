#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

// Serial ownership of the existing native memory manager. Guest worker threads
// are not enabled by this startup adapter; reset runs only after guest execution.
class DirectMemory {
    std::unique_lock<std::mutex> lease;
public:
    DirectMemory();
    ~DirectMemory();
    DirectMemory(const DirectMemory &) = delete;
    DirectMemory &operator=(const DirectMemory &) = delete;
    static uint64_t capacity();
    static uint64_t allocated();
    static int32_t allocate(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
    static int32_t map(void **, uint64_t, int, int, int64_t, uint64_t);
    static int32_t release(uint64_t, uint64_t);
    static int32_t unmap(void *, uint64_t);
    static int32_t protect(void *, uint64_t, int);
    static uint8_t *pointer(uint64_t, size_t, bool);
};
