#include "thread_storage.h"
#include "tls-fixture.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>
static unsigned checks;
static void require(bool ok){if(!ok){std::fprintf(stderr,"thread storage check %u failed\n",checks+1);std::abort();}++checks;}
int main(int argc,char **argv) {
    require(argc==2);std::ifstream input(argv[1],std::ios::binary|std::ios::ate);require(bool(input));
    std::vector<uint8_t> image(input.tellg());input.seekg(0);input.read(reinterpret_cast<char *>(image.data()),image.size());require(bool(input));
    ModuleTlsTemplates templates;
    for(const auto &m:tls_modules){require(m.start<=image.size() && m.filesz<=image.size()-m.start);require(templates.define(m.id,image.data()+m.start,m.filesz,m.memsz));}
    require(!templates.define(2,image.data(),1,1));require(!templates.define(1,image.data(),1,1));
    require(!templates.define(8,image.data(),1,1));require(!templates.define(4,nullptr,1,1));
    require(!templates.define(4,image.data(),2,1));require(!templates.define(4,image.data(),1,1024*1024+1));
    Memory owner,observer;observer.mappings=owner.mappings;uint64_t stack=0,tls=0;
    {ThreadStorage storage(owner,templates,false,0x80000000);
     require(storage.makeStack(65537));stack=storage.stackAddress();
     require(stack==0x80000000 && storage.stackSize()==65537 && storage.allocatedBytes()==81920);
     auto bytes=guestPointer(observer,stack,65537,true);require(bytes && std::all_of(bytes,bytes+65537,[](uint8_t x){return !x;}));
     require(!guestPointer(observer,stack+65537,1));observer.fault={};
     for(const auto &m:tls_modules) {
       const auto address=storage.tls(m.id,0);require(address && address%16384==0);
       auto block=guestPointer(observer,address,m.memsz,true);require(block);
       require(!std::memcmp(block,image.data()+m.start,m.filesz));
       require(std::all_of(block+m.filesz,block+m.memsz,[](uint8_t x){return !x;}));
       require(storage.tls(m.id,m.memsz-1)==address+m.memsz-1);
       block[m.memsz-1]=0xa5;require(*guestPointer(owner,storage.tls(m.id,m.memsz-1),1)==0xa5);
       require(!storage.tls(m.id,m.memsz) && owner.fault=="unsupported TLS module or offset");owner.fault={};
       if(m.id==2)tls=address;
     }
     const auto allocated=storage.allocatedBytes();require(!storage.tls(99,0) && storage.allocatedBytes()==allocated);owner.fault={};
     require(!storage.tls(2,UINT64_MAX) && storage.allocatedBytes()==allocated);owner.fault={};
     require(!storage.makeStack(16384) && storage.allocatedBytes()==allocated);owner.fault={};
     std::array<uint8_t,64> index{};mapRegion(owner,0x1000,index.data(),index.size());
     uint64_t pair[]={2,16};std::memcpy(index.data()+1,pair,16);State state{};state.gpr.rdi.qword=0x1001;uint64_t result=0;
     require(GuestTls::call("__tls_get_addr",state,owner,storage,result) && result==tls+16);
     require(index[0]==0 && index[17]==0);
     state.gpr.rdi.qword=0x1031;GuestTls::call("__tls_get_addr",state,owner,storage,result);require(!owner.fault.empty());owner.fault={};
     require(!GuestTls::call("unsupported",state,owner,storage,result));
    }
    require(!guestPointer(observer,stack,1));observer.fault={};require(!guestPointer(observer,tls,1));observer.fault={};
    {ThreadStorage invalid(owner,templates,false,(uint64_t(1)<<40)-8192);require(!invalid.makeStack(16384));owner.fault={};}
    std::atomic<unsigned> arrived{0},passed{0};std::atomic<bool> release{false};
    std::array<uint64_t,4> addresses{};std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&,i] {
        Memory memory;memory.mappings=owner.mappings;ThreadStorage storage(memory,templates,false,0x90000000ull+i*0x10000000ull);
        bool good=storage.makeStack(32768);
        const auto address=storage.tls(2,0);auto block=guestPointer(memory,address,1168,true);good&=block!=nullptr;
        if(block){good&=!std::memcmp(block,image.data()+tls_modules[0].start,416);block[700]=uint8_t(i+1);}
        addresses[i]=address;++arrived;
        while(!release.load(std::memory_order_acquire))std::this_thread::yield();
        if(block)good&=block[700]==i+1;
        if(good && memory.fault.empty())++passed;
    });
    while(arrived!=4)std::this_thread::yield();
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<i;++j)require(addresses[i]!=addresses[j]);
    release.store(true,std::memory_order_release);for(auto &worker:workers)worker.join();require(passed==4);
    for(auto address:addresses){require(!guestPointer(observer,address,1));observer.fault={};}
    bool identity=false,sanitizer_guard=false;
#ifdef __aarch64__
    {ThreadStorage native(owner,templates,true,0xd0000000);const bool mapped=native.makeStack(65536);
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
     if(!mapped){require(owner.fault=="guest thread storage exceeds address range" && native.allocatedBytes()==0);owner.fault={};sanitizer_guard=true;}
     else
#endif
     {
     require(mapped);
     auto address=native.tls(2,0);require(address && reinterpret_cast<uint64_t>(guestPointer(observer,address,1168))==address);
     require(native.stackAddress()%16384==0 && reinterpret_cast<uint64_t>(guestPointer(observer,native.stackAddress(),65536))==native.stackAddress());identity=true;
     }}
#endif
    std::printf("{\"thread_storage_checks\":%u,\"real_libc_tls_bytes\":1168,\"real_fios_tls_bytes\":64,\"native_workers\":4,\"identity_checked\":%s,\"identity_sanitizer_guard\":%s,\"failed\":0,\"game_frames\":false}\n",checks,identity ? "true" : "false",sanitizer_guard ? "true" : "false");
}
