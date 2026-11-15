#include "../services/guest_audio.h"
#include "../services/clock.h"
// Real AOT guest + native arm64 renderer. This bounded integration executable
// deliberately supports only the VideoOut calls whose marshalling is defined.
#include "../services/gpu.h"
#include "oslgpu.h"
#include "save-message-presenter.h"
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
#include <charconv>

extern "C" uint64_t osl_aot_memory_allocated();
extern "C" uint64_t runtime_eventflag_live_count();
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

#include "guest-gpu.h"

#include "bridge-checks.h"

int main(int argc,char **argv) {
    if(!pthread_main_np() || argc<3 || argc>5 || !std::filesystem::is_directory(argv[2])) return 2;
    uint32_t budget_ms=2000;
    const bool timed=argc==5 && !std::strcmp(argv[3],"--run-ms");
    if(timed) {
        const auto end=argv[4]+std::strlen(argv[4]);
        const auto parsed=std::from_chars(argv[4],end,budget_ms);
        if(parsed.ec!=std::errc{} || parsed.ptr!=end || budget_ms<100 || budget_ms>600000) return 2;
    }
    const bool check_bridge=argc==4 && !std::strcmp(argv[3],"--check-bridge");
    const bool discovery_only=timed || (argc==4 && !std::strcmp(argv[3],"--discover"));
    if((argc==4 && !check_bridge && !discovery_only) || (argc==5 && !timed))return 2;
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
    GuestAudio audio;
    MobileGpuServices services{&context,NativeGpu::call,NativeGpu::finish};
    services.execution_budget_ms=budget_ms;
    services.frame_count=[](void *){return oslgpu_presented_frames();};
    services.save_message=nativeSaveMessagePresenter();
    services.audio_context=&audio;services.audio_call=GuestAudio::call;
    std::thread guest([&] {
        if(check_bridge) {
            const unsigned count=bridge_checks();checked=true;
            std::printf("OSL_GPU_BRIDGE_CHECKS:{\"checks\":%u,\"failed\":0,\"game_executed\":false,\"game_frames\":false}\n",count);
            runtime_log_flush();SDL_Event quit{};quit.type=SDL_EVENT_QUIT;SDL_PushEvent(&quit);return;
        }
        const std::string report=osl_mobile_game_report_with_gpu(image.c_str(),&services);
        audio.finish();
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
                    "\"attributes_set\":%u,\"buffers_registered\":%u,\"gnm_init_calls\":%llu,\"gnm_init_words\":%llu,\"gnm_shader_calls\":%llu,\"gnm_submission_calls\":%llu,\"gnm_batches\":%llu,\"gnm_buffers\":%llu,\"gnm_bytes\":%llu,\"gnm_submit_done\":%llu,\"flip_wait_packets\":%llu,\"vblank_waits\":%llu,\"presentation_interruptions\":%llu,\"gnm_performance_queries\":%llu,\"game_frames\":false,\"discovery_only\":%s}\n",context.opened,context.closed,
                    context.mapped_before_reset?"true":"false",unmapped?"true":"false",released?"true":"false",
                    static_cast<uint32_t>(sdk),(unsigned long long)oslgpu_presented_frames(),context.queues_created,context.queues_deleted,context.events_added,context.attributes_set,context.buffers_registered,(unsigned long long)context.gnm_init_calls,(unsigned long long)context.gnm_init_words,(unsigned long long)context.gnm_shader_calls,(unsigned long long)context.submissions.calls,(unsigned long long)context.submissions.batches,(unsigned long long)context.submissions.buffers,(unsigned long long)context.submissions.bytes,(unsigned long long)context.submissions.submit_done,(unsigned long long)context.presentation.flip_wait_packets,(unsigned long long)context.presentation.vblank_waits,(unsigned long long)context.presentation.interruptions,(unsigned long long)context.presentation.performance_queries,discovery_only?"true":"false");
        runtime_log_flush();
        SDL_Event quit{}; quit.type=SDL_EVENT_QUIT; SDL_PushEvent(&quit);
    });
    oslgpu_run_main_loop();
}
