// Real native command-processor retirement, with no guest frame or VideoOut.
#include "guest-gpu.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "../services/memory.h"
#include "../services/clock.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
static unsigned checks;
static void gpuFault(int number,siginfo_t *info,void *context){
    if(info && reinterpret_cast<uintptr_t>(info->si_addr)<(uint64_t(1)<<40) && oslgpu_handle_fault(context,info->si_addr))return;
    static constexpr char message[]="GPU retirement: unhandled native memory fault\n";
    write(STDERR_FILENO,message,sizeof(message)-1);_exit(128+number);
}
static void require(bool ok){if(!ok){std::fprintf(stderr,"GPU retirement check %u failed\n",checks+1);std::abort();}++checks;}
static void entered(const std::atomic<bool> &flag){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!flag && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    require(flag);
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    const std::string profile=argv[1],settings=profile+"/osl.ini";
    struct sigaction action{},old_segv{},old_bus{};action.sa_sigaction=gpuFault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    require(!sigaction(SIGSEGV,&action,&old_segv) && !sigaction(SIGBUS,&action,&old_bus));
    oslgpu_register_kernel();OslGpuConfig config{"GPU retirement checks","CUSA05083",profile.c_str(),settings.c_str(),0x03008041,0,960,540};
    require(oslgpu_init(&config)==0 && liverpool);
    DirectMemory memory;NativeGpu gpu;int64_t physical=-1;void *mapping=nullptr;
    require(DirectMemory::allocate(0,DirectMemory::capacity(),16384,16384,0,&physical)==0);
    require(DirectMemory::map(&mapping,16384,3,0,physical,16384)==0);
    const uint64_t address=reinterpret_cast<uint64_t>(mapping);
    require(oslgpu_guest_range_mapped(address,16384));
    auto *command=static_cast<uint32_t *>(mapping);auto *ack=command+1024;
    for(uint32_t round=1;round<=12;++round){
        *ack=0;
        auto *packet=reinterpret_cast<AmdGpu::PM4CmdWriteData *>(command);
        packet->header=AmdGpu::PM4Type3Header{AmdGpu::PM4ItOpcode::WriteData,3};
        packet->raw=0x500u;packet->Address<uintptr_t>(reinterpret_cast<uintptr_t>(ack));packet->data[0]=0x2468ac00u+round;
        std::atomic<bool> active{false},release{false},callback_done{false},finished{false};
        liverpool->SendCommand([&]{active=true;while(!release)std::this_thread::yield();callback_done=true;});
        entered(active);liverpool->SubmitGfx(std::span<const uint32_t>{command,5},{});
        std::thread cleanup([&]{NativeGpu::finish(&gpu);finished=true;});
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        require(!finished && gpu.handle==0); // No VideoOut must still drain work.
        release=true;cleanup.join();
        require(finished && callback_done && *ack==0x2468ac00u+round);
        require(runtime_memory_is_mapped(address,16384) && oslgpu_guest_range_mapped(address,16384));
    }
    // Multiple host callers must receive their own complete retirement ticket;
    // an idle phase from before the request cannot release any caller early.
    std::atomic<bool> active{false},release{false};std::atomic<unsigned> started{0},finished{0};
    liverpool->SendCommand([&]{active=true;while(!release)std::this_thread::yield();});entered(active);
    std::vector<std::thread> waiters;
    for(unsigned i=0;i<4;++i)waiters.emplace_back([&]{++started;liverpool->WaitForRetirement();++finished;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(started!=4 && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    require(started==4);std::this_thread::sleep_for(std::chrono::milliseconds(15));require(finished==0);
    release=true;for(auto &thread:waiters)thread.join();require(finished==4);
    NativeGpu::finish(&gpu);
    require(gpu.mapped_before_reset && gpu.opened==0 && oslgpu_presented_frames()==0);
    memory.reset();require(!oslgpu_guest_range_mapped(address,16384) && DirectMemory::allocated()==0);
    require(oslgpu_shutdown_main_thread()==0 && !SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    require(!sigaction(SIGSEGV,&old_segv,nullptr) && !sigaction(SIGBUS,&old_bus,nullptr));
    std::printf("{\"gpu_retirement_checks\":%u,\"write_data_rounds\":12,\"concurrent_barriers\":4,\"no_videoout_cleanup\":true,\"mapping_released_after_retirement\":true,\"game_executed\":false,\"game_frames\":false,\"failed\":0}\n",checks);
}
