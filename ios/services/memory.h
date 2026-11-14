#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>

// Serial ownership of the existing native memory manager. Guest worker threads
// share checked mappings; reset runs after guest workers and GPU work quiesce.
class DirectMemory {
    std::unique_lock<std::mutex> lease;
public:
    DirectMemory();
    ~DirectMemory();
    void reset(); // Called after the renderer quiesces, before guest buffers are freed.
    DirectMemory(const DirectMemory &) = delete;
    DirectMemory &operator=(const DirectMemory &) = delete;
    static uint64_t capacity();
    static uint64_t allocated();
    static uint64_t windowSize();
    static uint64_t backingViewSize();
    static int32_t allocate(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
    static int32_t map(void **, uint64_t, int, int, int64_t, uint64_t);
    static int32_t release(uint64_t, uint64_t);
    static int32_t unmap(void *, uint64_t);
    static int32_t protect(void *, uint64_t, int);
    static uint8_t *pointer(uint64_t, size_t, bool);
};
