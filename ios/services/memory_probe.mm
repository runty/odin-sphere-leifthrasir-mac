// Synthetic checks of the actual mobile allocator; never a game-frame result.
#import <Foundation/Foundation.h>
#include "memory.h"
#include <vector>
#include <cstring>
#include <stdexcept>
extern "C" int runtime_memory_write_backing(uintptr_t,const void *,uint64_t);
extern "C" NSDictionary *osl_mobile_memory_report() {
    unsigned checks=0; uint64_t window=0,backing=0;
    const char *failure="native allocation/alias contract failed";
    try {
        auto require=[&](bool ok) { if(!ok)throw std::runtime_error(failure);++checks; };
        {
            DirectMemory owner;
            constexpr uint64_t page=16384,size=8*1024*1024;
            int64_t physical=-1;void *first=nullptr,*alias=nullptr;
            failure="direct physical allocation";
            require(DirectMemory::capacity()==uint64_t(5056)*1024*1024);
            require(!DirectMemory::allocate(0,DirectMemory::capacity(),size,page,0,&physical));
            window=DirectMemory::windowSize();backing=DirectMemory::backingViewSize();
            require(window>=(uint64_t(2)<<30) && window<=(uint64_t(8)<<30) && backing==0);
            failure="shared direct mappings";
            require(!DirectMemory::map(&first,size,3,0,physical,page));
            require(!DirectMemory::map(&alias,2*page,3,0,physical,page));
            const uintptr_t address=reinterpret_cast<uintptr_t>(first);
            require(address && address<(uint64_t(1)<<40) && size<=(uint64_t(1)<<40)-address);
            std::vector<uint8_t> payload(5*1024*1024+37);
            for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*17+i/991);
            failure="bounded writeback and read-only protection";
            require(!DirectMemory::protect(first,6*1024*1024,1));
            require(runtime_memory_write_backing(address+13,payload.data(),payload.size())==1);
            require(std::memcmp(static_cast<uint8_t *>(first)+13,payload.data(),payload.size())==0);
            require(std::memcmp(static_cast<uint8_t *>(alias)+13,payload.data(),2*page-13)==0);
            require(!DirectMemory::pointer(address,1,true) && DirectMemory::pointer(address,1,false)==first);
            failure="release and zero-on-reuse";
            require(!DirectMemory::release(physical,size));
            require(!DirectMemory::pointer(address,1,false) && !DirectMemory::pointer(reinterpret_cast<uintptr_t>(alias),1,false));
            require(!DirectMemory::allocate(0,DirectMemory::capacity(),2*page,page,0,&physical));
            void *reused=nullptr;require(!DirectMemory::map(&reused,2*page,3,0,physical,page));
            bool zero=true;for(unsigned i=0;i<2*page;++i)if(static_cast<uint8_t *>(reused)[i]){zero=false;break;}
            require(zero);
            require(!DirectMemory::release(physical,2*page));
            require(DirectMemory::allocated()==0);
        }
        failure="allocator teardown";
        require(DirectMemory::allocated()==0 && DirectMemory::windowSize()==0 && DirectMemory::backingViewSize()==0);
        return @{@"passed":@YES,@"checks":@(checks),@"window_bytes":@(window),@"permanent_backing_bytes":@(backing),@"writeback_bytes":@(5*1024*1024+37),@"cleanup_complete":@YES,@"game_executed":@NO,@"game_frames":@NO};
    } catch(const std::exception &) {
        return @{@"passed":@NO,@"checks":@(checks),@"error":@(failure),@"window_bytes":@(window),@"permanent_backing_bytes":@(backing),@"cleanup_complete":@(DirectMemory::allocated()==0 && DirectMemory::windowSize()==0),@"game_executed":@NO,@"game_frames":@NO};
    }
}
