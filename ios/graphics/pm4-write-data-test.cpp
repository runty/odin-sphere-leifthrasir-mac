#include <span>
#include "video_core/amdgpu/pm4_cmds.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(){
    alignas(8) std::array<uint32_t,8> words;words.fill(0x7b7b7b7b);
    auto *packet=reinterpret_cast<AmdGpu::PM4CmdWriteData *>(words.data()+1);
    packet->header=AmdGpu::PM4Type3Header{AmdGpu::PM4ItOpcode::WriteData,3};
    packet->raw=0x500;packet->Address<uint64_t>(0x123456789abcdef0ull);packet->data[0]=0x0badbeef;
    constexpr std::array<uint32_t,8> expected{0x7b7b7b7b,0xc0033700,0x500,0x9abcdef0,0x12345678,0x0badbeef,0x7b7b7b7b,0x7b7b7b7b};
    if(std::memcmp(words.data(),expected.data(),sizeof(words)) || packet->Address<uint64_t>()!=0x123456789abcdef0ull || packet->Size()!=4)std::abort();
    std::puts("{\"pm4_write_data_layout\":true,\"four_byte_packet_alignment\":true,\"serialized_bytes_preserved\":true,\"canaries_preserved\":true,\"failed\":0,\"game_frames\":false}");
}
