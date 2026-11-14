#include "memory.h"
#include "../cpu/runtime.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <array>
#include <thread>
#include <mach/mach.h>
extern "C" uint32_t __remill_read_memory_32(Memory *, uint64_t);
extern "C" Memory *__remill_write_memory_32(Memory *, uint64_t, uint32_t);
extern "C" int runtime_memory_write_backing(uintptr_t,const void *,uint64_t);
extern "C" void runtime_memory_set_gpu_hooks(void (*)(uintptr_t,uint64_t),void (*)(uintptr_t,uint64_t),void (*)(uintptr_t,uint64_t));
static uintptr_t reset_address;
static unsigned reset_unmaps;
static bool unmap_observed_live_mapping;
static void reset_unmap(uintptr_t address,uint64_t size) {
    if (address==reset_address) {
        ++reset_unmaps;
        unmap_observed_live_mapping=DirectMemory::pointer(address,size,false)!=nullptr;
    }
}
static unsigned checks;
static void require(bool value) {
    if (!value) throw std::runtime_error("memory contract check " + std::to_string(checks+1));
    ++checks;
}
static uint64_t resident() {
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    require(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count)==KERN_SUCCESS);
    return info.resident_size;
}
int main() {
    try {
        constexpr uint64_t page=16384, size=51*1024*1024;
        uintptr_t stale=0;
        for (unsigned run=0;run<2;++run) {
            DirectMemory owner;
            Memory memory;
            memory.external_pointer=DirectMemory::pointer;
            require(!DirectMemory::pointer(stale,4,false));
            require(DirectMemory::capacity()==uint64_t(5056)*1024*1024);
            int64_t physical=-1;
            require(DirectMemory::allocate(0,DirectMemory::capacity(),page+1,page,0,&physical)!=0);
            require(DirectMemory::allocate(0,DirectMemory::capacity(),page,3*page,0,&physical)!=0);
            require(DirectMemory::release(UINT64_MAX-page+1,page)!=0);
            require(DirectMemory::protect(reinterpret_cast<void *>(UINT64_MAX-3),page,3)!=0);
            require(DirectMemory::unmap(reinterpret_cast<void *>(UINT64_MAX-3),page)!=0);
            const uint64_t before=resident();
            require(DirectMemory::allocate(0,DirectMemory::capacity(),size,2*1024*1024,0,&physical)==0);
            require(physical==0);
            require(DirectMemory::allocated()==size);
            require(DirectMemory::windowSize()>=(uint64_t(2)<<30) && DirectMemory::windowSize()<=(uint64_t(8)<<30));
            require(DirectMemory::backingViewSize()==0);
            void *first=nullptr, *alias=nullptr;
            require(DirectMemory::map(&first,size,0x33,0,physical,2*1024*1024)==0);
            require(DirectMemory::map(&alias,2*page,3,0,physical,page)==0);
            require(resident()<before+32*1024*1024); // Reserving a pool must not eagerly touch it.
            stale=reinterpret_cast<uintptr_t>(first);
            require(stale% (2*1024*1024)==0 && stale+size<(uint64_t(1)<<40));
            require(DirectMemory::pointer(stale,size,true)==first);
            require(DirectMemory::pointer(stale+size-1,2,false)!=nullptr); // Adjacent alias is mapped too.
            require(!DirectMemory::pointer(reinterpret_cast<uintptr_t>(alias)+2*page-1,2,false));
            require(!DirectMemory::pointer(UINT64_MAX-1,4,false));
            require(!DirectMemory::pointer(stale,0,false));
            __remill_write_memory_32(&memory,stale+page-2,0x5a3cc3a5);
            require(memory.fault.empty());
            uint32_t observed=0;
            std::memcpy(&observed,static_cast<uint8_t *>(alias)+page-2,4);
            require(observed==0x5a3cc3a5);
            require(DirectMemory::protect(first,page,2)==0);
            require(DirectMemory::pointer(stale,page,false)==first); // x86 write implies read.
            require(DirectMemory::pointer(stale,page,true)==first);
            require(DirectMemory::protect(first,page,1)==0);
            require(DirectMemory::pointer(stale,page,false)==first);
            require(!DirectMemory::pointer(stale,page,true));
            require(!DirectMemory::pointer(stale+page-2,4,true));
            __remill_write_memory_32(&memory,stale,9);
            require(!memory.fault.empty());
            memory.fault={};
            require(DirectMemory::protect(first,page,7)==0);
            vm_address_t address=stale;
            vm_size_t length=0;
            vm_region_basic_info_data_64_t region{};
            mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object=MACH_PORT_NULL;
            require(vm_region_64(mach_task_self(),&address,&length,VM_REGION_BASIC_INFO_64,
                                 (vm_region_info_t)&region,&count,&object)==KERN_SUCCESS);
            if (object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
            require(!(region.protection&VM_PROT_EXECUTE));
            // Host/GPU writeback must cross alias-budget and guest VMA
            // boundaries without relaxing the original read-only pages.
            std::vector<uint8_t> transfer(8*1024*1024+700);
            for(size_t i=0;i<transfer.size();++i)transfer[i]=uint8_t((i*31+i/997)^0xa7);
            require(DirectMemory::protect(first,6*1024*1024,1)==0);
            require(runtime_memory_write_backing(stale+37,transfer.data(),transfer.size())==1);
            require(std::memcmp(static_cast<uint8_t *>(first)+37,transfer.data(),transfer.size())==0);
            require(std::memcmp(static_cast<uint8_t *>(alias)+37,transfer.data(),2*page-37)==0);
            require(DirectMemory::pointer(stale,1,false)==first && !DirectMemory::pointer(stale,1,true));
            require(DirectMemory::protect(first,6*1024*1024,3)==0);
            std::array<bool,4> written{};
            std::array<std::thread,4> writers;
            for(unsigned worker=0;worker<writers.size();++worker)writers[worker]=std::thread([&,worker] {
                std::vector<uint8_t> payload(1024*1024+19,uint8_t(0x51+worker));
                const uintptr_t at=stale+10*1024*1024+worker*2*1024*1024+13;
                written[worker]=runtime_memory_write_backing(at,payload.data(),payload.size())==1 &&
                                std::memcmp(reinterpret_cast<void *>(at),payload.data(),payload.size())==0;
            });
            for(auto &writer:writers)writer.join();
            for(bool ok:written)require(ok);
            const uint8_t tail[]={1,7,3,9,5};
            require(runtime_memory_write_backing(stale+size-5,tail,sizeof(tail))==1);
            require(std::memcmp(static_cast<uint8_t *>(first)+size-5,tail,sizeof(tail))==0);
            require(runtime_memory_write_backing(UINTPTR_MAX-2,tail,4)==0);
            require(runtime_memory_write_backing(stale,nullptr,1)==0);
            require(runtime_memory_write_backing(stale,nullptr,0)==1);
            require(DirectMemory::unmap(first,page)==0);
            require(runtime_memory_write_backing(stale,tail,sizeof(tail))==0);
            require(!DirectMemory::pointer(stale,4,false));
            require(DirectMemory::pointer(stale+page,4,false)!=nullptr);
            require(__remill_read_memory_32(&memory,stale)==0 && !memory.fault.empty());
            memory.fault={};
            // A fixed request outside the owned window must not replace host storage.
            alignas(16384) uint8_t canary[16384];
            canary[0]=0x7b;
            void *outside=canary;
            require(DirectMemory::map(&outside,page,3,0x10,physical,page)!=0);
            require(canary[0]==0x7b);
            require(DirectMemory::release(physical,size)==0);
            require(!DirectMemory::pointer(reinterpret_cast<uintptr_t>(alias),4,false));
            require(!DirectMemory::pointer(stale+page,4,false));
            require(DirectMemory::allocate(0,DirectMemory::capacity(),2*page,page,0,&physical)==0);
            void *reused=nullptr;
            require(DirectMemory::map(&reused,2*page,3,0,physical,page)==0);
            for (unsigned i=0;i<2*page;++i)
                if (static_cast<uint8_t *>(reused)[i]) throw std::runtime_error("released pages were not zeroed");
            require(true);
            require(DirectMemory::release(physical,2*page)==0);
            require(DirectMemory::allocated()==0);
        }
        {
            DirectMemory owner;
            int64_t physical=-1;void *mapping=nullptr;
            require(DirectMemory::allocate(0,DirectMemory::capacity(),page,page,0,&physical)==0);
            require(DirectMemory::map(&mapping,page,3,0,physical,page)==0);
            reset_address=reinterpret_cast<uintptr_t>(mapping);
            runtime_memory_set_gpu_hooks(nullptr,reset_unmap,nullptr);
            owner.reset();
            require(reset_unmaps==1 && unmap_observed_live_mapping);
            require(!DirectMemory::pointer(reset_address,1,false) && DirectMemory::allocated()==0);
            require(DirectMemory::windowSize()==0 && DirectMemory::backingViewSize()==0);
            owner.reset();
            require(reset_unmaps==1); // Explicit reset followed by destruction is idempotent.
            runtime_memory_set_gpu_hooks(nullptr,nullptr,nullptr);
        }
        std::printf("{\"memory_checks\":%u,\"failed\":0,\"game_booted\":false}\n",checks);
    } catch (const std::exception &error) {
        std::fprintf(stderr,"%s\n",error.what()); return 1;
    }
}
