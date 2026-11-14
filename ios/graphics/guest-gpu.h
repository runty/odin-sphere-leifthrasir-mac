#pragma once
// Checked, native VideoOut/event-queue marshalling shared by desktop AOT and
// UIKit frontends. Own this context until the guest report returns: Session
// joins guest workers, calls finish, then releases shared direct mappings.
#include "../services/gpu.h"
#include "oslgpu.h"
#include "core/libraries/kernel/equeue.h"
#include "core/libraries/videoout/buffer.h"
#include "core/libraries/gnmdriver/gnmdriver_init.h"
#include "video_core/texture_cache/image_info.h"
#include <array>
#include <cstdlib>
#include <cstring>
extern "C" int runtime_memory_region(uintptr_t,uintptr_t *,uintptr_t *,int *);
extern "C" int runtime_memory_is_mapped(uintptr_t,uint64_t);

struct NativeGpu {
    int32_t handle=0;
    unsigned opened=0,closed=0;
    uint64_t observed_mapping=0;
    bool mapped_before_reset=false;
    std::array<int64_t,64> queues{};
    unsigned queues_created=0,queues_deleted=0,events_added=0,attributes_set=0,buffers_registered=0;
    uint64_t gnm_init_calls=0,gnm_init_words=0;
    static bool format32(uint64_t value) {
        return value==0x80000000u || value==0x80002200u || value==0x88060000u || value==0x88000000u || value==0x88740000u;
    }
    static bool call(void *opaque,const char *symbol,const char *nid,const uint64_t args[6],uint64_t stack,Memory *memory,uint64_t *result) {
        auto &self=*static_cast<NativeGpu *>(opaque);
        using namespace Libraries::VideoOut;
        if(!std::strcmp(symbol,"sceGnmDrawInitDefaultHardwareState200") ||
           !std::strcmp(symbol,"sceGnmDispatchInitDefaultHardwareState")) {
            constexpr size_t words=Libraries::GnmDriver::HwInitPacketSize;
            std::array<uint32_t,words> packet{};
            const uint32_t size=static_cast<uint32_t>(args[1]);
            uint8_t *output=nullptr;
            if(size>=words) {
                output=guestPointer(*memory,args[0],sizeof(packet),true);if(!output)return true;
                // Native packet writers require aligned u32 storage. Preserve
                // untouched NOP padding and support unaligned guest pointers.
                std::memcpy(packet.data(),output,sizeof(packet));
            }
            auto init=reinterpret_cast<uint32_t (*)(uint32_t *,uint32_t)>(oslgpu_resolve(nid));
            if(!init){stopGuest(*memory,"native Gnm initialization export is unavailable");return true;}
            const uint32_t written=init(packet.data(),size);
            if(written && (written!=words || !output)){stopGuest(*memory,"unexpected native Gnm initialization size");return true;}
            if(written)std::memcpy(output,packet.data(),sizeof(packet));
            ++self.gnm_init_calls;self.gnm_init_words+=written;*result=written;return true;
        }
        if (!std::strcmp(symbol,"sceVideoOutSetBufferAttribute")) {
            auto output=guestPointer(*memory,args[0],sizeof(BufferAttribute),true);if(!output)return true;
            if(stack>UINT64_MAX-8){stopGuest(*memory,"invalid VideoOut stack argument");return true;}
            auto input=guestPointer(*memory,stack+8,8);if(!input)return true;
            uint64_t pitch=0;std::memcpy(&pitch,input,8);
            if(!format32(static_cast<uint32_t>(args[1]))){stopGuest(*memory,"unsupported VideoOut pixel format");return true;}
            auto set=reinterpret_cast<void (*)(BufferAttribute *,PixelFormat,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t)>(oslgpu_resolve(nid));
            if(!set){stopGuest(*memory,"native VideoOut attribute export is unavailable");return true;}
            BufferAttribute attribute{};
            set(&attribute,static_cast<PixelFormat>(args[1]),args[2],args[3],args[4],args[5],pitch);
            std::memcpy(output,&attribute,sizeof(attribute));*result=0;++self.attributes_set;return true;
        }
        if (!std::strcmp(symbol,"sceVideoOutRegisterBuffers")) {
            const int32_t start=static_cast<int32_t>(args[1]),count=static_cast<int32_t>(args[3]);
            if(!self.handle || static_cast<int32_t>(args[0])!=self.handle){*result=0x8029000bu;return true;}
            if(start<0 || count<1 || count>16 || start>16-count){*result=0x80290001u;return true;}
            auto attributes=guestPointer(*memory,args[4],sizeof(BufferAttribute));if(!attributes)return true;
            BufferAttributeGroup group{};std::memcpy(&group.attrib,attributes,sizeof(group.attrib));
            const auto &a=group.attrib;
            if(!format32(static_cast<uint32_t>(a.pixel_format)) || !a.width || !a.height || a.width>16384 ||
               a.height>16384 || a.pitch_in_pixel<a.width || a.pitch_in_pixel>16384 || a.aspect_ratio ||
               a.reserved0 || a.reserved1 || (a.tiling_mode!=TilingMode::Tile && a.tiling_mode!=TilingMode::Linear)) {
                *result=0x80290001u;return true;
            }
            const uint32_t native_pitch=a.tiling_mode==TilingMode::Linear ? a.width : (a.width+127)&~127u;
            if(a.pitch_in_pixel!=native_pitch){stopGuest(*memory,"VideoOut pitch differs from backend layout");return true;}
            auto addresses=guestPointer(*memory,args[2],count*8);if(!addresses)return true;
            std::array<void *,16> native{};
            for(int32_t i=0;i<count;++i) {
                uint64_t address=0;std::memcpy(&address,addresses+i*8,8);
                VideoCore::ImageInfo info(group,address);
                if(address>=(uint64_t(1)<<40) || !info.guest_size || info.guest_size>(uint64_t(1)<<40)-address ||
                   !runtime_memory_is_mapped(address,info.guest_size) || !oslgpu_guest_range_mapped(address,info.guest_size)) {
                    stopGuest(*memory,"VideoOut buffer is outside shared GPU mappings");return true;
                }
                native[i]=reinterpret_cast<void *>(address);
            }
            auto reg=reinterpret_cast<int32_t (*)(int32_t,int32_t,void *const *,int32_t,const BufferAttribute *)>(oslgpu_resolve(nid));
            if(!reg){stopGuest(*memory,"native VideoOut buffer export is unavailable");return true;}
            const int32_t status=reg(self.handle,start,native.data(),count,&group.attrib);
            *result=static_cast<uint32_t>(status);if(status>=0)self.buffers_registered+=count;return true;
        }
        if (!std::strcmp(symbol,"sceKernelCreateEqueue")) {
            if (!args[0] || !args[1]) { *result=0x80020016u;return true; }
            auto output=guestPointer(*memory,args[0],8,true);if(!output)return true;
            char name[33]{}; bool terminated=false;
            for(unsigned i=0;i<33;++i) {
                auto byte=guestPointer(*memory,args[1]+i,1);if(!byte)return true;
                if(i==32 && *byte) { *result=0x8002003fu;return true; }
                name[i]=static_cast<char>(*byte);if(!*byte){terminated=true;break;}
            }
            if(!terminated || self.queues_created==self.queues.size()) { *result=0x8002000cu;return true; }
            auto create=reinterpret_cast<int32_t (*)(int64_t *,const char *)>(oslgpu_resolve(nid));
            if(!create){stopGuest(*memory,"native event queue export is unavailable");return true;}
            int64_t queue=0;*result=static_cast<uint32_t>(create(&queue,name));
            if(!*result) {self.queues[self.queues_created++]=queue;std::memcpy(output,&queue,8);}
            return true;
        }
        if (!std::strcmp(symbol,"sceVideoOutAddFlipEvent") || !std::strcmp(symbol,"sceVideoOutAddVblankEvent")) {
            bool owned=false;
            for(unsigned i=0;i<self.queues_created;++i) owned|=self.queues[i]==static_cast<int64_t>(args[0]);
            if(!owned){*result=0x8029000cu;return true;}
            if(!self.handle || static_cast<int32_t>(args[1])!=self.handle){*result=0x8029000bu;return true;}
            auto add=reinterpret_cast<int32_t (*)(int64_t,int32_t,void *)>(oslgpu_resolve(nid));
            if(!add){stopGuest(*memory,"native VideoOut event export is unavailable");return true;}
            *result=static_cast<uint32_t>(add(static_cast<int64_t>(args[0]),self.handle,reinterpret_cast<void *>(args[2])));
            if(!*result)++self.events_added;
            return true;
        }
        if (std::strcmp(symbol,"sceVideoOutOpen")) return false;
        if (static_cast<int32_t>(args[1])!=0 || static_cast<int32_t>(args[2])!=0) { *result=0x80290001u; return true; }
        if (args[3]) { stopGuest(*memory,"VideoOut service-thread parameters are unsupported"); return true; }
        auto open=reinterpret_cast<int32_t (*)(int32_t,int32_t,int32_t,const void *)>(oslgpu_resolve(nid));
        if (!open) { stopGuest(*memory,"native VideoOut export is unavailable"); return true; }
        const int32_t handle=open(static_cast<int32_t>(args[0]),0,0,nullptr);
        *result=static_cast<uint32_t>(handle);
        if (handle>0) { self.handle=handle; ++self.opened; }
        return true;
    }
    static void finish(void *opaque) {
        auto &self=*static_cast<NativeGpu *>(opaque);
        if (self.handle) {
            oslgpu_finish();
            auto close=reinterpret_cast<int32_t (*)(int32_t)>(oslgpu_resolve("uquVH4-Du78#libSceVideoOut"));
            if (!close || close(self.handle)) std::abort();
            self.handle=0; ++self.closed;
        }
        auto remove=reinterpret_cast<int32_t (*)(int64_t)>(oslgpu_resolve("jpFjmgAC5AE#libkernel"));
        for(unsigned i=0;i<self.queues_created;++i) {
            if(!remove || remove(self.queues[i]) || Libraries::Kernel::GetEqueue(self.queues[i]))std::abort();
            ++self.queues_deleted;
        }
        uintptr_t cursor=0,start=0,end=0; int mapped=0;
        for(unsigned i=0;i<4096 && runtime_memory_region(cursor,&start,&end,&mapped);++i) {
            if (mapped) {
                self.observed_mapping=start;
                self.mapped_before_reset=oslgpu_guest_range_mapped(start,1);
                break;
            }
            if(end<=cursor) break;
            cursor=end;
        }
    }
};
