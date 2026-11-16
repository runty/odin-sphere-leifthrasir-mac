// SPDX-License-Identifier: GPL-2.0-or-later
// Real host protection/fault recovery using the renderer's actual page policy.
// This tests policy and mprotect, not cache invalidation or a complete renderer.
#include <algorithm>
#include <bit>
#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include "video_core/page_manager.h"
#include "common/signal_context.h"
static sigjmp_buf recovery;
static volatile sig_atomic_t faults=0, failed=0, failure_errno=0, direction_error=0;
static volatile sig_atomic_t write_case=0;
static void fault(int, siginfo_t *info, void *context) {
    ++faults;
    if(Common::IsWriteError(context)!=bool(write_case))direction_error=1;
    const auto address=reinterpret_cast<uintptr_t>(info->si_addr);
    if(mprotect(reinterpret_cast<void*>(VideoCore::PageManager::GetPageAddr(address)),
                VideoCore::TRACKER_BYTES_PER_PAGE,write_case ? PROT_READ|PROT_WRITE : PROT_READ)) {
        failure_errno=errno;failed=1;siglongjmp(recovery,1);
    }
    if(faults>2){failed=2;siglongjmp(recovery,1);}
}
int main() {
    const size_t host=size_t(sysconf(_SC_PAGESIZE)), size=65536;
    auto *span=static_cast<unsigned char*>(mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));
    if(span==MAP_FAILED || host>16384 || size%host)return 2;
    struct sigaction action{},old_bus{},old_segv{};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
    if(sigaction(SIGBUS,&action,&old_bus)||sigaction(SIGSEGV,&action,&old_segv))return 3;
    unsigned cases=0, unresolved=0, bad=0;
    for(unsigned writing=0;writing<2;++writing)for(unsigned subpage=0;subpage<4;++subpage) {
        if(mprotect(span,size,PROT_READ|PROT_WRITE))return 4;
        std::memset(span,0x37,size);
        unsigned char transfer[64];std::memset(transfer,0x6b,sizeof(transfer));
        if(mprotect(span,size,writing?PROT_READ:PROT_NONE))return 5;
        faults=failed=failure_errno=direction_error=0;write_case=writing;
        auto *target=span+16384+subpage*4096+128;
        if(sigsetjmp(recovery,1)==0) {
            if(writing)std::memmove(target,transfer,sizeof(transfer));
            else std::memmove(transfer,target,sizeof(transfer));
        }
        ++cases;if(failed)++unresolved;
        if(direction_error || !faults || faults>2 || (failed && (failed!=1 || failure_errno!=EINVAL)))++bad;
        if(mprotect(span,size,PROT_READ|PROT_WRITE))return 6;
        if(!failed) {
            if(writing){for(auto v:transfer)if(v!=0x6b)++bad;for(unsigned i=0;i<64;++i)if(target[i]!=0x6b)++bad;}
            else for(auto v:transfer)if(v!=0x37)++bad;
        }
        std::printf("CASE write=%u subpage=%u faults=%d unresolved=%d errno=%d direction_error=%d\n",writing,subpage,int(faults),int(failed),int(failure_errno),int(direction_error));
    }
    if(sigaction(SIGBUS,&old_bus,nullptr)||sigaction(SIGSEGV,&old_segv,nullptr)||munmap(span,size))return 7;
    std::printf("RESULT:{\"host_page_bytes\":%zu,\"tracker_page_bytes\":%llu,\"cases\":%u,\"unresolved_faults\":%u,\"other_failures\":%u,\"renderer_executed\":false}\n",host,(unsigned long long)VideoCore::TRACKER_BYTES_PER_PAGE,cases,unresolved,bad);
    return bad?1:0;
}
