#pragma once
#include "../cpu/runtime.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>

// Immutable after startup. Image/template bytes outlive all thread storage.
class ModuleTlsTemplates {
public:
    struct Module { const uint8_t *data=nullptr;size_t filesz=0,memsz=0; };
private:
    std::array<Module,8> modules_{};
public:
    bool define(uint64_t id,const uint8_t *data,size_t filesz,size_t memsz) {
        if(id<2 || id>=modules_.size() || modules_[id].memsz || !memsz || memsz>1024*1024 ||
           filesz>memsz || (filesz && !data))return false;
        modules_[id]={data,filesz,memsz};return true;
    }
    const Module *find(uint64_t id)const {
        return id<modules_.size() && modules_[id].memsz ? &modules_[id] : nullptr;
    }
};

// One instance per CPU/host worker. Mappings are visible to all CPUs sharing
// Memory::mappings; bytes remain owned until the worker/session has quiesced.
class ThreadStorage {
    struct Free {void operator()(uint8_t *p)const{std::free(p);}};
    struct Allocation {uint64_t address=0;size_t size=0;std::unique_ptr<uint8_t,Free> bytes;};
    Memory &memory_;
    const ModuleTlsTemplates &templates_;
    bool identity_;
    uint64_t next_,reserved_=0;
    std::array<Allocation,9> allocations_{}; // stack in slot 0; TLS in slots 2..7
    bool allocate(unsigned slot,size_t size) {
        const char *failure=nullptr;
        {
            std::unique_ptr<uint8_t,Free> bytes;
            if(!size || size>128*1024*1024)failure="guest thread storage budget exceeded";
            else {
                const size_t reserved=(size+16383)&~size_t(16383);
                void *raw=nullptr;
                if(reserved_+reserved>256*1024*1024)failure="guest thread storage budget exceeded";
                else if(posix_memalign(&raw,16384,reserved))failure="guest thread storage allocation failed";
                else {
                    bytes.reset(static_cast<uint8_t *>(raw));std::memset(raw,0,reserved);
                    const uint64_t address=identity_ ? reinterpret_cast<uint64_t>(raw) : next_;
                    if(address>=(uint64_t(1)<<40) || reserved>(uint64_t(1)<<40)-address)
                        failure="guest thread storage exceeds address range";
                    else try {
                        mapRegion(memory_,address,bytes.get(),size);
                        allocations_[slot].bytes=std::move(bytes);allocations_[slot].address=address;allocations_[slot].size=size;
                        next_+=reserved;reserved_+=reserved;
                    } catch(...) {failure="guest thread storage mapping failed";}
                }
            }
        } // Do not longjmp across temporary allocation owners.
        if(failure){stopGuest(memory_,failure);return false;}return true;
    }
public:
    ThreadStorage(Memory &memory,const ModuleTlsTemplates &templates,bool identity,uint64_t virtual_base)
        :memory_(memory),templates_(templates),identity_(identity),next_(virtual_base){}
    ThreadStorage(const ThreadStorage &)=delete;
    ThreadStorage &operator=(const ThreadStorage &)=delete;
    ~ThreadStorage(){for(const auto &a:allocations_)if(a.bytes)unmapRegion(memory_,a.address);}
    bool makeStack(size_t size) {
        if(size<16384 || allocations_[0].bytes){stopGuest(memory_,"invalid guest thread stack");return false;}
        return allocate(0,size);
    }
    uint64_t stackAddress()const{return allocations_[0].address;}
    size_t stackSize()const{return allocations_[0].size;}
    size_t allocatedBytes()const{return reserved_;}
    uint64_t tls(uint64_t module,uint64_t offset) {
        const auto *source=templates_.find(module);
        if(!source || offset>=source->memsz){stopGuest(memory_,"unsupported TLS module or offset");return 0;}
        auto &block=allocations_[module];
        if(!block.bytes) {
            if(!allocate(module,source->memsz))return 0;
            if(source->filesz)std::memcpy(block.bytes.get(),source->data,source->filesz);
        }
        return block.address+offset;
    }
};
namespace GuestTls {
inline bool call(const char *name,State &state,Memory &memory,ThreadStorage &storage,uint64_t &result) {
    if(std::strcmp(name,"__tls_get_addr"))return false;
    auto pointer=guestPointer(memory,state.gpr.rdi.qword,16);if(!pointer)return true;
    uint64_t index[2];std::memcpy(index,pointer,sizeof(index));
    result=storage.tls(index[0],index[1]);return true;
}
}
