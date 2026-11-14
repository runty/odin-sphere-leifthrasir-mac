#include "guest-gpu.h"
#include "../services/clock.h"
#include "core/libraries/videoout/video_out.h"
#include "image-layout.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <thread>
#include <unistd.h>
extern "C" uint64_t runtime_process_time_counter(){return MobileClock::processCounter();}
extern "C" uint64_t runtime_process_time_us(){return MobileClock::processTime();}
extern "C" uint64_t runtime_tsc_frequency(){return MobileClock::frequency;}
extern "C" uint64_t runtime_tsc_counter(){return MobileClock::tsc();}
extern "C" void runtime_log_flush(){std::fflush(nullptr);}
extern "C" [[noreturn]] void runtime_exit_clean(){std::abort();}
static unsigned checks,fixture_frames;
static void require(bool ok){if(!ok){std::fprintf(stderr,"Gnm submission check %u failed\n",checks+1);std::abort();}++checks;}
static void fault(int n,siginfo_t *info,void *context){
    if(info && reinterpret_cast<uintptr_t>(info->si_addr)<(uint64_t(1)<<40) && oslgpu_handle_fault(context,info->si_addr))return;
    static constexpr char message[]="Submission fixture: unhandled native memory fault\n";write(2,message,sizeof(message)-1);_exit(128+n);
}
static void test(){
    DirectMemory owner;NativeGpu gpu;Memory memory;memory.external_pointer=DirectMemory::pointer;
    std::array<uint8_t,4096> bytes{},protected_bytes{};bytes.fill(0x7b);protected_bytes.fill(0x7b);
    constexpr uint64_t base=0x20000000,stack=base+0x101;
    mapRegion(memory,base,bytes.data(),bytes.size());mapRegion(memory,base+0x10000,protected_bytes.data(),protected_bytes.size(),false);
    auto put=[&](size_t offset,const auto &value){std::memcpy(bytes.data()+offset,&value,sizeof(value));};
    auto call=[&](const char *name,std::array<uint64_t,6> args={},uint64_t rsp=stack,GuestWaitScope wait=GuestWaitScope{}) {
        const char *nid=nullptr;for(const auto &entry:imports)if(!std::strcmp(entry.symbol,name)){nid=entry.nid;break;}
        require(nid!=nullptr);memory.fault={};memory.fault_address=0;uint64_t result=0xdeadbeef;
        require(NativeGpu::call(&gpu,name,nid,args.data(),rsp,&memory,&result,wait));return result;
    };
    int64_t physical=-1;void *command_mapping=nullptr;
    require(DirectMemory::allocate(0,DirectMemory::capacity(),32768,16384,0,&physical)==0);
    require(DirectMemory::map(&command_mapping,32768,3,0,physical,16384)==0);
    const auto command_address=reinterpret_cast<uint64_t>(command_mapping);
    auto *commands=static_cast<uint32_t *>(command_mapping);auto *acks=commands+4096;
    auto write=[&](unsigned word,uint32_t *destination,uint32_t value){
        auto *p=reinterpret_cast<AmdGpu::PM4CmdWriteData *>(commands+word);p->header=AmdGpu::PM4Type3Header{AmdGpu::PM4ItOpcode::WriteData,3};
        p->raw=0x500;p->Address<uintptr_t>(reinterpret_cast<uintptr_t>(destination));p->data[0]=value;
    };
    const uint64_t first=command_address,second=command_address+0x100,constant=command_address+0x200;
    const uint32_t twenty=20,eight=8,zero=0;
    write(0,acks,0x12345678);write(0x40,acks+1,0xfedcba98);commands[0x80]=0xc0001000;commands[0x81]=0;
    auto tables=[&]{put(1,first);put(9,second);put(0x41,twenty);put(0x45,twenty);put(0x81,constant);put(0x89,constant);put(0xa1,eight);put(0xa5,eight);};tables();
    for(const char *name:{"sceGnmSubmitCommandBuffers","sceGnmSubmitCommandBuffersForWorkload"}) {
        const bool workload=std::strstr(name,"ForWorkload")!=nullptr;
        auto args=[&](uint64_t count,uint64_t dcb,uint64_t sizes,uint64_t ccb=0,uint64_t csizes=0){
            return workload?std::array<uint64_t,6>{37,count,dcb,sizes,ccb,csizes}:std::array<uint64_t,6>{count,dcb,sizes,ccb,csizes};
        };
        const auto calls=gpu.submissions.calls;const auto before=bytes;
        require(call(name,args(1,0,UINT64_MAX))==NativeGnmSubmissions::invalid && memory.fault.empty());
        require(call(name,args(1,UINT64_MAX,base+0x41,0,base+0xa1))==NativeGnmSubmissions::invalid && memory.fault.empty());
        put(0x41,zero);require(call(name,args(1,UINT64_MAX,base+0x41))==NativeGnmSubmissions::invalid && memory.fault.empty());tables();
        const uint32_t huge=0x400000;put(0x41,huge);require(call(name,args(1,UINT64_MAX,base+0x41))==NativeGnmSubmissions::invalid && memory.fault.empty());tables();
        call(name,args(4097,base+1,base+0x41));require(memory.fault=="Gnm submission count exceeds checked adapter budget");
        call(name,args(1,base+4092,base+0x41));require(!memory.fault.empty());
        put(1,base+0x301);call(name,args(1,base+1,base+0x41));require(!memory.fault.empty());tables();
        const uint64_t misaligned=first+1;put(1,misaligned);call(name,args(1,base+1,base+0x41));require(!memory.fault.empty());tables();
        call(name,args(1,base+1,base+0x41,base+4092,base+0xa1));require(!memory.fault.empty());
        call(name,args(1,base+1,base+0x41,base+0x81,base+4094));require(!memory.fault.empty());
        const uint64_t private_constant=base+0x300;put(0x81,private_constant);call(name,args(1,base+1,base+0x41,base+0x81,base+0xa1));require(!memory.fault.empty());tables();
        require(gpu.submissions.calls==calls && bytes==before);
        // Native zero-count semantics do not dereference otherwise-unused tables.
        require(call(name,args(0,UINT64_MAX,UINT64_MAX))==0 && memory.fault.empty());
        *acks=0;acks[1]=0;const auto expected=bytes;
        require(call(name,args(2,base+1,base+0x41,base+0x81,base+0xa1))==0 && memory.fault.empty() && bytes==expected);
        liverpool->WaitForRetirement();oslgpu_finish();require(*acks==0x12345678 && acks[1]==0xfedcba98);
        require(DirectMemory::protect(command_mapping,16384,1)==0);
        *acks=0;require(call(name,args(1,base+1,base+0x41))==0 && memory.fault.empty());
        liverpool->WaitForRetirement();oslgpu_finish();require(*acks==0x12345678);
        require(DirectMemory::protect(command_mapping,16384,3)==0);
    }
    // Exercise the actual native SubmitDone wait while an independent guest
    // CPU needs both HLE leases to release the held GPU callback.
    {
        struct Lease {std::recursive_mutex state;std::unique_lock<std::recursive_mutex> locked{state};GuestMemoryScope memory;unsigned releases=0,acquires=0;
            GuestWaitScope scope(){return {this,[](void *p){auto &l=*static_cast<Lease *>(p);++l.releases;l.memory.unlock();l.locked.unlock();},[](void *p){auto &l=*static_cast<Lease *>(p);l.locked.lock();l.memory.lock();++l.acquires;}};}} lease;
        std::atomic<bool> active{false},release{false},other_acquired{false},stop{false};memory.stop_requested=&stop;
        liverpool->SendCommand([&]{active=true;while(!release)std::this_thread::yield();});
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!active && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();require(active);
        liverpool->SubmitGfx(std::span<const uint32_t>{commands+0x80,2},{});
        require(call("sceGnmSubmitDone",{},stack,lease.scope())==0 && memory.fault.empty());
        std::thread other([&]{std::scoped_lock lock{lease.state};GuestMemoryScope memory;other_acquired=true;stop=true;release=true;});
        require(call("sceGnmSubmitDone",{},stack,lease.scope())==0 && memory.fault=="guest execution interrupted");
        other.join();require(other_acquired && lease.releases==2 && lease.acquires==2 && lease.locked.owns_lock());
        const auto count=gpu.submissions.calls;call("sceGnmSubmitDone",{},stack,lease.scope());
        require(memory.fault=="guest execution interrupted" && gpu.submissions.calls==count && lease.releases==2);
        memory.stop_requested=nullptr;liverpool->WaitForRetirement();oslgpu_finish();
    }
    // Real flips use eight independent diagnostic buffers, avoiding reuse until
    // each queued presentation is observed. These are NOT game-generated frames.
    void *image_mapping=nullptr;require(DirectMemory::allocate(0,DirectMemory::capacity(),8*16384,16384,0,&physical)==0);
    require(DirectMemory::map(&image_mapping,8*16384,3,0,physical,16384)==0);
    auto *pixels=static_cast<uint32_t *>(image_mapping);
    for(unsigned i=0;i<8*4096;++i)pixels[i]=0xff006699u^(i&0xff);
    require(call("sceVideoOutOpen",{255,0,0,0})>0);
    const uint64_t pitch=64;put(0x109,pitch);
    require(call("sceVideoOutSetBufferAttribute",{base+0x201,0x80000000u,1,0,64,64})==0);
    for(unsigned i=0;i<8;++i){const uint64_t address=reinterpret_cast<uint64_t>(pixels+i*4096);put(0x281+i*8,address);}
    require(call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+0x281,8,base+0x201})==0 && gpu.registered==0xff);
    auto status=reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutGetFlipStatus)>(oslgpu_resolve("SbU3dwp80lQ#libSceVideoOut"));require(status!=nullptr);
    for(const char *name:{"sceGnmSubmitAndFlipCommandBuffers","sceGnmSubmitAndFlipCommandBuffersForWorkload"}) {
        const bool workload=std::strstr(name,"ForWorkload")!=nullptr;
        const uint32_t length=69*4;put(1,first);put(0x41,length);
        auto args=workload?std::array<uint64_t,6>{41,1,base+1,base+0x41,0,0}:std::array<uint64_t,6>{1,base+1,base+0x41,0,0,uint64_t(gpu.handle)};
        auto extra=[&](uint32_t handle,uint32_t index,int64_t argument){
            const std::array<uint64_t,4> values=workload?std::array<uint64_t,4>{handle,index,1,uint64_t(argument)}:std::array<uint64_t,4>{index,1,uint64_t(argument),0};
            std::memcpy(bytes.data()+0x109,values.data(),(workload?4:3)*8);
        };
        extra(gpu.handle,0,0);
        const auto calls=gpu.submissions.calls;
        auto empty=args;empty[workload?1:0]=0;require(call(name,empty,UINT64_MAX)==NativeGnmSubmissions::invalid && memory.fault.empty());
        auto bad_handle=args;if(!workload)bad_handle[5]=0;extra(0,0,0);
        require(call(name,bad_handle)==uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE) && memory.fault.empty());extra(gpu.handle,0,0);
        call(name,args,UINT64_MAX-4);require(!memory.fault.empty());
        call(name,args,base+4092);require(!memory.fault.empty());
        extra(gpu.handle,15,0);require(call(name,args)==uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX) && memory.fault.empty());
        extra(gpu.handle,0,0);const uint32_t short_buffer=16;put(0x41,short_buffer);require(call(name,args)==NativeGnmSubmissions::invalid && memory.fault.empty());put(0x41,length);
        std::fill(commands+5,commands+69,0);call(name,args);require(memory.fault=="Gnm flip is missing a supported prepare-flip packet");
        commands[5]=0xc03e1000;commands[6]=AmdGpu::PM4CmdNop::PrepareFlip;
        require(DirectMemory::protect(command_mapping,16384,1)==0);call(name,args);require(!memory.fault.empty());require(DirectMemory::protect(command_mapping,16384,3)==0);
        commands[6]=AmdGpu::PM4CmdNop::PrepareFlipLabel;commands[7]=uint32_t(base+0x300);commands[8]=0;
        call(name,args);require(!memory.fault.empty()); // CPU-private label must never reach the native parser.
        const uint64_t mapped_label=reinterpret_cast<uint64_t>(acks+4);commands[7]=uint32_t(mapped_label)|1;commands[8]=uint32_t(mapped_label>>32);
        call(name,args);require(memory.fault=="Gnm flip label is not aligned");
        commands[7]&=~3u;require(DirectMemory::protect(acks,16384,1)==0);call(name,args);require(!memory.fault.empty());require(DirectMemory::protect(acks,16384,3)==0);
        require(gpu.submissions.calls==calls);
        for(uint32_t kind:{uint32_t(AmdGpu::PM4CmdNop::PrepareFlip),uint32_t(AmdGpu::PM4CmdNop::PrepareFlipLabel),uint32_t(AmdGpu::PM4CmdNop::PrepareFlipInterrupt),uint32_t(AmdGpu::PM4CmdNop::PrepareFlipInterruptLabel)}) {
            const unsigned index=fixture_frames;const int64_t argument=static_cast<int64_t>(0x87654321fedcba90ull+index);
            write(0,acks,0x11223344);std::fill(commands+5,commands+69,0);
            commands[5]=0xc03e1000;commands[6]=kind;
            const bool tail=kind==AmdGpu::PM4CmdNop::PrepareFlipInterruptLabel;
            auto *label_word=tail?acks+4095:acks+4;auto *canary=tail?acks+4094:acks+5;
            const uint64_t label=reinterpret_cast<uint64_t>(label_word);*label_word=0;*canary=0x7b7b7b7b;
            commands[7]=uint32_t(label);commands[8]=uint32_t(label>>32);commands[9]=0x13579bdf;commands[10]=0x14;commands[11]=0;
            if(kind==AmdGpu::PM4CmdNop::PrepareFlipInterruptLabel)commands[8]|=0xa5000000u; // Native mask keeps only address bits.
            extra(gpu.handle,index,argument);const auto before=bytes;const auto frames=oslgpu_presented_frames();
            require(call(name,args)==0 && memory.fault.empty() && bytes==before);
            liverpool->WaitForRetirement();oslgpu_finish();
            Libraries::VideoOut::FlipStatus observed;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            do {if(status(gpu.handle,&observed)!=0)std::abort();if(observed.flip_arg==argument && oslgpu_presented_frames()>frames)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
            require(observed.flip_arg==argument && observed.current_buffer==int(index) && oslgpu_presented_frames()>frames);
            require(*acks==0x11223344);
            if(kind==AmdGpu::PM4CmdNop::PrepareFlipLabel || kind==AmdGpu::PM4CmdNop::PrepareFlipInterruptLabel)require(*label_word==0x13579bdf && *canary==0x7b7b7b7b);
            ++fixture_frames;
        }
    }
    require(gpu.submissions.batches==14 && gpu.submissions.buffers==14 && gpu.submissions.submit_done==2 && fixture_frames==8);
    NativeGpu::finish(&gpu);owner.reset();require(gpu.opened==gpu.closed && DirectMemory::allocated()==0 && !oslgpu_guest_range_mapped(command_address,32768));
}
int main(int argc,char **argv){
    if(argc!=2)return 2;struct sigaction action{},old_segv{},old_bus{};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    require(!sigaction(SIGSEGV,&action,&old_segv) && !sigaction(SIGBUS,&action,&old_bus));
    const std::string profile=argv[1],settings=profile+"/osl.ini";oslgpu_register_kernel();OslGpuConfig config{"GPU submission checks","CUSA05083",profile.c_str(),settings.c_str(),0x03008041,0,960,540};require(oslgpu_init(&config)==0);
    std::atomic<bool> done{false};std::thread worker([&]{test();done=true;});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(25);
    while(!done && std::chrono::steady_clock::now()<end){if(oslgpu_poll_main_thread()!=1)std::abort();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(!done)std::abort();worker.join();require(oslgpu_shutdown_main_thread()==0 && !SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD));
    require(!sigaction(SIGSEGV,&old_segv,nullptr) && !sigaction(SIGBUS,&old_bus,nullptr));
    std::printf("{\"gnm_submission_checks\":%u,\"fixture_frames\":%u,\"four_submit_abis\":true,\"hle_leases_released\":true,\"game_executed\":false,\"game_frames\":false,\"failed\":0}\n",checks,fixture_frames);
}
