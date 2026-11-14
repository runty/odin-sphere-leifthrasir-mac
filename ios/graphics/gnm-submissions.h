#pragma once
#include <span>
#include "../services/gpu.h"
#include "../services/memory.h"
#include "../services/wait_scope.h"
#include "native-wait.h"
#include "oslgpu.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/videoout/videoout_error.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include <array>
#include <cstring>
#include <vector>

// Only descriptor tables are copied. Command streams remain in checked shared
// identity mappings until the caller's normal GPU synchronization / final drain.
struct NativeGnmSubmissions {
    uint64_t calls=0,batches=0,buffers=0,bytes=0,submit_done=0;
    static constexpr uint32_t invalid=0x80d11000u;
    static bool shared(Memory &memory,uint64_t address,size_t size,bool writing=false) {
        if((address&3) || address>=(uint64_t(1)<<40) || size>(uint64_t(1)<<40)-address ||
           !DirectMemory::pointer(address,size,writing) || !oslgpu_guest_range_mapped(address,size)) {
            stopGuest(memory,"Gnm command or label is outside aligned shared GPU mappings");return false;
        }
        return true;
    }
    bool call(const char *symbol,const char *nid,const uint64_t args[6],uint64_t stack,Memory &memory,
              uint64_t &result,int32_t video_handle,uint16_t registered,GuestWaitScope wait={}) {
        const bool done=!std::strcmp(symbol,"sceGnmSubmitDone");
        const bool flip=!std::strcmp(symbol,"sceGnmSubmitAndFlipCommandBuffers") || !std::strcmp(symbol,"sceGnmSubmitAndFlipCommandBuffersForWorkload");
        const bool workload=!std::strcmp(symbol,"sceGnmSubmitCommandBuffersForWorkload") || !std::strcmp(symbol,"sceGnmSubmitAndFlipCommandBuffersForWorkload");
        if(!done && !flip && std::strcmp(symbol,"sceGnmSubmitCommandBuffers") && !workload)return false;
        const auto entry=oslgpu_resolve(nid);
        if(!entry){stopGuest(memory,"native Gnm submission export is unavailable");return true;}
        using namespace Libraries::GnmDriver;
        if(done) {
            if(const char *reason=guestInterruption(memory)){stopGuest(memory,reason);return true;}
            int32_t status;
            {NativeGpuWait unlocked(wait);status=reinterpret_cast<decltype(&sceGnmSubmitDone)>(entry)();}
            ++calls;++submit_done;result=static_cast<uint32_t>(status);
            if(const char *reason=guestInterruption(memory))stopGuest(memory,reason);
            return true;
        }
        const unsigned first=workload?1:0;
        const uint32_t count=static_cast<uint32_t>(args[first]);
        const uint64_t dcb_table=args[first+1],dcb_sizes=args[first+2],ccb_table=args[first+3],ccb_sizes=args[first+4];
        // These are native submission validation failures; avoid the backend's
        // zero-count flip underflow and inconsistent CCB pointer pair as well.
        if(!dcb_table || !dcb_sizes || (ccb_sizes && !ccb_table) || (flip && !count)){result=invalid;return true;}
        if(count>4096){stopGuest(memory,"Gnm submission count exceeds checked adapter budget");return true;}
        std::vector<uint32_t> draw_sizes(count),constant_sizes(count);
        if(count) {
            auto input=guestPointer(memory,dcb_sizes,count*4);if(!input)return true;
            std::memcpy(draw_sizes.data(),input,count*4);
            if(ccb_sizes){input=guestPointer(memory,ccb_sizes,count*4);if(!input)return true;std::memcpy(constant_sizes.data(),input,count*4);}
            for(unsigned i=0;i<count;++i)if(!draw_sizes[i] || draw_sizes[i]>0x3ffffcu || constant_sizes[i]>0x3ffffcu){result=invalid;return true;}
        }
        std::vector<uint64_t> draw_addresses(count),constant_addresses(count);
        if(count) {
            auto input=guestPointer(memory,dcb_table,count*8);if(!input)return true;
            std::memcpy(draw_addresses.data(),input,count*8);
            if(ccb_table){input=guestPointer(memory,ccb_table,count*8);if(!input)return true;std::memcpy(constant_addresses.data(),input,count*8);}
        }
        std::vector<const uint32_t *> draw(count),constant(count);
        std::vector<uint32_t *> writable_draw(count),writable_constant(count);
        uint64_t byte_count=0;
        for(unsigned i=0;i<count;++i) {
            const size_t dbytes=(draw_sizes[i]>>2)*4,cbytes=(constant_sizes[i]>>2)*4;
            if(dbytes && !shared(memory,draw_addresses[i],dbytes))return true;
            if(cbytes && !shared(memory,constant_addresses[i],cbytes))return true;
            draw[i]=dbytes?reinterpret_cast<const uint32_t *>(draw_addresses[i]):nullptr;constant[i]=cbytes?reinterpret_cast<const uint32_t *>(constant_addresses[i]):nullptr;
            writable_draw[i]=const_cast<uint32_t *>(draw[i]);writable_constant[i]=const_cast<uint32_t *>(constant[i]);
            byte_count+=dbytes+cbytes;
        }
        uint32_t handle=0,index=0,mode=0;int64_t argument=0;
        if(flip) {
            const size_t stack_count=workload?4:3;
            if(stack>UINT64_MAX-stack_count*8){stopGuest(memory,"invalid Gnm flip stack arguments");return true;}
            auto input=guestPointer(memory,stack+8,stack_count*8);if(!input)return true;
            std::array<uint64_t,4> extra{};std::memcpy(extra.data(),input,stack_count*8);
            handle=static_cast<uint32_t>(workload?extra[0]:args[5]);
            index=static_cast<uint32_t>(extra[workload?1:0]);mode=static_cast<uint32_t>(extra[workload?2:1]);
            argument=static_cast<int64_t>(extra[workload?3:2]);
            if(video_handle<=0 || static_cast<int32_t>(handle)!=video_handle){result=uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE);return true;}
            if(index>=16 || !(registered&(uint16_t(1)<<index))){result=uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX);return true;}
            const size_t words=draw_sizes.back()>>2;
            if(words<64){result=invalid;return true;}
            const auto patch_address=draw_addresses.back()+(words-64)*4;
            if(!shared(memory,patch_address,64*4,true))return true;
            std::array<uint32_t,7> header{};std::memcpy(header.data(),reinterpret_cast<const void *>(patch_address),sizeof(header));
            using Kind=AmdGpu::PM4CmdNop;
            if(header[0]!=0xc03e1000u || (header[1]!=Kind::PrepareFlip && header[1]!=Kind::PrepareFlipLabel &&
               header[1]!=Kind::PrepareFlipInterrupt && header[1]!=Kind::PrepareFlipInterruptLabel)) {
                stopGuest(memory,"Gnm flip is missing a supported prepare-flip packet");return true;
            }
            if(header[1]==Kind::PrepareFlipLabel || header[1]==Kind::PrepareFlipInterruptLabel) {
                if(header[1]==Kind::PrepareFlipLabel && (header[2]&3)){stopGuest(memory,"Gnm flip label is not aligned");return true;}
                const uint32_t high=header[1]==Kind::PrepareFlipInterruptLabel?(header[3]&0xffffu):header[3];
                const uint64_t label=(uint64_t(high)<<32)|(header[2]&~3u);
                if(!shared(memory,label,4,true))return true;
            }
        }
        if(const char *reason=guestInterruption(memory)){stopGuest(memory,reason);return true;}
        // A zero-count non-flip call may still emit native initialization work.
        // Preserve non-null table semantics without dereferencing unused ranges.
        uint32_t zero_size=0;const uint32_t *zero_pointer=nullptr;
        auto draws=count?draw.data():&zero_pointer;auto sizes=count?draw_sizes.data():&zero_size;
        int32_t status;
        {
            NativeGpuWait unlocked(wait);
            if(flip && workload)status=reinterpret_cast<decltype(&sceGnmSubmitAndFlipCommandBuffersForWorkload)>(entry)(static_cast<uint32_t>(args[0]),count,writable_draw.data(),sizes,ccb_table?writable_constant.data():nullptr,ccb_sizes?constant_sizes.data():nullptr,handle,index,mode,argument);
            else if(flip)status=reinterpret_cast<decltype(&sceGnmSubmitAndFlipCommandBuffers)>(entry)(count,writable_draw.data(),sizes,ccb_table?writable_constant.data():nullptr,ccb_sizes?constant_sizes.data():nullptr,handle,index,mode,argument);
            else if(workload)status=reinterpret_cast<decltype(&sceGnmSubmitCommandBuffersForWorkload)>(entry)(static_cast<uint32_t>(args[0]),count,draws,sizes,ccb_table?(count?constant.data():&zero_pointer):nullptr,ccb_sizes?(count?constant_sizes.data():&zero_size):nullptr);
            else status=reinterpret_cast<decltype(&sceGnmSubmitCommandBuffers)>(entry)(count,draws,sizes,ccb_table?(count?constant.data():&zero_pointer):nullptr,ccb_sizes?(count?constant_sizes.data():&zero_size):nullptr);
        }
        ++calls;result=static_cast<uint32_t>(status);
        if(!status){++batches;buffers+=count;bytes+=byte_count;}
        if(const char *reason=guestInterruption(memory))stopGuest(memory,reason);
        return true;
    }
};
