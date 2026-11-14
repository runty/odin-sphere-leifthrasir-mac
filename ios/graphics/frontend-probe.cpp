#include "../services/clock.h"
// Native renderer initialization only: no guest execution, synthetic success or game frames.
#include "oslgpu.h"
#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "video_core/cache_storage.h"
#include <thread>
#include <time.h>
#include <pthread.h>
#include <mach/mach.h>
#include <cstring>

static std::atomic<bool> checked{false};
extern "C" uint64_t runtime_process_time_counter() { return MobileClock::processCounter(); }
extern "C" uint64_t runtime_process_time_us() { return MobileClock::processTime(); }
extern "C" uint64_t runtime_tsc_frequency() { return MobileClock::frequency; }
extern "C" uint64_t runtime_tsc_counter() { return MobileClock::tsc(); }
extern "C" void runtime_log_flush() { std::fflush(nullptr); }
extern "C" [[noreturn]] void runtime_exit_clean() {
    std::printf("OSL_NATIVE_GPU_RESULT:{\"initialized\":%s,\"presented_frames\":%llu,\"game_frames\":false}\n",
                checked ? "true" : "false", (unsigned long long)oslgpu_presented_frames());
    runtime_log_flush();
    std::_Exit(checked ? 0 : 1);
}
static unsigned sessionWorkerCount() {
    thread_act_array_t threads=nullptr;mach_msg_type_number_t count=0;
    if(task_threads(mach_task_self(),&threads,&count)!=KERN_SUCCESS)return UINT32_MAX;
    unsigned live=0;
    for(unsigned i=0;i<count;++i) {
        char name[64]{};
        if(auto thread=pthread_from_mach_thread_np(threads[i])) {
            pthread_getname_np(thread,name,sizeof(name));
            live+=!std::strcmp(name,"osl:kernel_service") || !std::strcmp(name,"osl:present") ||
                  !std::strcmp(name,"shadPS4:PresentThread") || !std::strcmp(name,"shadPS4:GpuCommandProcessor") ||
                  !std::strcmp(name,"shadPS4:PipelineCacheIO") || !std::strcmp(name,"osl:DrawRec") || !std::strncmp(name,"osl:Shader",10);
        }
        mach_port_deallocate(mach_task_self(),threads[i]);
    }
    vm_deallocate(mach_task_self(),reinterpret_cast<vm_address_t>(threads),count*sizeof(thread_t));
    return live;
}
int main(int argc,char **argv) {
    if(!pthread_main_np()) return 6;
    if((argc!=2 && argc!=3) || !std::filesystem::is_directory(argv[1])) return 2;
    const unsigned cycles=argc==3 ? static_cast<unsigned>(std::strtoul(argv[2],nullptr,10)) : 2;
    if(!cycles || cycles>10)return 2;
    SDL_SetMainReady();
    const std::string profile=argv[1], settings=profile+"/osl.ini";
    OslGpuConfig config{"Odin Sphere arm64 Renderer Probe","ODIN_ARM64_PROBE",profile.c_str(),settings.c_str(),
                        0x04508000,0,640,360};
    unsigned completed=0;
    for(unsigned cycle=0;cycle<cycles;++cycle) {
        oslgpu_register_kernel();
        if(oslgpu_init(&config)) return 3;
        if(oslgpu_init(&config)!=-1)return 7; // Cannot replace an active session.
        using Open=int32_t (*)(int32_t,int32_t,int32_t,const void *);
        using Close=int32_t (*)(int32_t);
        auto open=reinterpret_cast<Open>(oslgpu_resolve("Up36PTk687E#libSceVideoOut"));
        auto close=reinterpret_cast<Close>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
        if(!open || !close) return 4;
        const int32_t handle=open(1,0,0,nullptr);
        if(handle<=0 || close(handle)!=0) return 5;
        int wrong_poll=0,wrong_shutdown=0;
        std::thread wrong([&]{wrong_poll=oslgpu_poll_main_thread();wrong_shutdown=oslgpu_shutdown_main_thread();});wrong.join();
        if(wrong_poll!=-1 || wrong_shutdown!=-1)return 8;
        for(unsigned i=0;i<30;++i) {
            if(oslgpu_poll_main_thread()!=1)return 9;
            SDL_Delay(10); // CLI timing only; UIKit uses its display link.
        }
        auto &cache=Storage::DataBase::Instance();
        const auto folder=cache.Folder();
        if(folder.empty() || !std::filesystem::is_directory(folder))return 13;
        for(unsigned i=0;i<64;++i) {
            std::vector<u8> data(4096,static_cast<u8>(cycle*64+i));
            if(!cache.Save(Storage::BlobType::ShaderProfile,"lifecycle-close-"+std::to_string(cycle)+"-"+std::to_string(i),std::move(data)))return 14;
        }
        if(oslgpu_shutdown_main_thread()!=0 || oslgpu_poll_main_thread()!=0 ||
           oslgpu_guest_range_mapped(0x10000000,4096) || SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD))return 10;
        if(oslgpu_shutdown_main_thread()!=0)return 11; // Idempotent after stop.
        if(sessionWorkerCount()!=0)return 12;
        for(unsigned i=0;i<64;++i) {
            const auto file=folder/("lifecycle-close-"+std::to_string(cycle)+"-"+std::to_string(i)+".bin");
            std::ifstream input(file,std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(input)),{});
            if(input.bad() || data.size()!=4096)return 15;
            for(auto value:data)if(static_cast<u8>(value)!=static_cast<u8>(cycle*64+i))return 16;
        }
        ++completed;
    }
    checked=true;
    std::printf("OSL_NATIVE_GPU_RESULT:{\"initialized\":true,\"cycles\":%u,\"shutdown_checks\":%u,\"wrong_thread_rejected\":true,\"guest_mapping_hook_cleared\":true,\"sdl_video_gamepad_released\":true,\"session_workers_remaining\":0,\"cache_writes_verified\":%u,\"presented_frames\":%llu,\"game_frames\":false}\n",
                completed,completed,completed*64,(unsigned long long)oslgpu_presented_frames());
    runtime_log_flush();
    return 0;
}
