#include "../services/clock.h"
// Native renderer initialization only: no guest execution, synthetic success or game frames.
#include "oslgpu.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <time.h>
#include <pthread.h>

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
int main(int argc,char **argv) {
    if(!pthread_main_np()) return 6;
    if(argc!=2 || !std::filesystem::is_directory(argv[1])) return 2;
    const std::string profile=argv[1], settings=profile+"/osl.ini";
    OslGpuConfig config{"Odin Sphere arm64 Renderer Probe","ODIN_ARM64_PROBE",profile.c_str(),settings.c_str(),
                        0x04508000,0,640,360};
    oslgpu_register_kernel();
    if(oslgpu_init(&config)) return 3;
    using Open=int32_t (*)(int32_t,int32_t,int32_t,const void *);
    using Close=int32_t (*)(int32_t);
    auto open=reinterpret_cast<Open>(oslgpu_resolve("Up36PTk687E#libSceVideoOut"));
    auto close=reinterpret_cast<Close>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
    if(!open || !close) return 4;
    const int32_t handle=open(1,0,0,nullptr);
    if(handle<=0 || close(handle)!=0) return 5;
    std::printf("Native arm64 renderer initialized, VideoOut opened and closed; %u symbols\n",oslgpu_symbol_count());
    checked=true;
    std::thread([] {
        SDL_Delay(300);
        SDL_Event quit{}; quit.type=SDL_EVENT_QUIT; SDL_PushEvent(&quit);
    }).detach();
    oslgpu_run_main_loop();
}
