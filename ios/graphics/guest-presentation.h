#pragma once
#include "../services/gpu.h"
#include "native-wait.h"
#include "oslgpu.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/videoout/video_out.h"
#include "core/libraries/videoout/videoout_error.h"
#include <array>
#include <cstring>

struct NativeGpuPresentation {
    uint64_t flip_wait_packets=0,vblank_waits=0,interruptions=0,performance_queries=0;
    bool call(const char *symbol,const char *nid,const uint64_t args[6],Memory &memory,uint64_t &result,
              int32_t handle,GuestWaitScope wait={}) {
        if(!std::strcmp(symbol,"sceGnmIsUserPaEnabled")) {
            auto query=reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmIsUserPaEnabled)>(oslgpu_resolve(nid));
            if(!query){stopGuest(memory,"native Gnm performance query is unavailable");return true;}
            result=query()?1:0;++performance_queries;return true;
        }
        if(!std::strcmp(symbol,"sceGnmInsertWaitFlipDone")) {
            auto write=reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertWaitFlipDone)>(oslgpu_resolve(nid));
            if(!write){stopGuest(memory,"native Gnm flip-wait export is unavailable");return true;}
            const uint32_t size=static_cast<uint32_t>(args[1]);
            if(size!=7){result=static_cast<uint32_t>(write(nullptr,size,0,0));return true;}
            if(handle<=0 || static_cast<int32_t>(args[2])!=handle){result=uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE);return true;}
            if(static_cast<uint32_t>(args[3])>=16){result=uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_INDEX);return true;}
            auto output=guestPointer(memory,args[0],7*4,true);if(!output)return true;
            // The backend writes an eight-byte label address at word offset 2.
            alignas(8) std::array<uint32_t,7> packet{};
            const int32_t status=write(packet.data(),7,handle,static_cast<uint32_t>(args[3]));
            result=static_cast<uint32_t>(status);if(!status){std::memcpy(output,packet.data(),sizeof(packet));++flip_wait_packets;}
            return true;
        }
        if(std::strcmp(symbol,"sceVideoOutWaitVblank"))return false;
        if(handle<=0 || static_cast<int32_t>(args[0])!=handle){result=uint32_t(ORBIS_VIDEO_OUT_ERROR_INVALID_HANDLE);return true;}
        struct Guard {Memory *memory;const char *reason=nullptr;} guard{&memory};
        auto interrupt=[](void *opaque)->int {
            auto &g=*static_cast<Guard *>(opaque);
            if((g.reason=guestInterruption(*g.memory)))return 1;
            if(std::chrono::steady_clock::now()>=g.memory->deadline){g.reason="guest execution deadline exceeded";return 1;}
            return 0;
        };
        if(interrupt(&guard)){++interruptions;stopGuest(memory,guard.reason);return true;}
        int32_t status;
        {NativeGpuWait unlocked(wait);status=Libraries::VideoOut::WaitVblankGuarded(handle,interrupt,&guard);}
        ++vblank_waits;
        if(guard.reason || interrupt(&guard)){++interruptions;stopGuest(memory,guard.reason);return true;}
        result=static_cast<uint32_t>(status);return true;
    }
};
