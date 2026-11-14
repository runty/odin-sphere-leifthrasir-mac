#pragma once
#include "../services/gpu.h"
#include "oslgpu.h"
#include <array>
#include <algorithm>
#include <cstring>

// Packet writers only: all native pointers refer to aligned, call-local
// storage. No command buffer is submitted and no pointer survives this call.
// Footprints/validation follow the vendored gnmdriver.cpp implementations:
// PS 40 dwords / 12 registers, VS 29 / 7, CS 25 / 7. Size is capacity, not
// the number of dwords touched, and trailing NOP payload can remain unwritten.
inline bool nativeGnmShaderPacket(const char *symbol,const char *nid,const uint64_t args[6],Memory &memory,uint64_t &result) {
    enum class Shader { Pixel,Vertex,Compute } shader;
    size_t words,registers;
    if(!std::strcmp(symbol,"sceGnmSetPsShader") || !std::strcmp(symbol,"sceGnmUpdatePsShader")) {
        shader=Shader::Pixel;words=40;registers=12;
    } else if(!std::strcmp(symbol,"sceGnmSetVsShader") || !std::strcmp(symbol,"sceGnmUpdateVsShader")) {
        shader=Shader::Vertex;words=29;registers=7;
    } else if(!std::strcmp(symbol,"sceGnmSetCsShader")) {
        shader=Shader::Compute;words=25;registers=7;
    } else return false;
    const auto address=oslgpu_resolve(nid);
    if(!address){stopGuest(memory,"native Gnm shader export is unavailable");return true;}
    const uint32_t size=static_cast<uint32_t>(args[1]),modifier=static_cast<uint32_t>(args[3]);
    auto invoke=[&](uint32_t *output,const uint32_t *input)->int32_t {
        if(shader==Shader::Vertex)
            return reinterpret_cast<int32_t (*)(uint32_t *,uint32_t,const uint32_t *,uint32_t)>(address)(output,size,input,modifier);
        return reinterpret_cast<int32_t (*)(uint32_t *,uint32_t,const uint32_t *)>(address)(output,size,input);
    };
    std::array<uint32_t,52> scratch{};
    std::array<uint32_t,12> input{};
    auto returned=[&](int32_t status){result=static_cast<uint32_t>(status);};
    // Keep native validation priority. Invalid sizes/null outputs do not read
    // either guest range; VS modifier/null-register errors do not touch output.
    if(!args[0] || size<words) {returned(invoke(nullptr,nullptr));return true;}
    if(shader!=Shader::Pixel && !args[2]) {returned(invoke(scratch.data(),nullptr));return true;}
    if(shader==Shader::Vertex && (modifier&0xfcfffc3fu)) {
        returned(invoke(scratch.data(),input.data()));return true;
    }
    const uint8_t *source=nullptr;
    if(args[2]) {
        source=guestPointer(memory,args[2],registers*4);if(!source)return true;
        std::memcpy(input.data(),source,registers*4);
        if(input[1]) {returned(invoke(scratch.data(),input.data()));return true;}
    }
    auto output=guestPointer(memory,args[0],words*4,true);if(!output)return true;
    uint32_t *native_output=scratch.data();const uint32_t *native_input=source?input.data():nullptr;
    const bool overlaps=source && args[0]<args[2]+registers*4 && args[2]<args[0]+words*4;
    if(overlaps) {
        const auto start=std::min(args[0],args[2]);
        const auto out_offset=args[0]-start,in_offset=args[2]-start;
        // Equal byte misalignment is fine: the scratch base realigns both.
        // Different alignments cannot be represented by two aligned u32 views.
        if((out_offset|in_offset)&3) {stopGuest(memory,"Gnm shader packet aliases have incompatible alignment");return true;}
        auto bytes=reinterpret_cast<uint8_t *>(scratch.data());
        std::memcpy(bytes+out_offset,output,words*4);
        std::memcpy(bytes+in_offset,source,registers*4);
        native_output=scratch.data()+out_offset/4;native_input=scratch.data()+in_offset/4;
    } else std::memcpy(scratch.data(),output,words*4);
    const int32_t status=invoke(native_output,native_input);
    if(!status)std::memcpy(output,native_output,words*4);
    returned(status);return true;
}
