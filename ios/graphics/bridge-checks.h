// Included after NativeGpu: real renderer/API boundary checks, without running
// the guest or fabricating a presented game frame.
#include "../services/memory.h"
#include "core/libraries/videoout/videoout_error.h"
#include "core/libraries/kernel/time.h"
static unsigned bridge_checks() {
    unsigned checks=0;
    auto require=[&](bool ok) { if(!ok){std::fprintf(stderr,"GPU bridge check %u failed\n",checks+1);std::abort();}++checks; };
    DirectMemory owner;
    Memory memory;memory.external_pointer=DirectMemory::pointer;
    std::array<uint8_t,2048> bytes{},protected_bytes{};
    std::fill(bytes.begin(),bytes.end(),0x7b);std::fill(protected_bytes.begin(),protected_bytes.end(),0x7b);
    constexpr uint64_t base=0x20000000;
    mapRegion(memory,base,bytes.data(),bytes.size());mapRegion(memory,base+0x10000,protected_bytes.data(),protected_bytes.size(),false);
    NativeGpu gpu;
    const auto clock_start=std::chrono::steady_clock::now();
    const auto ticks_start=Libraries::Kernel::sceKernelReadTsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto ticks=Libraries::Kernel::sceKernelReadTsc()-ticks_start;
    const double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-clock_start).count();
    const double tick_seconds=double(ticks)/Libraries::Kernel::sceKernelGetTscFrequency();
    require(tick_seconds>wall*.75 && tick_seconds<wall*1.25);
    struct Cleanup { NativeGpu &gpu;DirectMemory &memory;bool done=false;
        ~Cleanup(){if(!done){NativeGpu::finish(&gpu);memory.reset();}} } cleanup{gpu,owner};
    auto call=[&](const char *name,std::array<uint64_t,6> args,uint64_t stack=base+0x100) {
        const char *nid=nullptr;
        for(const auto &entry:imports)if(!std::strcmp(entry.symbol,name)){nid=entry.nid;break;}
        require(nid!=nullptr);memory.fault={};uint64_t result=0;
        require(NativeGpu::call(&gpu,name,nid,args.data(),stack,&memory,&result));return result;
    };
    require(call("sceVideoOutOpen",{255,1,0,0})==0x80290001u && gpu.opened==0);
    require(call("sceVideoOutOpen",{255,0,0,0})>0 && gpu.opened==1);
    require(call("sceVideoOutOpen",{255,0,0,0})==uint32_t(ORBIS_VIDEO_OUT_ERROR_RESOURCE_BUSY) && gpu.opened==1);
    call("sceKernelCreateEqueue",{base+2044,base+0x20});require(!memory.fault.empty() && gpu.queues_created==0);
    std::memcpy(bytes.data()+0x20,"bridge",7);
    require(call("sceKernelCreateEqueue",{base+1,base+0x20})==0 && gpu.queues_created==1);
    int64_t queue=0;std::memcpy(&queue,bytes.data()+1,8);
    require(queue>0 && bytes[0]==0x7b && bytes[9]==0x7b);
    require(call("sceVideoOutAddVblankEvent",{UINT64_MAX,uint64_t(gpu.handle),0})==0x8029000cu);
    require(call("sceVideoOutAddFlipEvent",{uint64_t(queue),uint64_t(gpu.handle),0x1234})==0);
    require(call("sceVideoOutAddVblankEvent",{uint64_t(queue),uint64_t(gpu.handle),0x5678})==0 && gpu.events_added==2);
    auto wait=reinterpret_cast<int32_t (*)(int64_t,Libraries::Kernel::OrbisKernelEvent *,int,int *,uint32_t *)>(oslgpu_resolve("fzyMKs9kim0#libkernel"));
    require(wait!=nullptr);
    Libraries::Kernel::OrbisKernelEvent event{};int count=0;uint32_t timeout=100000;
    require(wait(queue,&event,1,&count,&timeout)==0 && count==1);
    require(event.ident==7 && event.filter==Libraries::Kernel::OrbisKernelEvent::VideoOut && event.udata==reinterpret_cast<void *>(0x5678));
    uint64_t pitch=96;std::memcpy(bytes.data()+0x108,&pitch,8);
    call("sceVideoOutSetBufferAttribute",{base+0x10000,0x80000000u,1,0,64,64});
    require(!memory.fault.empty() && protected_bytes[0]==0x7b && gpu.attributes_set==0);
    call("sceVideoOutSetBufferAttribute",{base+0x41,0x80000000u,1,0,64,64},UINT64_MAX-4);
    require(!memory.fault.empty() && bytes[0x41]==0x7b && gpu.attributes_set==0);
    require(call("sceVideoOutSetBufferAttribute",{base+0x41,0x80000000u,1,0,64,64})==0);
    Libraries::VideoOut::BufferAttribute attribute{};std::memcpy(&attribute,bytes.data()+0x41,sizeof(attribute));
    static_assert(sizeof(attribute)==40);
    require(attribute.width==64 && attribute.height==64 && attribute.pitch_in_pixel==96);
    require(bytes[0x40]==0x7b && bytes[0x69]==0x7b);
    int64_t physical=-1;void *buffer=nullptr;
    require(DirectMemory::allocate(0,DirectMemory::capacity(),16384,16384,0,&physical)==0);
    require(DirectMemory::map(&buffer,16384,3,0,physical,16384)==0);
    const uint64_t address=reinterpret_cast<uint64_t>(buffer);std::memcpy(bytes.data()+0x81,&address,8);
    call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+0x81,1,base+0x41});
    require(memory.fault=="VideoOut pitch differs from backend layout" && gpu.buffers_registered==0);
    pitch=64;std::memcpy(bytes.data()+0x108,&pitch,8);
    require(call("sceVideoOutSetBufferAttribute",{base+0x41,0x80000000u,1,0,64,64})==0);
    call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+2044,1,base+0x41});
    require(!memory.fault.empty() && gpu.buffers_registered==0);
    const uint64_t invalid=0;std::memcpy(bytes.data()+0x81,&invalid,8);
    call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+0x81,1,base+0x41});
    require(memory.fault=="VideoOut buffer is outside shared GPU mappings" && gpu.buffers_registered==0);
    std::memcpy(bytes.data()+0x81,&address,8);
    require(call("sceVideoOutRegisterBuffers",{uint64_t(gpu.handle),0,base+0x81,1,base+0x41})==0 && gpu.buffers_registered==1);
    // Compare real backend PM4 output, including unwritten padding, with checked
    // guest copy-out. No command is submitted or counted as a rendered frame.
    constexpr size_t init_words=Libraries::GnmDriver::HwInitPacketSize;
    static_assert(init_words==256);
    for(const char *name:{"sceGnmDrawInitDefaultHardwareState200","sceGnmDispatchInitDefaultHardwareState"}) {
        const char *nid=nullptr;for(const auto &entry:imports)if(!std::strcmp(entry.symbol,name)){nid=entry.nid;break;}
        require(nid!=nullptr);auto native=reinterpret_cast<uint32_t (*)(uint32_t *,uint32_t)>(oslgpu_resolve(nid));require(native!=nullptr);
        const auto before=bytes;const auto calls=gpu.gnm_init_calls;
        require(call(name,{UINT64_MAX,init_words-1})==0 && memory.fault.empty() && bytes==before && gpu.gnm_init_calls==calls+1);
        call(name,{base+0x10000+0x301,init_words});require(!memory.fault.empty() && protected_bytes[0x301]==0x7b && gpu.gnm_init_calls==calls+1);
        call(name,{base+2044,init_words});require(!memory.fault.empty() && bytes==before && gpu.gnm_init_calls==calls+1);
        call(name,{UINT64_MAX,init_words});require(!memory.fault.empty() && gpu.gnm_init_calls==calls+1);
        for(uint32_t size:{uint32_t(init_words),UINT32_MAX}) {
            std::fill(bytes.begin()+0x300,bytes.begin()+0x702,0x7b);
            std::array<uint32_t,init_words+2> expected;expected.fill(0x7b7b7b7b);
            require(native(expected.data()+1,size)==init_words && expected.front()==0x7b7b7b7b && expected.back()==0x7b7b7b7b);
            require(call(name,{base+0x301,size})==init_words && memory.fault.empty());
            require(bytes[0x300]==0x7b && bytes[0x701]==0x7b && !std::memcmp(bytes.data()+0x301,expected.data()+1,init_words*4));
        }
    }
    require(gpu.gnm_init_calls==6 && gpu.gnm_init_words==4*init_words);
    NativeGpu::finish(&gpu);owner.reset();cleanup.done=true;
    require(gpu.opened==gpu.closed && gpu.queues_created==gpu.queues_deleted);
    require(gpu.mapped_before_reset && !oslgpu_guest_range_mapped(gpu.observed_mapping,1));
    require(osl_aot_memory_allocated()==0 && runtime_eventflag_live_count()==0);
    return checks;
}
