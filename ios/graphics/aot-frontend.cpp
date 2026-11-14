#include "../services/clock.h"
// Real AOT guest + native arm64 renderer. This bounded integration executable
// deliberately supports only the VideoOut calls whose marshalling is defined.
#include "../services/gpu.h"
#include "oslgpu.h"
#include "image-layout.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <array>
#include "core/libraries/kernel/equeue.h"
#include "core/libraries/videoout/buffer.h"
#include "video_core/texture_cache/image_info.h"

extern "C" uint64_t osl_aot_memory_allocated();
extern "C" uint64_t runtime_eventflag_live_count();
extern "C" int runtime_memory_region(uintptr_t,uintptr_t *,uintptr_t *,int *);
extern "C" int runtime_memory_is_mapped(uintptr_t,uint64_t);
static std::atomic<bool> checked{false};
static void gpu_fault(int number,siginfo_t *info,void *context) {
    if (info && reinterpret_cast<uintptr_t>(info->si_addr)<(uint64_t(1)<<40) &&
        oslgpu_handle_fault(context,info->si_addr)) return;
    static constexpr char message[]="AOT renderer: unhandled native memory fault\n";
    write(STDERR_FILENO,message,sizeof(message)-1);
    _exit(128+number);
}
extern "C" uint64_t runtime_process_time_counter() { return MobileClock::processCounter(); }
extern "C" uint64_t runtime_process_time_us() { return MobileClock::processTime(); }
extern "C" uint64_t runtime_tsc_frequency() { return MobileClock::frequency; }
extern "C" uint64_t runtime_tsc_counter() { return MobileClock::tsc(); }
extern "C" void runtime_log_flush() { std::fflush(nullptr); }
extern "C" [[noreturn]] void runtime_exit_clean() { runtime_log_flush(); std::_Exit(checked ? 0 : 1); }

struct NativeGpu {
    int32_t handle=0;
    unsigned opened=0,closed=0;
    uint64_t observed_mapping=0;
    bool mapped_before_reset=false;
    std::array<int64_t,64> queues{};
    unsigned queues_created=0,queues_deleted=0,events_added=0,attributes_set=0,buffers_registered=0;
    static bool format32(uint64_t value) {
        return value==0x80000000u || value==0x80002200u || value==0x88060000u || value==0x88000000u || value==0x88740000u;
    }
    static bool call(void *opaque,const char *symbol,const char *nid,const uint64_t args[6],uint64_t stack,Memory *memory,uint64_t *result) {
        auto &self=*static_cast<NativeGpu *>(opaque);
        using namespace Libraries::VideoOut;
        if (!std::strcmp(symbol,"sceVideoOutSetBufferAttribute")) {
            auto output=guestPointer(*memory,args[0],sizeof(BufferAttribute),true);if(!output)return true;
            if(stack>UINT64_MAX-8){stopGuest(*memory,"invalid VideoOut stack argument");return true;}
            auto input=guestPointer(*memory,stack+8,8);if(!input)return true;
            uint64_t pitch=0;std::memcpy(&pitch,input,8);
            if(!format32(static_cast<uint32_t>(args[1]))){stopGuest(*memory,"unsupported VideoOut pixel format");return true;}
            auto set=reinterpret_cast<void (*)(BufferAttribute *,PixelFormat,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(oslgpu_resolve(nid));
            if(!set){stopGuest(*memory,"native VideoOut attribute export is unavailable");return true;}
            BufferAttribute attribute{};
            set(&attribute,static_cast<PixelFormat>(args[1]),args[2],args[3],args[4],args[5],pitch);
            std::memcpy(output,&attribute,sizeof(attribute));*result=0;++self.attributes_set;return true;
        }
        if (!std::strcmp(symbol,"sceVideoOutRegisterBuffers")) {
            const int32_t start=static_cast<int32_t>(args[1]),count=static_cast<int32_t>(args[3]);
            if(!self.handle || static_cast<int32_t>(args[0])!=self.handle){*result=0x8029000bu;return true;}
            if(start<0 || count<1 || count>16 || start>16-count){*result=0x80290001u;return true;}
            auto attributes=guestPointer(*memory,args[4],sizeof(BufferAttribute));if(!attributes)return true;
            BufferAttributeGroup group{};std::memcpy(&group.attrib,attributes,sizeof(group.attrib));
            const auto &a=group.attrib;
            if(!format32(static_cast<uint32_t>(a.pixel_format)) || !a.width || !a.height || a.width>16384 ||
               a.height>16384 || a.pitch_in_pixel<a.width || a.pitch_in_pixel>16384 || a.aspect_ratio ||
               a.reserved0 || a.reserved1 || (a.tiling_mode!=TilingMode::Tile && a.tiling_mode!=TilingMode::Linear)) {
                *result=0x80290001u;return true;
            }
            const uint32_t native_pitch=a.tiling_mode==TilingMode::Linear ? a.width : (a.width+127)&~127u;
            if(a.pitch_in_pixel!=native_pitch){stopGuest(*memory,"VideoOut pitch differs from backend layout");return true;}
            auto addresses=guestPointer(*memory,args[2],count*8);if(!addresses)return true;
            std::array<void *,16> native{};
            for(int32_t i=0;i<count;++i) {
                uint64_t address=0;std::memcpy(&address,addresses+i*8,8);
                VideoCore::ImageInfo info(group,address);
                if(address>=(uint64_t(1)<<40) || !info.guest_size || info.guest_size>(uint64_t(1)<<40)-address ||
                   !runtime_memory_is_mapped(address,info.guest_size) || !oslgpu_guest_range_mapped(address,info.guest_size)) {
                    stopGuest(*memory,"VideoOut buffer is outside shared GPU mappings");return true;
                }
                native[i]=reinterpret_cast<void *>(address);
            }
            auto reg=reinterpret_cast<int32_t (*)(int32_t,int32_t,void *const *,int32_t,const BufferAttribute *)>(oslgpu_resolve(nid));
            if(!reg){stopGuest(*memory,"native VideoOut buffer export is unavailable");return true;}
            const int32_t status=reg(self.handle,start,native.data(),count,&group.attrib);
            *result=static_cast<uint32_t>(status);if(status>=0)self.buffers_registered+=count;return true;
        }
        if (!std::strcmp(symbol,"sceKernelCreateEqueue")) {
            if (!args[0] || !args[1]) { *result=0x80020016u;return true; }
            auto output=guestPointer(*memory,args[0],8,true);if(!output)return true;
            char name[33]{}; bool terminated=false;
            for(unsigned i=0;i<33;++i) {
                auto byte=guestPointer(*memory,args[1]+i,1);if(!byte)return true;
                if(i==32 && *byte) { *result=0x8002003fu;return true; }
                name[i]=static_cast<char>(*byte);if(!*byte){terminated=true;break;}
            }
            if(!terminated || self.queues_created==self.queues.size()) { *result=0x8002000cu;return true; }
            auto create=reinterpret_cast<int32_t (*)(int64_t *,const char *)>(oslgpu_resolve(nid));
            if(!create){stopGuest(*memory,"native event queue export is unavailable");return true;}
            int64_t queue=0;*result=static_cast<uint32_t>(create(&queue,name));
            if(!*result) {self.queues[self.queues_created++]=queue;std::memcpy(output,&queue,8);}
            return true;
        }
        if (!std::strcmp(symbol,"sceVideoOutAddFlipEvent") || !std::strcmp(symbol,"sceVideoOutAddVblankEvent")) {
            bool owned=false;
            for(unsigned i=0;i<self.queues_created;++i) owned|=self.queues[i]==static_cast<int64_t>(args[0]);
            if(!owned){*result=0x8029000cu;return true;}
            if(!self.handle || static_cast<int32_t>(args[1])!=self.handle){*result=0x8029000bu;return true;}
            auto add=reinterpret_cast<int32_t (*)(int64_t,int32_t,void *)>(oslgpu_resolve(nid));
            if(!add){stopGuest(*memory,"native VideoOut event export is unavailable");return true;}
            *result=static_cast<uint32_t>(add(static_cast<int64_t>(args[0]),self.handle,reinterpret_cast<void *>(args[2])));
            if(!*result)++self.events_added;
            return true;
        }
        if (std::strcmp(symbol,"sceVideoOutOpen")) return false;
        if (static_cast<int32_t>(args[1])!=0 || static_cast<int32_t>(args[2])!=0) { *result=0x80290001u; return true; }
        if (args[3]) { stopGuest(*memory,"VideoOut service-thread parameters are unsupported"); return true; }
        auto open=reinterpret_cast<int32_t (*)(int32_t,int32_t,int32_t,const void *)>(oslgpu_resolve(nid));
        if (!open) { stopGuest(*memory,"native VideoOut export is unavailable"); return true; }
        const int32_t handle=open(static_cast<int32_t>(args[0]),0,0,nullptr);
        *result=static_cast<uint32_t>(handle);
        if (handle>0) { self.handle=handle; ++self.opened; }
        return true;
    }
    static void finish(void *opaque) {
        auto &self=*static_cast<NativeGpu *>(opaque);
        if (self.handle) {
            oslgpu_finish();
            auto close=reinterpret_cast<int32_t (*)(int32_t)>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
            if (!close || close(self.handle)) std::abort();
            self.handle=0; ++self.closed;
        }
        auto remove=reinterpret_cast<int32_t (*)(int64_t)>(oslgpu_resolve("jpFjmgAC5AE#libkernel"));
        for(unsigned i=0;i<self.queues_created;++i) {
            if(!remove || remove(self.queues[i]) || Libraries::Kernel::GetEqueue(self.queues[i]))std::abort();
            ++self.queues_deleted;
        }
        uintptr_t cursor=0,start=0,end=0; int mapped=0;
        for(unsigned i=0;i<4096 && runtime_memory_region(cursor,&start,&end,&mapped);++i) {
            if (mapped) {
                self.observed_mapping=start;
                self.mapped_before_reset=oslgpu_guest_range_mapped(start,1);
                break;
            }
            if(end<=cursor) break;
            cursor=end;
        }
    }
};

#include "bridge-checks.h"

int main(int argc,char **argv) {
    if(!pthread_main_np() || (argc!=3 && argc!=4) || !std::filesystem::is_directory(argv[2])) return 2;
    const bool check_bridge=argc==4 && !std::strcmp(argv[3],"--check-bridge");
    const bool discovery_only=argc==4 && !std::strcmp(argv[3],"--discover");
    if(argc==4 && !check_bridge && !discovery_only)return 2;
    // Use the actual prepared image's procparam SDK, matching the working Mac
    // loader; do not substitute a newer SDK's initialization packets.
    uint64_t sdk=0;
    std::ifstream file(argv[1],std::ios::binary);
    file.seekg(procparam+16); file.read(reinterpret_cast<char *>(&sdk),8);
    if(!file || sdk>UINT32_MAX) return 3;
    struct sigaction action{};action.sa_sigaction=gpu_fault;action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGSEGV,&action,nullptr) || sigaction(SIGBUS,&action,nullptr)) return 5;
    const std::string profile=argv[2],settings=profile+"/osl.ini",image=argv[1];
    OslGpuConfig config{"Odin Sphere AOT Renderer","CUSA05083",profile.c_str(),settings.c_str(),
                        static_cast<uint32_t>(sdk),0,960,540};
    oslgpu_register_kernel();
    if(oslgpu_init(&config)) return 4;
    NativeGpu context;
    MobileGpuServices services{&context,NativeGpu::call,NativeGpu::finish};
    std::thread guest([&] {
        if(check_bridge) {
            const unsigned count=bridge_checks();checked=true;
            std::printf("OSL_GPU_BRIDGE_CHECKS:{\"checks\":%u,\"failed\":0,\"game_executed\":false,\"game_frames\":false}\n",count);
            runtime_log_flush();SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);return;
        }
        const std::string report=osl_mobile_game_report_with_gpu(image.c_str(),&services);
        std::printf("OSL_AOT_GPU_GUEST:%s\n",report.c_str());
        const bool unmapped=context.observed_mapping && !oslgpu_guest_range_mapped(context.observed_mapping,1);
        const bool released=osl_aot_memory_allocated()==0 && runtime_eventflag_live_count()==0;
        const bool clean=context.closed==context.opened && context.queues_created==context.queues_deleted && released &&
                         (!context.observed_mapping || (context.mapped_before_reset && unmapped));
        // Discovery must report real early CPU stops before VideoOut exists.
        // Normal qualification still requires a VideoOut and tracked mapping.
        checked=clean && (discovery_only || (context.opened>0 && context.mapped_before_reset && unmapped));
        std::printf("OSL_AOT_GPU_RESULT:{\"opened\":%u,\"closed\":%u,\"mapped_before_reset\":%s,"
                    "\"unmapped_after_reset\":%s,\"guest_resources_released\":%s,\"sdk\":%u,"
                    "\"presented_frames\":%llu,\"queues_created\":%u,\"queues_deleted\":%u,\"events_added\":%u,"
                    "\"attributes_set\":%u,\"buffers_registered\":%u,\"game_frames\":false,\"discovery_only\":%s}\n",context.opened,context.closed,
                    context.mapped_before_reset?"true":"false",unmapped?"true":"false",released?"true":"false",
                    static_cast<uint32_t>(sdk),(unsigned long long)oslgpu_presented_frames(),context.queues_created,context.queues_deleted,context.events_added,context.attributes_set,context.buffers_registered,discovery_only?"true":"false");
        runtime_log_flush();
        SDL_Event quit{}; quit.type=SDL_EVENT_QUIT; SDL_PushEvent(&quit);
    });
    oslgpu_run_main_loop();
}
