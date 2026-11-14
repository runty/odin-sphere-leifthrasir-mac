// Run only the three bounded, read-reviewed routines from the player's image.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <sys/mman.h>
#include "fixtures.h"
struct Result { uint64_t value, flags; };
extern "C" Result osl_invoke(void *, uint64_t);
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), {});
    if (image.size() != image_size) return 3;
    auto code = static_cast<uint8_t *>(mmap(nullptr, image.size(), PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0));
    if (code == MAP_FAILED) return 4;
    std::memcpy(code, image.data(), image.size());
    for (const auto &t : fixture_cases) {
        std::vector<uint8_t> data(8192, 0x55);
        uint64_t arg = t.a;
        if (t.kind == 0) {
            std::memset(data.data(), 0, data.size());
            auto nested = t.a == 0 ? uint64_t(0) : reinterpret_cast<uint64_t>(data.data() + 1024);
            std::memcpy(data.data() + 0x1d8, &nested, 8);
            uint32_t flag = t.a == 2 ? 1 : 0;
            std::memcpy(data.data() + 1024 + 0xa0, &flag, 4);
            arg = reinterpret_cast<uint64_t>(data.data());
        } else if (t.kind == 1) {
            data[t.a + t.b] = 0;
            arg = reinterpret_cast<uint64_t>(data.data() + t.a);
        }
        auto result = osl_invoke(code + t.offset, arg);
        uint64_t observed = 0;
        if (t.kind == 2) std::memcpy(&observed, code + fios_storage, 4);
        printf("%u %llu %llu %llu\n", t.kind, (unsigned long long)result.value,
               (unsigned long long)(result.flags & 0x8c5), (unsigned long long)observed);
    }
    munmap(code, image.size());
}
