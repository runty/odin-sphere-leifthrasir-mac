// Opt-in OS capability diagnostics. No executable memory or guest-state edits.
#import <Foundation/Foundation.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

extern "C" NSDictionary *osl_mobile_vm_report() {
    const uint64_t gib=uint64_t(1024)*1024*1024;
    NSMutableArray *windows=[NSMutableArray array];
    for(uint64_t size:{8*gib,4*gib,2*gib,gib}) {
        errno=0;
        void *base=mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
        const bool mapped=base!=MAP_FAILED;
        const int error=mapped?0:errno;
        const uintptr_t address=mapped?reinterpret_cast<uintptr_t>(base):0;
        const bool fits=mapped && address && address<(uint64_t(1)<<40) && size<=(uint64_t(1)<<40)-address;
        const bool cleaned=!mapped || munmap(base,size)==0;
        [windows addObject:@{@"requested_bytes":@(size),@"reserved":@(mapped),@"errno":@(error),@"fits_gpu_address_bits":@(fits),@"cleanup_ok":@(cleaned)}];
    }
    // The current default direct pool (5056 MiB) and flexible backing (1024
    // MiB). ftruncate is sparse; no game assets or save files are opened.
    const uint64_t backing_bytes=uint64_t(6080)*1024*1024;
    NSMutableArray *pairs=[NSMutableArray array];
    FILE *file=tmpfile();
    if(!file)return @{@"windows":windows,@"backing_stage":@"temporary sparse file",@"backing_errno":@(errno),@"game_frames":@NO};
    if(ftruncate(fileno(file),static_cast<off_t>(backing_bytes))) {
        const int error=errno;fclose(file);
        return @{@"windows":windows,@"backing_stage":@"sparse sizing",@"backing_errno":@(error),@"game_frames":@NO};
    }
    for(uint64_t window:{uint64_t(0),gib,2*gib}) {
        errno=0;
        void *reserved=window?mmap(nullptr,window,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0):nullptr;
        const bool have_window=!window || reserved!=MAP_FAILED;
        int error=have_window?0:errno;
        void *backing=MAP_FAILED;
        if(have_window) {
            backing=mmap(nullptr,backing_bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_NORESERVE,fileno(file),0);
            error=backing==MAP_FAILED?errno:0;
        }
        bool cleaned=true;
        if(backing!=MAP_FAILED)cleaned=munmap(backing,backing_bytes)==0;
        if(window && have_window)cleaned=(munmap(reserved,window)==0) && cleaned;
        [pairs addObject:@{@"window_bytes":@(window),@"window_reserved":@(have_window),@"backing_bytes":@(backing_bytes),@"backing_mapped":@(backing!=MAP_FAILED),@"errno":@(error),@"cleanup_ok":@(cleaned)}];
    }
    fclose(file);
    return @{@"windows":windows,@"backing_pairs":pairs,@"sparse_data_written_bytes":@0,@"game_frames":@NO};
}
