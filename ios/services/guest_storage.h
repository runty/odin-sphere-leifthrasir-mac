#pragma once
#include "../cpu/runtime.h"
#include "storage.h"
#include <cstring>
#include <string>
#include <vector>
extern "C" int runtime_file_valid(int);
namespace GuestStorage {
template<class T> bool get(Memory &m,uint64_t address,T &value) {
    if (auto p=guestPointer(m,address,sizeof(T))) { std::memcpy(&value,p,sizeof(T)); return true; }
    return false;
}
inline bool path(Memory &m,uint64_t address,std::string &out) {
    for (unsigned i=0;i<1024;++i) {
        auto p=guestPointer(m,address+i,1);
        if (!p) return false;
        if (!*p) return true;
        out.push_back(static_cast<char>(*p));
    }
    stopGuest(m,"guest path exceeds storage bound"); return false;
}
inline bool save(const char *name,State &s,Memory &m,MobileStorage &owner,bool &initialized,uint64_t &result) {
    if (std::strncmp(name,"sceSaveData",11) || std::strstr(name,"Dialog")) return false;
    if (!owner.configured()) { stopGuest(m,"mobile storage roots unavailable"); return true; }
    if (!std::strcmp(name,"sceSaveDataInitialize")) {
        result=uint32_t(Storage::initialize()); if (!result) initialized=true; return true;
    }
    OslSaveTitleId title{}; OslSaveDirName dir{};
    auto nested=[&](auto *guest,auto &local) {
        if (!guest) return true;
        return get(m,reinterpret_cast<uint64_t>(guest),local);
    };
    if (!std::strcmp(name,"sceSaveDataMount")) {
        if (!s.gpr.rdi.qword || !s.gpr.rsi.qword) { result=0x809f0000u; return true; }
        OslSaveMount1 input{}; OslSaveMountResult output{};
        if (!get(m,s.gpr.rdi.qword,input) || !nested(input.title,title) || !nested(input.dir,dir)) return true;
        input.title=input.title ? &title : nullptr; input.dir=input.dir ? &dir : nullptr;
        input.fingerprint=nullptr; // Production backend does not consume it.
        auto target=guestPointer(m,s.gpr.rsi.qword,sizeof(output),true); if (!target) return true;
        result=uint32_t(Storage::mount(&input,&output));
        if (!result) std::memcpy(target,&output,sizeof(output));
        return true;
    }
    if (!std::strcmp(name,"sceSaveDataDirNameSearch")) {
        if (!s.gpr.rdi.qword || !s.gpr.rsi.qword) { result=0x809f0000u; return true; }
        OslSaveSearchCond condition{}; OslSaveSearchResult guest{},query{};
        if (!get(m,s.gpr.rdi.qword,condition) || !get(m,s.gpr.rsi.qword,guest) ||
            !nested(condition.title,title) || !nested(condition.dir,dir)) return true;
        condition.title=condition.title ? &title : nullptr; condition.dir=condition.dir ? &dir : nullptr;
        auto header=guestPointer(m,s.gpr.rsi.qword,sizeof(guest),true); if (!header) return true;
        result=uint32_t(Storage::search(&condition,&query)); if (result) return true;
        const size_t count=std::min(query.hits,guest.names_capacity);
        std::vector<OslSaveDirName> names(guest.names ? count : 0);
        std::vector<OslSaveParam> params(guest.params ? count : 0);
        std::vector<OslSaveSearchInfo> infos(guest.infos ? count : 0);
        uint8_t *names_out=nullptr,*params_out=nullptr,*infos_out=nullptr;
        if (count) {
            if (guest.names) { names_out=guestPointer(m,reinterpret_cast<uint64_t>(guest.names),count*sizeof(OslSaveDirName),true); if (!names_out) return true; }
            if (guest.params) { params_out=guestPointer(m,reinterpret_cast<uint64_t>(guest.params),count*sizeof(OslSaveParam),true); if (!params_out) return true; }
            if (guest.infos) { infos_out=guestPointer(m,reinterpret_cast<uint64_t>(guest.infos),count*sizeof(OslSaveSearchInfo),true); if (!infos_out) return true; }
        }
        query={}; query.names_capacity=static_cast<uint32_t>(count);
        query.names=names.empty() ? nullptr : names.data(); query.params=params.empty() ? nullptr : params.data();
        query.infos=infos.empty() ? nullptr : infos.data();
        result=uint32_t(Storage::search(&condition,&query)); if (result) return true;
        if (names_out) std::memcpy(names_out,names.data(),query.set_count*sizeof(OslSaveDirName));
        if (params_out) std::memcpy(params_out,params.data(),query.set_count*sizeof(OslSaveParam));
        if (infos_out) std::memcpy(infos_out,infos.data(),query.set_count*sizeof(OslSaveSearchInfo));
        guest.hits=query.hits; guest.set_count=query.set_count;
        std::memcpy(header,&guest,sizeof(guest)); // Preserve guest pointer fields.
        return true;
    }
    if (!std::strcmp(name,"sceSaveDataDelete")) {
        if (!s.gpr.rdi.qword) { result=0x809f0000u; return true; }
        OslSaveDelete input{};
        if (!get(m,s.gpr.rdi.qword,input) || !nested(input.title,title) || !nested(input.dir,dir)) return true;
        input.title=input.title ? &title : nullptr; input.dir=input.dir ? &dir : nullptr;
        result=uint32_t(Storage::remove(&input)); return true;
    }
    const bool unmount=!std::strcmp(name,"sceSaveDataUmount"),info=!std::strcmp(name,"sceSaveDataGetMountInfo"),
        param=!std::strcmp(name,"sceSaveDataSetParam"),icon=!std::strcmp(name,"sceSaveDataSaveIcon");
    if (!unmount && !info && !param && !icon) return false;
    OslSaveMountPoint point{};
    if (s.gpr.rdi.qword && !get(m,s.gpr.rdi.qword,point)) return true;
    const auto *p=s.gpr.rdi.qword ? &point : nullptr;
    if (unmount) { result=uint32_t(Storage::unmount(p)); return true; }
    if (info) {
        if (!s.gpr.rsi.qword) { result=uint32_t(Storage::info(p,nullptr)); return true; }
        OslSaveSearchInfo output{};
        auto target=guestPointer(m,s.gpr.rsi.qword,sizeof(output),true); if (!target) return true;
        result=uint32_t(Storage::info(p,&output)); if (!result) std::memcpy(target,&output,sizeof(output));
        return true;
    }
    if (param) {
        const void *data=nullptr; uint8_t dummy=0;
        if (s.gpr.rdx.qword) {
            data=s.gpr.rcx.qword ? guestPointer(m,s.gpr.rdx.qword,s.gpr.rcx.qword) : &dummy;
            if (!data) return true;
        }
        result=uint32_t(Storage::param(p,s.gpr.rsi.dword,data,s.gpr.rcx.qword)); return true;
    }
    if (!s.gpr.rsi.qword) { result=uint32_t(Storage::icon(p,nullptr)); return true; }
    OslSaveIcon input{};
    if (!get(m,s.gpr.rsi.qword,input)) return true;
    if (input.data_size>input.buffer_size) { result=0x809f0000u; return true; }
    uint8_t dummy=0;
    if (input.buffer) {
        input.buffer=input.data_size ? guestPointer(m,reinterpret_cast<uint64_t>(input.buffer),input.data_size) : &dummy;
        if (!input.buffer) return true;
    }
    result=uint32_t(Storage::icon(p,&input)); return true;
}
inline bool file(const char *name,State &s,Memory &m,MobileStorage &owner,uint64_t &result) {
    if (std::strncmp(name,"sceKernel",9)) return false;
    const uintptr_t function=runtime_file_resolve(name);
    if (!function) return false;
    if (!owner.configured()) { stopGuest(m,"mobile storage roots unavailable"); return true; }
    const int fd=static_cast<int>(s.gpr.rdi.dword);
    auto unary=[&]() { result=static_cast<uint64_t>(reinterpret_cast<int64_t (*)(int)>(function)(fd)); };
    if (!std::strcmp(name,"sceKernelClose") || !std::strcmp(name,"sceKernelFsync")) { unary(); return true; }
    if (!std::strcmp(name,"sceKernelLseek")) {
        result=reinterpret_cast<int64_t (*)(int,int64_t,int)>(function)(fd,s.gpr.rsi.qword,s.gpr.rdx.dword); return true;
    }
    if (!std::strcmp(name,"sceKernelFtruncate")) {
        result=reinterpret_cast<int64_t (*)(int,int64_t)>(function)(fd,s.gpr.rsi.qword); return true;
    }
    const bool read=!std::strcmp(name,"sceKernelRead"),write=!std::strcmp(name,"sceKernelWrite"),
        pread=!std::strcmp(name,"sceKernelPread"),pwrite=!std::strcmp(name,"sceKernelPwrite");
    if (read || write || pread || pwrite) {
        void *bytes=nullptr;
        if (runtime_file_valid(fd) && s.gpr.rdx.qword) {
            bytes=guestPointer(m,s.gpr.rsi.qword,s.gpr.rdx.qword,read || pread); if (!bytes) return true;
        }
        if (pread || pwrite)
            result=reinterpret_cast<int64_t (*)(int,void *,uint64_t,int64_t)>(function)(fd,bytes,s.gpr.rdx.qword,s.gpr.rcx.qword);
        else result=reinterpret_cast<int64_t (*)(int,void *,uint64_t)>(function)(fd,bytes,s.gpr.rdx.qword);
        return true;
    }
    if (!std::strcmp(name,"sceKernelFstat")) {
        OslFileStat value{};
        if (!runtime_file_valid(fd) || !s.gpr.rsi.qword) {
            result=reinterpret_cast<int64_t (*)(int,OslFileStat *)>(function)(fd,nullptr); return true;
        }
        auto target=guestPointer(m,s.gpr.rsi.qword,sizeof(value),true); if (!target) return true;
        result=reinterpret_cast<int64_t (*)(int,OslFileStat *)>(function)(fd,&value);
        if (!result) std::memcpy(target,&value,sizeof(value)); return true;
    }
    if (!std::strcmp(name,"sceKernelGetdirentries")) {
        uint8_t dummy=0; char *bytes=nullptr; int64_t base=0;
        uint8_t *base_out=nullptr;
        if (runtime_file_valid(fd)) {
            if (s.gpr.rsi.qword) bytes=s.gpr.rdx.qword>=512 ? reinterpret_cast<char *>(guestPointer(m,s.gpr.rsi.qword,s.gpr.rdx.qword,true)) : reinterpret_cast<char *>(&dummy);
            if (!m.fault.empty()) return true;
            if (s.gpr.rcx.qword) { base_out=guestPointer(m,s.gpr.rcx.qword,8,true); if (!base_out) return true; }
        }
        result=reinterpret_cast<int64_t (*)(int,char *,uint64_t,int64_t *)>(function)(fd,bytes,s.gpr.rdx.qword,base_out ? &base : nullptr);
        if (static_cast<int64_t>(result)>=0 && base_out) std::memcpy(base_out,&base,8);
        return true;
    }
    std::string first,second;
    if (s.gpr.rdi.qword && !path(m,s.gpr.rdi.qword,first)) return true;
    const char *a=s.gpr.rdi.qword ? first.c_str() : nullptr;
    if (!std::strcmp(name,"sceKernelOpen"))
        result=reinterpret_cast<int64_t (*)(const char *,int,int)>(function)(a,s.gpr.rsi.dword,s.gpr.rdx.dword);
    else if (!std::strcmp(name,"sceKernelStat")) {
        OslFileStat value{};
        auto target=s.gpr.rsi.qword ? guestPointer(m,s.gpr.rsi.qword,sizeof(value),true) : nullptr;
        if (!m.fault.empty()) return true;
        result=reinterpret_cast<int64_t (*)(const char *,OslFileStat *)>(function)(a,target ? &value : nullptr);
        if (!result) std::memcpy(target,&value,sizeof(value));
    } else if (!std::strcmp(name,"sceKernelRename")) {
        if (s.gpr.rsi.qword && !path(m,s.gpr.rsi.qword,second)) return true;
        result=reinterpret_cast<int64_t (*)(const char *,const char *)>(function)(a,s.gpr.rsi.qword ? second.c_str() : nullptr);
    } else if (!std::strcmp(name,"sceKernelMkdir"))
        result=reinterpret_cast<int64_t (*)(const char *,int)>(function)(a,s.gpr.rsi.dword);
    else if (!std::strcmp(name,"sceKernelTruncate"))
        result=reinterpret_cast<int64_t (*)(const char *,int64_t)>(function)(a,s.gpr.rsi.qword);
    else if (!std::strcmp(name,"sceKernelCheckReachability"))
        result=uint32_t(reinterpret_cast<int32_t (*)(const char *)>(function)(a));
    else if (!std::strcmp(name,"sceKernelRmdir") || !std::strcmp(name,"sceKernelUnlink"))
        result=reinterpret_cast<int64_t (*)(const char *)>(function)(a);
    else return false;
    return true;
}
}
