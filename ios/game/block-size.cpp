#include <Zydis/Zydis.h>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc, char **argv) {
    if ((argc != 2 && argc != 3) || std::strlen(argv[1]) % 2)
        return 2;
    const bool window = argc == 3 && !std::strcmp(argv[2], "window");
    std::vector<unsigned char> code;
    for (size_t i = 0; argv[1][i]; i += 2) {
        unsigned value;
        if (std::sscanf(argv[1] + i, "%2x", &value) != 1)
            return 2;
        code.push_back(static_cast<unsigned char>(value));
    }
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    size_t size = 0;
    while (size < code.size()) {
        ZydisDecodedInstruction instruction;
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, code.data() + size,
                                                        code.size() - size, &instruction))) {
            if (size) {
                printf("%zu\n", size);
                return 0;
            }
            return 3;
        }
        size += instruction.length;
        switch (instruction.meta.category) {
        case ZYDIS_CATEGORY_CALL:
        case ZYDIS_CATEGORY_COND_BR:
        case ZYDIS_CATEGORY_UNCOND_BR:
        case ZYDIS_CATEGORY_RET:
        case ZYDIS_CATEGORY_INTERRUPT:
        case ZYDIS_CATEGORY_SYSCALL:
            if (!window) {
                printf("%zu\n", size);
                return 0;
            }
            break;
        default:
            break;
        }
    }
    printf("%zu\n", size);
}
