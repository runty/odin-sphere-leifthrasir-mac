#include "runtime.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
extern "C" uint64_t __remill_read_memory_64(Memory *,uint64_t);
extern "C" Memory *__remill_write_memory_64(Memory *,uint64_t,uint64_t);
std::atomic<unsigned> checks{0};
void require(bool ok){++checks;if(!ok){std::fprintf(stderr,"striped memory check failed\n");std::abort();}}
constexpr uint64_t a=0xaabbccddeeff1234,b=0x9988776655443210;
void replaceBacking() {
    Memory owner;auto bytes=std::make_unique<std::array<uint8_t,32>>();
    std::memcpy(bytes->data()+3,&a,8);mapRegion(owner,0x200000,bytes->data(),bytes->size());
    std::atomic<bool> start{false},good{true};std::atomic<unsigned> warmed{0};std::atomic<uint64_t> reads{0},faults{0};
    std::vector<std::thread> cpus;
    for(unsigned i=0;i<8;++i)cpus.emplace_back([&]{
        Memory m;m.mappings=owner.mappings;
        require(__remill_read_memory_64(&m,0x200003)==a);
        ++warmed;
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned j=0;j<40000;++j){
            auto value=__remill_read_memory_64(&m,0x200003);
            if(m.fault.empty()){if(value!=a && value!=b)good=false;++reads;}
            else {if(m.fault!="unmapped guest memory")good=false;m.fault={};++faults;}
            if(j%4==0){__remill_write_memory_64(&m,0x200003,b);if(!m.fault.empty()){if(m.fault!="unmapped guest memory" && m.fault!="guest write to protected region")good=false;m.fault={};}}
        }
    });
    // All readers warm their descriptors before mutations begin.
    // The main CPU also shares the same backing-owner registry.
    while(warmed.load()!=8)std::this_thread::yield();
    start.store(true,std::memory_order_release);
    for(unsigned i=0;i<5000;++i){
        // Erase under unmapRegion's exclusive scope, then destroy storage after
        // that scope has ended. A racing warmed descriptor must see invalidation
        // before it can dereference the old allocation. A fresh lookup may fail
        // explicitly during the unmapped interval.
        require(unmapRegion(owner,0x200000));bytes.reset();
        bytes=std::make_unique<std::array<uint8_t,32>>();const uint64_t value=i%2?a:b;
        std::memcpy(bytes->data()+3,&value,8);mapRegion(owner,0x200000,bytes->data(),i%3?32:11,i%2);
    }
    for(auto &cpu:cpus)cpu.join();require(good && reads>0);require(reads+faults==8*40000);
}
static std::atomic<bool> externalEntered{false},externalAllowed{false};
static uint8_t *externalView=nullptr;
uint8_t *physicalAlias(uint64_t address,size_t size,bool writing){
    if(address<0x900000 || size>4096 || address-0x900000>4096-size)return nullptr;
    if(writing){externalEntered.store(true,std::memory_order_release);while(!externalAllowed.load(std::memory_order_acquire))std::this_thread::yield();}
    return externalView+address-0x900000;
}
void distinctPhysicalAliases() {
    // Different host virtual addresses alias one shared physical file page.
    // No guessed virtual-address stripe may split their synchronization.
    char path[]="/tmp/odin-stripe-alias-XXXXXX";int fd=mkstemp(path);require(fd>=0);require(unlink(path)==0);require(ftruncate(fd,4096)==0);
    auto first=static_cast<uint8_t *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
    auto second=static_cast<uint8_t *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
    require(first!=MAP_FAILED && second!=MAP_FAILED && first!=second);require(close(fd)==0);
    externalView=second;std::memcpy(first,&a,8);Memory owner;mapRegion(owner,0x400000,first,4096);
    std::atomic<bool> warm{false},attempted{false},readFinished{false};uint64_t value=0;
    std::thread cached([&]{
        Memory m;m.mappings=owner.mappings;require(__remill_read_memory_64(&m,0x400000)==a);warm=true;
        while(!externalEntered.load(std::memory_order_acquire))std::this_thread::yield();
        attempted=true;value=__remill_read_memory_64(&m,0x400000);readFinished=true;
    });
    while(!warm.load())std::this_thread::yield();
    std::thread external([&]{Memory m;m.external_pointer=physicalAlias;__remill_write_memory_64(&m,0x900000,b);require(m.fault.empty());});
    while(!attempted.load())std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));require(!readFinished.load());
    externalAllowed.store(true,std::memory_order_release);cached.join();external.join();require(value==b);
    require(unmapRegion(owner,0x400000));require(munmap(first,4096)==0 && munmap(second,4096)==0);externalView=nullptr;
}
void crossingBands() {
    void *storage=nullptr;require(posix_memalign(&storage,256*1024,512*1024)==0);auto bytes=static_cast<uint8_t *>(storage);std::memset(bytes,0,512*1024);
    Memory owner;mapRegion(owner,0x10000000,bytes,512*1024);
    constexpr uint64_t address=0x10000000+256*1024-3;
    __remill_write_memory_64(&owner,address,a);std::atomic<bool> good{true};std::vector<std::thread> cpus;
    for(unsigned i=0;i<8;++i)cpus.emplace_back([&,i]{Memory m;m.mappings=owner.mappings;for(unsigned j=0;j<10000;++j){__remill_write_memory_64(&m,address,i%2?a:b);auto value=__remill_read_memory_64(&m,address);if(value!=a && value!=b)good=false;}});
    for(auto &cpu:cpus)cpu.join();require(good);require(unmapRegion(owner,0x10000000));free(storage);
}
int main(){replaceBacking();distinctPhysicalAliases();crossingBands();std::printf("{\"passed\":true,\"checks\":%u,\"replacement_cycles\":5000,\"racing_reads\":320000,\"cross_band_transfers\":160000,\"distinct_virtual_physical_alias\":true}\n",checks.load());}
