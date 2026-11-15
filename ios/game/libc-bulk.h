#pragma once
#include "../cpu/runtime.h"
#include <cstring>
#include <limits>

// OpenCrown's checked bulk-libc/RET contract, adapted to Odin's removable
// mappings, external physical aliases, recursive scope and longjmp recovery.
// Caller owns per-session statistics and counts only a completed return.
enum class LibcBulk { Copy, Move, Fill };
inline bool libcBulkReturn(State &state, Memory &memory, LibcBulk kind,
                           uint64_t pc, uint64_t capacity) {
    const uint64_t destination=state.gpr.rdi.qword, source=state.gpr.rsi.qword;
    const uint64_t count=state.gpr.rdx.qword, stack=state.gpr.rsp.qword;
    auto recovery=memory.recovery;
    memory.recovery=nullptr;
    bool completed=false;
    {
        GuestMemoryScope scope;
        do {
            if(!memory.fault.empty())break;
            if(const char *reason=guestInterruption(memory)){stopGuest(memory,reason,pc);break;}
            if(count>capacity || count>std::numeric_limits<size_t>::max()) {
                stopGuest(memory,"libc bulk operation exceeds capacity",pc);break;
            }
            if(stack>UINT64_MAX-8){stopGuest(memory,"libc return stack overflow",pc);break;}
            // Validate RET before any writes. Read its value after the operation:
            // copying/filling over the return slot changes the actual RET value.
            auto returnSlot=guestPointer(memory,stack,8);
            if(!returnSlot)break;
            if(count) {
                auto output=guestPointer(memory,destination,size_t(count),true);
                if(!output)break;
                if(kind==LibcBulk::Fill)std::memset(output,uint8_t(source),size_t(count));
                else {
                    auto input=guestPointer(memory,source,size_t(count));
                    if(!input)break;
                    // Overlapping memcpy is outside the C contract; memmove
                    // gives deterministic bytes without claiming PS4 overlap ABI.
                    std::memmove(output,input,size_t(count));
                }
            }
            // Cancellation is cooperative: committed bytes are not rolled back.
            // A stop here leaves the return registers and counters unchanged.
            if(const char *reason=guestInterruption(memory)){stopGuest(memory,reason,pc);break;}
            uint64_t returnPC;std::memcpy(&returnPC,returnSlot,8);
            state.gpr.rax.qword=destination;
            state.gpr.rip.qword=returnPC;
            state.gpr.rsp.qword=stack+8;
            completed=true;
        } while(false);
    } // Unlock native scopes before resuming a lifted checkpoint.
    memory.recovery=recovery;
    if(!memory.fault.empty())resumeGuestFault(memory);
    return completed;
}
