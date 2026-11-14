#include "system.h"
#include <array>
#include <cstdio>
#include <cstdlib>
extern "C" void runtime_set_language(int);
static unsigned checks;
static void require(bool value) { if(!value){std::fprintf(stderr,"system check %u failed\n",checks+1);std::abort();}++checks; }
int main() {
    auto param=reinterpret_cast<int32_t (*)(int32_t,int32_t *)>(runtime_system_resolve("sceSystemServiceParamGetInt"));
    auto safe=reinterpret_cast<int32_t (*)(float *)>(runtime_system_resolve("sceSystemServiceGetDisplaySafeAreaInfo"));
    auto status=reinterpret_cast<int32_t (*)(unsigned char *)>(runtime_system_resolve("sceSystemServiceGetStatus"));
    require(param && safe && status && !runtime_system_resolve("unknown"));
    require(uint32_t(param(1,nullptr))==0x80a10003u);
    int32_t value=0;
    unsetenv("OSL_LANGUAGE");runtime_set_language(2);require(param(1,&value)==0 && value==2);
    setenv("OSL_LANGUAGE","9",1);require(param(1,&value)==0 && value==9);
    unsetenv("OSL_LANGUAGE");runtime_set_language(1);
    for(int id:{1,2,3,4,5,7,1000}) {
        require(runtime_system_param_supported(id));require(param(id,&value)==0);
        require(id==4 ? value>=-1440 && value<=1440 : value==((id==5 || id==7) ? 0 : 1));
    }
    require(!runtime_system_param_supported(-1) && !runtime_system_param_supported(6));
    float ratio=0;require(safe(&ratio)==0 && ratio==1.0f);
    require(uint32_t(safe(nullptr))==0x80a10003u && uint32_t(status(nullptr))==0x80a10003u);
    unsigned char native[14];std::memset(native,0x7b,sizeof(native));
    require(status(native+1)==0 && native[0]==0x7b && native[13]==0x7b);
    for(unsigned i=1;i<13;++i)require(native[i]==0);
    Memory memory;State state{};
    std::array<uint8_t,256> bytes{},protected_bytes{};
    std::fill(bytes.begin(),bytes.end(),0x7b);std::fill(protected_bytes.begin(),protected_bytes.end(),0x7b);
    mapRegion(memory,0x1000,bytes.data(),bytes.size());mapRegion(memory,0x2000,protected_bytes.data(),protected_bytes.size(),false);
    auto call=[&](const char *name,uint64_t first=0,uint64_t second=0) {
        state=State{};state.gpr.rdi.qword=first;state.gpr.rsi.qword=second;memory.fault={};uint64_t result=0;
        require(GuestSystem::call(name,state,memory,result));return result;
    };
    require(call("sceSystemServiceGetDisplaySafeAreaInfo")==0x80a10003u);
    require(call("sceSystemServiceGetDisplaySafeAreaInfo",0x1001)==0);
    std::memcpy(&ratio,bytes.data()+1,4);require(ratio==1.0f && bytes[0]==0x7b && bytes[5]==0x7b);
    call("sceSystemServiceGetDisplaySafeAreaInfo",0x10ff);require(!memory.fault.empty());
    call("sceSystemServiceGetDisplaySafeAreaInfo",0x2000);require(!memory.fault.empty() && protected_bytes[0]==0x7b);
    require(call("sceSystemServiceGetStatus",0x1011)==0);
    require(bytes[0x10]==0x7b && bytes[0x1d]==0x7b);
    for(unsigned i=0x11;i<0x1d;++i)require(bytes[i]==0);
    require(call("sceSystemServiceParamGetInt",1,0x1021)==0);
    std::memcpy(&value,bytes.data()+0x21,4);require(value==1 && bytes[0x20]==0x7b && bytes[0x25]==0x7b);
    call("sceSystemServiceParamGetInt",6,0x1021);require(memory.fault=="unsupported system parameter");
    std::memcpy(&value,bytes.data()+0x21,4);require(value==1);
    require(call("sceSystemServiceHideSplashScreen")==0);
    uint64_t result=0;require(!GuestSystem::call("sceSystemServiceLaunchWebBrowser",state,memory,result));
    std::printf("{\"system_checks\":%u,\"failed\":0,\"game_frames\":false}\n",checks);
}
