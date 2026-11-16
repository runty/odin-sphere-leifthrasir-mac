// Actual renderer + guest ownership checks; never a mobile-frame claim.
#include "guest-session.h"
#include "../services/clock.h"
#include "oslgpu.h"
#include "guest-gpu.h"
#include "image-layout.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <thread>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
extern "C" uint64_t osl_aot_memory_allocated();
extern "C" uint64_t runtime_eventflag_live_count();
#include "bridge-checks.h"
static unsigned checks=0;
static void require(bool ok){if(!ok){std::fprintf(stderr,"Guest session check %u failed\n",checks+1);std::_Exit(2);}++checks;}
static volatile sig_atomic_t chained=0;
static void previous(int){chained=chained+1;}
int main(int argc,char **argv){
    if(argc==4 && !std::strcmp(argv[1],"--validate")){
        uint32_t sdk=0xdeadbeef;const bool valid=osl_guest_session_sdk(argv[2],&sdk);
        require(valid==!std::strcmp(argv[3],"valid"));require(valid ? sdk==0x03008041 : sdk==0xdeadbeef);
        require(!osl_guest_session_sdk(nullptr,&sdk) && !osl_guest_session_sdk(argv[2],nullptr));
        std::printf("OSL_GUEST_VALIDATE:{\"checks\":%u,\"accepted\":%s}\n",checks,valid?"true":"false");return 0;
    }
    if(argc!=4)return 3;
    const std::string mode=argv[3];require(mode=="normal" || mode=="extended" || mode=="pre-stop" || mode=="live-stop" || mode=="bridge");
    uint32_t sdk=0;require(osl_guest_session_sdk(argv[1],&sdk));
    struct sigaction custom{},old_segv{},old_bus{};custom.sa_handler=previous;sigemptyset(&custom.sa_mask);
    require(!sigaction(SIGSEGV,&custom,&old_segv) && !sigaction(SIGBUS,&custom,&old_bus));
    for(uint32_t invalid:{99U,600001U,UINT32_MAX})require(!osl_guest_session_create_with_budget(invalid));
    for(uint32_t valid:{0U,100U,60001U,600000U}){auto *bounded=osl_guest_session_create_with_budget(valid);require(bounded && osl_guest_session_destroy(bounded));}
    auto *guest=mode=="extended" ? osl_guest_session_create_with_budget(5000) : osl_guest_session_create();
    auto *other=osl_guest_session_create();require(guest && other);
    require(osl_guest_session_faults_begin(guest));require(!osl_guest_session_faults_begin(guest) && !osl_guest_session_faults_begin(other));
    require(!osl_guest_session_destroy(guest));
    bool rejected=false;std::thread wrong([&]{rejected=!osl_guest_session_faults_begin(other) && !osl_guest_session_faults_end(guest);});wrong.join();require(rejected);
    require(!raise(SIGBUS) && !raise(SIGSEGV) && chained==2); // Chain an unclaimed software signal, not a fabricated GPU fault.
    require(osl_guest_session_destroy(other));
    oslgpu_register_kernel();const std::string profile=argv[2],settings=profile+"/osl.ini";
    OslGpuConfig config{"Odin guest ownership check","CUSA05083",profile.c_str(),settings.c_str(),sdk,0,960,540};
    require(oslgpu_init(&config)==0);
    if(mode=="live-stop")require(osl_guest_session_test_gate_after_buffers(guest));
    if(mode=="pre-stop")osl_guest_session_request_stop(guest);
    std::atomic<bool> done{false};std::string report;
    std::thread worker([&]{
        if(mode=="bridge")report="{\"bridge_checks\":"+std::to_string(bridge_checks())+",\"game_frames\":false}";
        else report=osl_guest_session_run(guest,argv[1]);
        done=true;
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);bool stopped=false;
    while(!done && std::chrono::steady_clock::now()<deadline){
        require(oslgpu_poll_main_thread()==1);
        if(mode=="live-stop" && !stopped && osl_guest_session_gpu_calls(guest)>=7){
            require(!osl_guest_session_destroy(guest) && !osl_guest_session_faults_end(guest));
            osl_guest_session_request_stop(guest);stopped=true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(done);worker.join();require(mode!="live-stop" || stopped);
    std::printf("OSL_GUEST_SESSION:%s\n",report.c_str());
    if(mode!="bridge")require(std::strstr(osl_guest_session_run(guest,argv[1]),"Invalid or reused")!=nullptr);
    require(oslgpu_shutdown_main_thread()==0 && !SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    require(osl_guest_session_faults_end(guest));
    struct sigaction segv{},bus{};require(!sigaction(SIGSEGV,nullptr,&segv) && !sigaction(SIGBUS,nullptr,&bus));
    require(segv.sa_handler==previous && bus.sa_handler==previous);
    require(!raise(SIGBUS) && !raise(SIGSEGV) && chained==4);
    require(osl_guest_session_destroy(guest));
    require(!sigaction(SIGSEGV,&old_segv,nullptr) && !sigaction(SIGBUS,&old_bus,nullptr));
    std::printf("OSL_GUEST_OWNERSHIP:{\"checks\":%u,\"mode\":\"%s\",\"guest_worker_joined\":true,\"fault_handlers_restored\":true,\"renderer_stopped\":true,\"game_frames\":false}\n",checks,argv[3]);
    return 0;
}
