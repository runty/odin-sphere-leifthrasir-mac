#include "../gpu/shadps4/shader_recompiler/ir/passes/portable_srt.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

using namespace Shader::PortableSrt;
int main() {
    unsigned checks=0;
    auto require=[&](bool ok) { if(!ok) throw std::runtime_error("Portable SRT fixture failed"); ++checks; };
    // Two nested resource tables. The second address has the same 48-bit tag
    // representation as the guest GPU pointers. One offset comes from a UD word.
    std::array<uint32_t,16> user{};
    std::array<uint32_t,8> table{11,12,0,0,41,42,43,44};
    std::array<uint32_t,4> child{51,52,53,54};
    uint64_t address=reinterpret_cast<uintptr_t>(table.data());
    std::memcpy(user.data(),&address,8);
    address=reinterpret_cast<uintptr_t>(child.data()) | 0xabcd000000000000ULL;
    std::memcpy(table.data()+2,&address,8);
    user[4]=4;
    std::vector<Instruction> code{
        {Op::Constant,0}, {Op::PushPointer,0},
        {Op::Constant,0}, {Op::Read,2,0,0,16},
        {Op::Constant,2}, {Op::Read,4,0,0,17},
        {Op::Constant,3}, {Op::Read,6,0,0,18},
        {Op::FlatWord,4}, {Op::Read,8,0,0,19},
        {Op::PushPointer,4},
        {Op::Constant,1}, {Op::Read,11,0,0,20},
        {Op::PopPointer}, {Op::Constant,7}, {Op::Read,14,0,0,21},
        {Op::PopPointer},
    };
    const auto original_user=user;
    const auto original_table=table;
    auto encoded=Encode(code,22);
    const auto program=Decode(encoded.data(),encoded.size());
    auto read=[&](uintptr_t from,void* to,size_t size) {
        for(auto range:std::array<std::pair<uintptr_t,size_t>,3>{{
            {reinterpret_cast<uintptr_t>(user.data()),sizeof(user)},
            {reinterpret_cast<uintptr_t>(table.data()),sizeof(table)},
            {reinterpret_cast<uintptr_t>(child.data()),sizeof(child)}}})
            if(size<=range.second && from>=range.first && from-range.first<=range.second-size) {
                std::memcpy(to,reinterpret_cast<void*>(from),size);return true;
            }
        return false;
    };
    auto run=[&]() {
        std::array<uint32_t,24> flat{};
        flat[22]=0xdeadbeef;flat[23]=0xabcdef01;
        std::memcpy(flat.data(),user.data(),sizeof(user));
        Execute(program,user.data(),flat.data(),read);
        return flat;
    };
    const auto flat=run();
    require(flat[16]==11 && flat[17]==table[2] && flat[18]==table[3]);
    require(flat[19]==41 && flat[20]==52 && flat[21]==44);
    require(flat[22]==0xdeadbeef && flat[23]==0xabcdef01);
    require(user==original_user && table==original_table);
    std::atomic<unsigned> successes{0};
    std::vector<std::thread> threads;
    for(unsigned n=0;n<8;++n)threads.emplace_back([&]{
        for(unsigned i=0;i<100;++i)if(run()==flat)++successes;
    });
    for(auto& thread:threads)thread.join();
    require(successes==800);

    // A stale pointer zeros each inaccessible load without touching the source.
    std::fill(table.begin(),table.end(),0);
    std::array<uint32_t,24> zero_flat{};
    std::memcpy(zero_flat.data(),user.data(),sizeof(user));
    Execute(program,user.data(),zero_flat.data(),[](uintptr_t,void*,size_t){return false;});
    for(unsigned i=16;i<22;++i)require(zero_flat[i]==0);

    // Offset arithmetic is 32-bit, with x86's masked variable shift counts.
    std::vector<Instruction> arithmetic{
        {Op::Constant,0xfffffff0}, {Op::Constant,36}, {Op::Constant,7},
        {Op::Add,0,2}, {Op::Subtract,2,0}, {Op::Multiply,0,2},
        {Op::ShiftLeft,2,1}, {Op::ShiftRight,0,1},
        {Op::And,0,2}, {Op::Or,0,2}, {Op::Xor,0,2}, {Op::Not,0},
        {Op::Minimum,0,2}, {Op::Maximum,0,2}, {Op::Extract,0,1,2},
    };
    const std::array<uint32_t,12> wanted{
        0xfffffff7,23,0xffffff90,112,0x0fffffff,0,0xfffffff7,0xfffffff7,
        15,7,0xfffffff0,127};
    // Use each result as a table offset so the callback observes the result
    // through the actual load path, including displacement wraparound.
    for(unsigned n=0;n<wanted.size();++n){
        auto sample=arithmetic;
        sample.push_back({Op::Read,n+3,0,0,16});
        auto bytes=Encode(sample,17);auto decoded=Decode(bytes.data(),bytes.size());
        std::array<uint32_t,17> output{};
        uintptr_t observed=0;
        Execute(decoded,user.data(),output.data(),[&](uintptr_t p,void* dst,size_t size){
            observed=p;std::memset(dst,0,size);return true;
        });
        require(observed==reinterpret_cast<uintptr_t>(user.data())+uint32_t(wanted[n]<<2));
    }
    auto rejected=[&](std::vector<uint8_t> bad) {
        try { Decode(bad.data(),bad.size()); return false; }
        catch(const std::runtime_error&) { return true; }
    };
    for(size_t n=0;n<encoded.size();++n)
        require(rejected({encoded.begin(),encoded.begin()+n}));
    auto bad=encoded;bad[0]^=1;require(rejected(bad));
    bad=encoded;Header header;std::memcpy(&header,bad.data(),sizeof(header));
    header.count=max_instructions+1;std::memcpy(bad.data(),&header,sizeof(header));require(rejected(bad));
    for(Instruction invalid:std::array<Instruction,5>{{
        {Op::PopPointer},{Op::Add,0,0},{Op::FlatWord,99},
        {Op::Read,0,0,0,16},{static_cast<Op>(99)}}}) {
        bool failed=false;
        try { Encode(std::span(&invalid,1),17); }
        catch(const std::runtime_error&) { failed=true; }
        require(failed);
    }
    std::printf("Portable SRT checks=%u, concurrent walks=800: PASS\n",checks);
}
