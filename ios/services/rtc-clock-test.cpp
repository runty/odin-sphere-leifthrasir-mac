#include "guest_clock.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

// RTC never invokes the unrelated kernel-errno converter. Fail if it does.
extern "C" int32_t runtime_guest_errno(int) { std::abort(); }

int main() {
    unsigned checks=0;
    auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("RTC check "+std::to_string(checks));};
    Memory memory;State state{};
    std::array<uint8_t,80> bytes{},protected_bytes{};
    mapRegion(memory,0x20000000,bytes.data(),bytes.size());
    mapRegion(memory,0x30000000,protected_bytes.data(),protected_bytes.size(),false);
    auto call=[&](const char *name,uint64_t pointer,uint64_t &result) {
        memory.fault={};memory.fault_address=0;memory.missing_pc=0;
        state.gpr.rdi.qword=pointer;state.gpr.rsp.qword=0x12345678;
        const auto before=state;
        GuestMemoryScope scope;
        const bool handled=GuestClock::call(name,state,memory,result);
        require(!std::memcmp(&state,&before,sizeof(state)));
        return handled;
    };
    for(const char *zone: {"UTC0","PST8PDT","UTC-14"}) {
        require(!setenv("TZ",zone,1));tzset();
        for(size_t offset: {size_t(1),size_t(3),size_t(7),size_t(16),size_t(47),size_t(64)}) {
            bytes.fill(0xa5);uint64_t result=UINT64_MAX;
            const auto before=std::time(nullptr);
            require(call("sceRtcGetCurrentClockLocalTime",0x20000000+offset,result));
            const auto after=std::time(nullptr);
            require(result==0 && memory.fault.empty());
            uint16_t date[8]{};std::memcpy(date,bytes.data()+offset,sizeof(date));
            uint32_t usec=0;std::memcpy(&usec,date+6,sizeof(usec));
            require(date[0]>=1970 && date[1]>=1 && date[1]<=12 && date[2]>=1 && date[2]<=31);
            require(date[3]<=23 && date[4]<=59 && date[5]<=60 && usec<1000000);
            std::tm decoded{};decoded.tm_year=date[0]-1900;decoded.tm_mon=date[1]-1;decoded.tm_mday=date[2];
            decoded.tm_hour=date[3];decoded.tm_min=date[4];decoded.tm_sec=date[5];decoded.tm_isdst=-1;
            const auto instant=std::mktime(&decoded);
            require(instant>=before && instant<=after);
            bool guarded=true;
            for(size_t i=0;i<bytes.size();++i)if(i<offset || i>=offset+16)guarded&=bytes[i]==0xa5;
            require(guarded);
        }
    }
    uint64_t result=UINT64_MAX;bytes.fill(0x7b);const auto untouched=bytes;
    require(call("sceRtcGetCurrentClockLocalTime",0,result));
    require(result==0x80b50002u && memory.fault.empty() && bytes==untouched);
    for(uint64_t address: {UINT64_C(0x20000041),UINT64_C(0x20000050),UINT64_MAX,UINT64_C(0x30000000)}) {
        require(call("sceRtcGetCurrentClockLocalTime",address,result));
        require(!memory.fault.empty() && memory.fault_address==address && bytes==untouched);
        require(protected_bytes==std::array<uint8_t,80>{});
    }
    for(const char *name: {"sceRtcGetCurrentClock","sceRtcGetCurrentClockLocalTime ","unknown"}) {
        require(!call(name,0x20000001,result));
        require(memory.fault.empty() && bytes==untouched);
    }
    require(GuestClock::rtcLocalClock(nullptr)==int32_t(0x80b50002));
    std::cout<<"{\"rtc_checks\":"<<checks<<",\"failed\":0,\"game_executed\":false}\n";
}
