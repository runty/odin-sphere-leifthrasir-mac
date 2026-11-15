#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/program.h"
#include <array>
#include <cstdio>
#include <setjmp.h>
#include "common/arch.h"
#ifdef ARCH_X86_64
extern "C" { __thread sigjmp_buf* runtime_fault_recover=nullptr; }
#endif

int main() {
    using namespace Shader;
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    std::array<u32,16> user{};
    std::array<u32,8> table{11,12,0,0,41,42,43,44};
    std::array<u32,4> child{51,52,53,54};
    u64 pointer=reinterpret_cast<uintptr_t>(table.data());
    std::memcpy(user.data(),&pointer,8);
    pointer=reinterpret_cast<uintptr_t>(child.data()) | 0xabcd000000000000ULL;
    std::memcpy(table.data()+2,&pointer,8);
    user[4]=4;
    Info info{};
    info.user_data=user;
    IR::Program program(info);
    program.blocks.push_back(&block);
    program.post_order_blocks.push_back(&block);
    IR::IREmitter ir(block);
    const auto root=ir.CompositeConstruct(ir.GetUserData(IR::ScalarReg::S0),
                                         ir.GetUserData(IR::ScalarReg::S1));
    const auto first=ir.ReadConst(root,ir.Imm32(0u));
    const auto duplicate=ir.ReadConst(root,ir.Imm32(0u));
    const auto low=ir.ReadConst(root,ir.Imm32(2u));
    const auto high=ir.ReadConst(root,ir.Imm32(3u));
    const auto nested=ir.CompositeConstruct(low,high);
    const auto last=ir.ReadConst(nested,ir.Imm32(1u));
    const auto dynamic=ir.ReadConst(root,ir.GetUserData(IR::ScalarReg::S4));
    Optimization::FlattenExtendedUserdataPass(program);
    auto check=[&](const IR::U32& value,u32 expected) {
        const u16 offset=value.Inst()->Flags<u16>();
        if (offset<16 || offset>=info.flattened_ud_buf.size() ||
            info.flattened_ud_buf[offset]!=expected)
            throw std::runtime_error("SRT pass produced an incorrect flattened resource");
    };
    check(first,11);check(duplicate,11);check(last,52);check(dynamic,41);
    if (first.Inst()->Flags<u16>()!=duplicate.Inst()->Flags<u16>())
        throw std::runtime_error("SRT duplicate offsets differ");
    table[0]=71;child[1]=72;user[4]=7;
    info.RefreshFlatBuf();
    check(first,71);check(duplicate,71);check(last,72);check(dynamic,44);
    auto copy=info.srt_info;
    copy.walker_code=RegisterWalkerCode(info.srt_info.walker_code,info.srt_info.walker_code_size);
    std::vector<u32> flat(copy.flattened_bufsize_dw);
    std::memcpy(flat.data(),user.data(),sizeof(user));
    copy.Walk(user.data(),flat.data());
    if(flat!=info.flattened_ud_buf)throw std::runtime_error("Reloaded SRT program differs");
#ifndef ARCH_X86_64
    ++copy.flattened_bufsize_dw;
    bool rejected=false;
    try { copy.Walk(user.data(),flat.data()); }
    catch(const std::runtime_error&) { rejected=true; }
    if(!rejected)throw std::runtime_error("SRT cached buffer extent mismatch was accepted");
#endif
    std::printf("SRT IR pass, nested/dynamic tables, duplicate offsets, refresh and cache code reload: PASS\n");
}
