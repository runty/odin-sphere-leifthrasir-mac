#include "guest-gpu.h"
#include "../services/clock.h"
#include "image-layout.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <signal.h>
#include <source_location>
#include <thread>
#include <unistd.h>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
static unsigned checks;static double parked_guard_ms;
static void require(bool ok,std::source_location at=std::source_location::current()){if(!ok){std::fprintf(stderr,"Presentation check %u failed at line %u\n",checks+1,at.line());std::abort();}++checks;}
static void fault(int n,siginfo_t *info,void *context){
    if(info && reinterpret_cast<uintptr_t>(info->si_addr)<(uint64_t(1)<<40) && oslgpu_handle_fault(context,info->si_addr))return;
    static constexpr char message[]="Presentation fixture: unhandled native memory fault\n";write(2,message,sizeof(message)-1);_exit(128+n);
}
static void test(){
    NativeGpu gpu;DirectMemory owner;Memory memory;memory.external_pointer=DirectMemory::pointer;
    memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::array<uint8_t,2048> bytes{},protected_bytes{};bytes.fill(0x7b);protected_bytes.fill(0x7b);
    constexpr uint64_t base=0x20000000,stack=base+0x101;
    mapRegion(memory,base,bytes.data(),bytes.size());mapRegion(memory,base+0x10000,protected_bytes.data(),protected_bytes.size(),false);
    auto put=[&](size_t offset,const auto &value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    auto call=[&](const char *name,std::array<uint64_t,6> args={},GuestWaitScope wait=GuestWaitScope{}){
        const char *nid=nullptr;for(const auto &entry:imports)if(!std::strcmp(name,entry.symbol)){nid=entry.nid;break;}
        require(nid!=nullptr);memory.fault={};memory.fault_address=0;uint64_t result=0xdeadbeef;
        require(NativeGpu::call(&gpu,name,nid,args.data(),stack,&memory,&result,wait));return result;
    };
    const auto initial=bytes;
    require(call("sceVideoOutWaitVblank",{0})==uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE) && memory.fault.empty() && bytes==initial);
    require(call("sceGnmIsUserPaEnabled")==0 && gpu.presentation.performance_queries==1 && memory.fault.empty());
    require(call("sceVideoOutOpen",{255,0,0,0})>0);
    auto writer=reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertWaitFlipDone)>(oslgpu_resolve("1qXLHIpROPE#libSceGnmDriver"));require(writer!=nullptr);
    for(uint32_t size:{0u,6u,8u,UINT32_MAX})require(call("sceGnmInsertWaitFlipDone",{UINT64_MAX,size,0,UINT64_MAX})==UINT32_MAX && memory.fault.empty() && bytes==initial);
    require(call("sceGnmInsertWaitFlipDone",{UINT64_MAX,7,0,0})==uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE) && memory.fault.empty());
    require(call("sceGnmInsertWaitFlipDone",{UINT64_MAX,7,uint64_t(gpu.handle),16})==uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX) && memory.fault.empty());
    for(uint64_t pointer:{base+0x10001,base+2044,UINT64_MAX}){
        call("sceGnmInsertWaitFlipDone",{pointer,7,uint64_t(gpu.handle),0});require(!memory.fault.empty() && bytes==initial && protected_bytes[1]==0x7b);
    }
    for(uint32_t index:{0u,15u}){
        alignas(8) std::array<uint32_t,12> expected;expected.fill(0x7b7b7b7b);
        require(writer(expected.data()+2,7,gpu.handle,index)==0);
        require(call("sceGnmInsertWaitFlipDone",{base+1,7,uint64_t(gpu.handle),index})==0 && memory.fault.empty());
        require(!std::memcmp(bytes.data()+1,expected.data()+2,28) && bytes[0]==0x7b && bytes[29]==0x7b && expected[1]==0x7b7b7b7b && expected[9]==0x7b7b7b7b);
    }
    auto status=reinterpret_cast<int32_t (*)(int32_t,Libraries::VideoOut::SceVideoOutVblankStatus *)>(oslgpu_resolve("1FZBKy8HeNU#libSceVideoOut"));
    auto legacy=reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutWaitVblank)>(oslgpu_resolve("j6RaAUlaLv0#libSceVideoOut"));require(status && legacy);
    Libraries::VideoOut::SceVideoOutVblankStatus before{},after{};
    require(status(gpu.handle,&before)==0 && legacy(gpu.handle)==0 && status(gpu.handle,&after)==0 && after.count!=before.count);
    require(status(gpu.handle,&before)==0 && call("sceVideoOutWaitVblank",{uint64_t(gpu.handle)})==0 && memory.fault.empty() && status(gpu.handle,&after)==0 && after.count!=before.count);
    // Observe a guard check after a real CV wait, rather than only at entry.
    bool parked=false;
    for(unsigned attempt=0;attempt<32 && !parked;++attempt){
        require(legacy(gpu.handle)==0);unsigned observations=0;
        const auto start=std::chrono::steady_clock::now();
        const auto result=Libraries::VideoOut::WaitVblankGuarded(gpu.handle,[](void *p)->int{return ++*static_cast<unsigned *>(p)>=2;},&observations);
        if(uint32_t(result)==0x80020004u){parked=true;parked_guard_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();require(observations>=2 && parked_guard_ms<250);}
        else require(result==0);
    }
    require(parked);
    {
        struct Lease {std::recursive_mutex state;std::unique_lock<std::recursive_mutex> lock{state};GuestMemoryScope memory;unsigned releases=0,restores=0;std::function<void()> released;
            GuestWaitScope scope(){return {this,[](void *p){auto &l=*static_cast<Lease *>(p);++l.releases;l.memory.unlock();l.lock.unlock();if(l.released)l.released();},[](void *p){auto &l=*static_cast<Lease *>(p);l.lock.lock();l.memory.lock();++l.restores;}};}} lease;
        std::atomic<bool> go{false},acquired{false},stop{false};memory.stop_requested=&stop;
        std::thread other([&]{while(!go)std::this_thread::yield();std::scoped_lock lock{lease.state};GuestMemoryScope memory;acquired=true;});
        lease.released=[&]{go=true;while(!acquired)std::this_thread::yield();};
        require(call("sceVideoOutWaitVblank",{uint64_t(gpu.handle)},lease.scope())==0 && memory.fault.empty());other.join();require(acquired && lease.releases==1 && lease.restores==1 && lease.lock.owns_lock());
        const auto snapshot=bytes;
        lease.released=[&]{stop=true;};
        require(call("sceVideoOutWaitVblank",{uint64_t(gpu.handle)},lease.scope())==0xdeadbeef && memory.fault=="guest execution interrupted" && bytes==snapshot);
        const auto waits=gpu.presentation.vblank_waits;
        require(call("sceVideoOutWaitVblank",{uint64_t(gpu.handle)},lease.scope())==0xdeadbeef && memory.fault=="guest execution interrupted" && gpu.presentation.vblank_waits==waits && lease.releases==2 && lease.restores==2);
        stop=false;memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        lease.released=[&]{memory.deadline=std::chrono::steady_clock::now()-std::chrono::milliseconds(1);};
        require(call("sceVideoOutWaitVblank",{uint64_t(gpu.handle)},lease.scope())==0xdeadbeef && memory.fault=="guest execution deadline exceeded" && bytes==snapshot && lease.releases==3 && lease.restores==3);
        memory.stop_requested=nullptr;memory.deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    }
    // Hold the current presentation's real label, then let a CPU flip to a
    // second buffer release it. The generated GPU wait must block retirement.
    int64_t physical=-1;void *mapping=nullptr;require(DirectMemory::allocate(0,DirectMemory::capacity(),3*16384,16384,0,&physical)==0);
    require(DirectMemory::map(&mapping,3*16384,3,0,physical,16384)==0);
    auto *command=static_cast<uint32_t *>(mapping);auto *pixels=command+4096;
    for(unsigned i=0;i<8192;++i)pixels[i]=0xff663300u^(i&0xff);
    const uint64_t pitch=64;put(0x109,pitch);
    require(call("sceVideoOutSetBufferAttribute",{base+0x201,0x80000000u,1,0,64,64})==0);
    for(unsigned i=0;i<2;++i){const uint64_t address=reinterpret_cast<uint64_t>(pixels+i*4096);put(0x281+i*8,address);}
    require(call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+0x281,2,base+0x201})==0);
    std::fill(command,command+64,0);command[0]=0xc03e1000;command[1]=AmdGpu::PM4CmdNop::PrepareFlip;
    const uint64_t address=reinterpret_cast<uint64_t>(command);const uint32_t size=256;put(1,address);put(0x41,size);
    const std::array<uint64_t,3> extra{0,1,0x123456789abcdef0ull};std::memcpy(bytes.data()+0x109,extra.data(),sizeof(extra));
    require(call("sceGnmSubmitAndFlipCommandBuffers",{1,base+1,base+0x41,0,0,uint64_t(gpu.handle)})==0 && memory.fault.empty());
    auto flip_status=reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutGetFlipStatus)>(oslgpu_resolve("SbU3dwp80lQ#libSceVideoOut"));
    auto cpu_flip=reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutSubmitFlip)>(oslgpu_resolve("U46NwOiJpys#libSceVideoOut"));require(flip_status && cpu_flip);
    auto flip_rate=reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutSetFlipRate)>(oslgpu_resolve("CBiu4mCE1DA#libSceVideoOut"));require(flip_rate!=nullptr);
    Libraries::VideoOut::FlipStatus observed{};
    auto presented=[&](int index){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{if(flip_status(gpu.handle,&observed))std::abort();if(observed.current_buffer==index)return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<end);return false;};
    require(presented(0));liverpool->WaitForRetirement();oslgpu_finish();
    // CPU flips queue preparation on the GPU thread. Prepare the next frame
    // before blocking that thread, and hold its real presentation by flip rate.
    require(flip_rate(gpu.handle,100000)==0 && legacy(gpu.handle)==0);
    require(cpu_flip(gpu.handle,1,1,0x23456789abcdef01ll)==0);
    liverpool->WaitForRetirement();oslgpu_finish();require(observed.current_buffer==0);
    require(call("sceGnmInsertWaitFlipDone",{base+0x301,7,uint64_t(gpu.handle),0})==0 && memory.fault.empty());
    std::memcpy(command,bytes.data()+0x301,28);
    auto *ack=command+1024;*ack=0;
    auto *write=reinterpret_cast<AmdGpu::PM4CmdWriteData *>(command+7);write->header=AmdGpu::PM4Type3Header{AmdGpu::PM4ItOpcode::WriteData,3};write->raw=0x500;write->Address<uintptr_t>(reinterpret_cast<uintptr_t>(ack));write->data[0]=0x13579bdf;
    const uint32_t length=48;put(0x41,length);require(call("sceGnmSubmitCommandBuffers",{1,base+1,base+0x41,0,0})==0 && memory.fault.empty());
    std::atomic<bool> retired{false};std::thread retirement([&]{liverpool->WaitForRetirement();retired=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));require(!retired);
    require(flip_rate(gpu.handle,0)==0 && presented(1));retirement.join();oslgpu_finish();require(retired && *ack==0x13579bdf);
    NativeGpu::finish(&gpu);owner.reset();require(gpu.opened==gpu.closed && DirectMemory::allocated()==0 && !oslgpu_guest_range_mapped(address,3*16384));
}
int main(int argc,char **argv){
    if(argc!=2)return 2;struct sigaction action{},old_segv{},old_bus{};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);require(!sigaction(SIGSEGV,&action,&old_segv) && !sigaction(SIGBUS,&action,&old_bus));
    const std::string profile=argv[1],settings=profile+"/osl.ini";oslgpu_register_kernel();OslGpuConfig config{"Presentation synchronization checks","CUSA05083",profile.c_str(),settings.c_str(),0x03008041,0,960,540};require(oslgpu_init(&config)==0);
    std::atomic<bool> done{false};std::thread worker([&]{test();done=true;});const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    while(!done && std::chrono::steady_clock::now()<end){if(oslgpu_poll_main_thread()!=1)std::abort();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(!done)std::abort();worker.join();require(oslgpu_shutdown_main_thread()==0 && !SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));require(!sigaction(SIGSEGV,&old_segv,nullptr) && !sigaction(SIGBUS,&old_bus,nullptr));
    std::printf("{\"presentation_checks\":%u,\"parked_guard_ms\":%.3f,\"native_vblank_count_advanced\":true,\"host_interruptions_masked\":true,\"queued_flip_wait_blocks\":true,\"fixture_frames\":2,\"game_executed\":false,\"game_frames\":false,\"failed\":0}\n",checks,parked_guard_ms);
}
