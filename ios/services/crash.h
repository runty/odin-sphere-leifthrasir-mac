#pragma once
#include "../cpu/runtime.h"
#include <cstring>

// No console crash-report thread is available on mobile. Report its creation
// failure rather than accepting an undeliverable callback. Never retain/read
// callback/user-data pointers or generate memory/register dumps.
// ABI/error reference (GPL-2.0-or-later shadPS4 implementation):
// https://github.com/shadps4-emu/shadPS4/tree/a26cac679b3f95fee5b591689adec9e53b62adfb/src/core/libraries/kernel/coredump
struct GuestCrashDiagnostics {
    static constexpr uint32_t invalid=0x81180000u,not_registered=0x81180001u,
        not_in_handler=0x81180003u,thread_create_failed=0x81180004u;
    unsigned registration_attempts=0,user_data_writes_rejected=0;
    bool call(const char *name,const State &state,uint64_t &result) {
        if(!std::strcmp(name,"sceCoredumpRegisterCoredumpHandler")) {
            ++registration_attempts;
            result=!state.gpr.rdi.qword || state.gpr.rsi.qword>=0x20000000 ? invalid : thread_create_failed;
            return true;
        }
        if(!std::strcmp(name,"sceCoredumpUnregisterCoredumpHandler")){result=not_registered;return true;}
        if(!std::strcmp(name,"sceCoredumpWriteUserData")) {
            ++user_data_writes_rejected;result=not_in_handler;return true;
        }
        return false;
    }
};
