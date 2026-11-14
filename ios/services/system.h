#pragma once
#include "../cpu/runtime.h"
#include <cstring>
extern "C" uintptr_t runtime_system_resolve(const char *);
extern "C" int runtime_system_param_supported(int32_t);
namespace GuestSystem {
inline bool call(const char *name,State &s,Memory &memory,uint64_t &result) {
    const bool param=!std::strcmp(name,"sceSystemServiceParamGetInt"),
        status=!std::strcmp(name,"sceSystemServiceGetStatus"),
        safe=!std::strcmp(name,"sceSystemServiceGetDisplaySafeAreaInfo"),
        hide=!std::strcmp(name,"sceSystemServiceHideSplashScreen");
    if(!(param || status || safe || hide)) return false;
    result=0;
    if(hide) {
        result=uint32_t(reinterpret_cast<int32_t (*)()>(runtime_system_resolve(name))());return true;
    }
    const uint64_t target=param ? s.gpr.rsi.qword : s.gpr.rdi.qword;
    if(!target){result=0x80a10003u;return true;}
    const size_t size=status ? 12 : 4;
    auto output=guestPointer(memory,target,size,true);if(!output)return true;
    if(param) {
        const int32_t id=s.gpr.rdi.dword;
        if(!runtime_system_param_supported(id)){stopGuest(memory,"unsupported system parameter");return true;}
        int32_t value=0;
        result=uint32_t(reinterpret_cast<int32_t (*)(int32_t,int32_t *)>(runtime_system_resolve(name))(id,&value));
        if(!result)std::memcpy(output,&value,4);
    } else if(safe) {
        // The game owns a full virtual canvas; UIKit presentation will place
        // that canvas inside the device's physical safe area.
        float ratio=0;
        result=uint32_t(reinterpret_cast<int32_t (*)(float *)>(runtime_system_resolve(name))(&ratio));
        if(!result)std::memcpy(output,&ratio,4);
    } else {
        unsigned char value[12]{};
        result=uint32_t(reinterpret_cast<int32_t (*)(unsigned char *)>(runtime_system_resolve(name))(value));
        if(!result)std::memcpy(output,value,12);
    }
    return true;
}
}
