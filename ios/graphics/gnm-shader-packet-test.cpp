#include "guest-gpu.h"
#include "../services/clock.h"
#include "image-layout.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
static unsigned checks=0;
static void require(bool ok){if(!ok){std::fprintf(stderr,"Gnm shader check %u failed\n",checks+1);std::abort();}++checks;}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    const std::string profile=argv[1],settings=profile+"/osl.ini";
    oslgpu_register_kernel();OslGpuConfig config{"Gnm shader packet checks","CUSA05083",profile.c_str(),settings.c_str(),0x03008041,0,960,540};
    require(oslgpu_init(&config)==0);
    NativeGpu gpu;Memory memory;std::array<uint8_t,1024> bytes{},protected_bytes{};
    constexpr uint64_t base=0x20000000;
    mapRegion(memory,base,bytes.data(),bytes.size());mapRegion(memory,base+0x10000,protected_bytes.data(),protected_bytes.size(),false);
    {
        const uint64_t args[6]{};uint64_t result=0xdeadbeef;
        require(!NativeGpu::call(&gpu,"unsupported",nullptr,args,0,&memory,&result) && memory.fault.empty() && result==0xdeadbeef && gpu.gnm_shader_calls==0);
    }
    for(const char *name:{"sceGnmSetPsShader","sceGnmUpdatePsShader","sceGnmSetVsShader","sceGnmUpdateVsShader","sceGnmSetCsShader"}) {
        const bool ps=std::strstr(name,"Ps")!=nullptr,vs=std::strstr(name,"Vs")!=nullptr;
        const size_t words=ps?40:vs?29:25,registers=ps?12:7;
        const char *nid=nullptr;for(const auto &entry:imports)if(!std::strcmp(entry.symbol,name)){nid=entry.nid;break;}
        require(nid!=nullptr);const auto export_address=oslgpu_resolve(nid);require(export_address!=0);
        auto native=[&](uint32_t *out,uint32_t size,const uint32_t *input,uint32_t modifier=0)->uint32_t {
            if(vs)return static_cast<uint32_t>(reinterpret_cast<int32_t (*)(uint32_t *,uint32_t,const uint32_t *,uint32_t)>(export_address)(out,size,input,modifier));
            return static_cast<uint32_t>(reinterpret_cast<int32_t (*)(uint32_t *,uint32_t,const uint32_t *)>(export_address)(out,size,input));
        };
        auto call=[&](std::array<uint64_t,6> args){memory.fault={};uint64_t result=0xdeadbeef;require(NativeGpu::call(&gpu,name,nid,args.data(),0,&memory,&result));return result;};
        auto reset=[&](){bytes.fill(0x7b);protected_bytes.fill(0x7b);};
        reset();auto before=bytes;
        require(call({UINT64_MAX,words-1,UINT64_MAX})==UINT32_MAX && memory.fault.empty() && bytes==before);
        require(call({0,words,UINT64_MAX})==UINT32_MAX && memory.fault.empty() && bytes==before);
        if(!ps)require(call({UINT64_MAX,words,0})==UINT32_MAX && memory.fault.empty() && bytes==before);
        if(vs)require(call({UINT64_MAX,words,UINT64_MAX,1})==UINT32_MAX && memory.fault.empty() && bytes==before);
        // Read/validate registers before touching a valid-size output range.
        std::array<uint32_t,12> regs{0x12345678,1,0x3510,0x27,0x143,0x28,0x17,0x41,0x52,0x63,0x74,0x85};
        std::memcpy(bytes.data()+0x201,regs.data(),registers*4);before=bytes;
        require(call({UINT64_MAX,words,base+0x201})==UINT32_MAX && memory.fault.empty() && bytes==before);
        regs[1]=0;std::memcpy(bytes.data()+0x201,regs.data(),registers*4);before=bytes;
        call({base+0x10000+1,words,base+0x201});require(!memory.fault.empty() && bytes==before && protected_bytes[1]==0x7b);
        call({base+1020,words,base+0x201});require(!memory.fault.empty() && bytes==before);
        call({UINT64_MAX,words,base+0x201});require(!memory.fault.empty() && bytes==before);
        call({base+1,words,base+1020});require(!memory.fault.empty() && bytes==before);
        call({base+1,words,UINT64_MAX});require(!memory.fault.empty() && bytes==before);
        // Read-only register mappings remain valid inputs; only output needs
        // write permission. The native call must not mutate the register view.
        reset();std::memcpy(protected_bytes.data()+1,regs.data(),registers*4);const auto protected_before=protected_bytes;
        std::array<uint32_t,42> read_only_expected;read_only_expected.fill(0x7b7b7b7b);
        require(native(read_only_expected.data()+1,words,regs.data())==0);
        require(call({base+1,words,base+0x10000+1})==0 && memory.fault.empty());
        require(protected_bytes==protected_before && !std::memcmp(bytes.data()+1,read_only_expected.data()+1,words*4));
        for(uint32_t size:{static_cast<uint32_t>(words),UINT32_MAX})for(uint32_t modifier:(vs?std::vector<uint32_t>{0,0x40u}:std::vector<uint32_t>{0})) {
            reset();std::memcpy(bytes.data()+0x201,regs.data(),registers*4);before=bytes;
            std::array<uint32_t,54> expected;expected.fill(0x7b7b7b7b);
            require(native(expected.data()+1,size,regs.data(),modifier)==0);
            require(call({base+1,size,base+0x201,modifier})==0 && memory.fault.empty());
            require(!std::memcmp(bytes.data()+1,expected.data()+1,words*4) && bytes[0]==0x7b && bytes[1+words*4]==0x7b);
            require(!std::memcmp(bytes.data()+0x201,regs.data(),registers*4));
            require(expected.front()==0x7b7b7b7b && expected[words+1]==0x7b7b7b7b);
        }
        if(ps) {
            reset();std::array<uint32_t,42> expected;expected.fill(0x7b7b7b7b);
            require(native(expected.data()+1,words,nullptr)==0);
            require(call({base+1,words,0})==0 && memory.fault.empty());
            require(!std::memcmp(bytes.data()+1,expected.data()+1,words*4) && bytes[0]==0x7b && bytes[1+words*4]==0x7b);
        }
        // Aligned-relative overlap must reproduce the backend's actual read /
        // write aliasing, including reads of registers overwritten mid-packet.
        for(int offset:{-4,0,4,int(words*4-4)}) {
            reset();const size_t out=0x101,in=size_t(int(out)+offset);
            std::memcpy(bytes.data()+in,regs.data(),registers*4);before=bytes;
            // Realign the whole byte canvas so the two native views are aligned.
            std::array<uint32_t,54> alias;alias.fill(0x7b7b7b7b);
            const size_t start=std::min(out,in);std::memcpy(alias.data(),bytes.data()+start,std::max(out+words*4,in+registers*4)-start);
            require(native(alias.data()+(out-start)/4,words,alias.data()+(in-start)/4)==0);
            require(call({base+out,words,base+in})==0 && memory.fault.empty());
            auto wanted=before;std::memcpy(wanted.data()+out,alias.data()+(out-start)/4,words*4);
            require(bytes==wanted);
        }
        reset();std::memcpy(bytes.data()+0x102,regs.data(),registers*4);before=bytes;
        call({base+0x101,words,base+0x102});require(memory.fault=="Gnm shader packet aliases have incompatible alignment" && bytes==before);
    }
    require(oslgpu_shutdown_main_thread()==0 && !SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    std::printf("{\"gnm_shader_packet_checks\":%u,\"native_packet_comparisons\":true,\"game_executed\":false,\"game_frames\":false,\"failed\":0}\n",checks);
}
