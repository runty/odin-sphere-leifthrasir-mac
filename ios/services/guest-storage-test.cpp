#include "guest_storage.h"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
static unsigned checks;
static void require(bool value) {
    if (!value) throw std::runtime_error("guest storage check "+std::to_string(checks+1));
    ++checks;
}
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    try {
        const std::filesystem::path root=argv[1]; const bool create=std::string(argv[2])=="create";
        MobileStorage owner; owner.configure((root/"App0").string(),(root/"User").string());
        Memory memory;
        std::vector<uint8_t> bytes(0x10000,0x7b);
        const uint64_t base=create ? 0x20000000 : 0x30000000;
        mapRegion(memory,base,bytes.data(),bytes.size());
        std::vector<uint8_t> protected_bytes(128,0);
        mapRegion(memory,base+0x40000000,protected_bytes.data(),protected_bytes.size(),false);
        auto put=[&](uint64_t offset,const auto &value) { std::memcpy(bytes.data()+offset,&value,sizeof(value)); };
        auto text=[&](uint64_t offset,const char *value) { std::strcpy(reinterpret_cast<char *>(bytes.data()+offset),value); };
        bool initialized=false;
        auto call=[&](const char *name,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0) {
            memory.fault={}; State state{}; state.gpr.rdi.qword=a; state.gpr.rsi.qword=b;
            state.gpr.rdx.qword=c; state.gpr.rcx.qword=d;
            uint64_t value=0;
            require(GuestStorage::save(name,state,memory,owner,initialized,value) || GuestStorage::file(name,state,memory,owner,value));
            return value;
        };
        require(call("sceSaveDataInitialize")==0 && initialized);
        OslSaveDirName dir{}; std::strcpy(dir.data,"TEST"); put(0x301,dir);
        OslSaveMount1 input{}; input.user=1; input.dir=reinterpret_cast<const OslSaveDirName *>(base+0x301);
        input.blocks=256; input.mode=create ? 4 : 1; put(0x101,input);
        require(call("sceSaveDataMount",base+0x101,base+0x181)==0 && memory.fault.empty());
        require(bytes[0x180]==0x7b && bytes[0x181+sizeof(OslSaveMountResult)]==0x7b);
        OslSaveMountResult mounted{}; std::memcpy(&mounted,bytes.data()+0x181,sizeof(mounted));
        require(!std::strcmp(mounted.point.data,"/savedata0") && mounted.status==(create ? 1u : 0u));
        put(0x501,mounted.point); text(0x801,"/savedata0/guest.bin");
        const char *payload="guest storage bytes"; const size_t length=std::strlen(payload);
        int fd=static_cast<int>(call("sceKernelOpen",base+0x801,create ? 0x602 : 0,0600)); require(fd>=3 && memory.fault.empty());
        if (create) {
            text(0x1001,payload);
            require(call("sceKernelWrite",fd,base+0x1001,length)==length);
            require(!std::strcmp(reinterpret_cast<char *>(bytes.data()+0x1001),payload));
            require(call("sceKernelClose",fd)==0);
            text(0x5001,"Guest Title");
            require(call("sceSaveDataSetParam",base+0x501,1,base+0x5001,12)==0);
            OslSaveIcon icon{}; icon.buffer=reinterpret_cast<const void *>(base+0x6001);
            icon.buffer_size=icon.data_size=128; put(0x6101,icon);
            std::memset(bytes.data()+0x6001,0x5a,128);
            require(call("sceSaveDataSaveIcon",base+0x501,base+0x6101)==0);
            fd=static_cast<int>(call("sceKernelOpen",base+0x801,0,0)); require(fd>=3);
        } else {
            require(uint32_t(call("sceKernelOpen",base+0x801,0x602,0600))==0x8002001eu);
            text(0x5001,"Wrong title");
            require(uint32_t(call("sceSaveDataSetParam",base+0x501,1,base+0x5001,12))==0x809f0000u);
        }
        call("sceKernelRead",fd,base+0x40000000,length);
        require(memory.fault=="guest write to protected region");
        require(call("sceKernelRead",fd,base+0x1001,length)==length && memory.fault.empty());
        require(!std::memcmp(bytes.data()+0x1001,payload,length));
        require(call("sceKernelFstat",fd,base+0x2001)==0);
        OslFileStat stat{}; std::memcpy(&stat,bytes.data()+0x2001,sizeof(stat));
        require(stat.size==int64_t(length) && bytes[0x2000]==0x7b && bytes[0x2001+sizeof(stat)]==0x7b);
        require(call("sceKernelClose",fd)==0);
        require(uint32_t(call("sceKernelRead",uint64_t(-1),UINT64_MAX,128))==0x80020009u && memory.fault.empty());
        OslSaveSearchCond condition{}; condition.user=1; put(0x3801,condition);
        OslSaveSearchResult search{}; search.names_capacity=2;
        search.names=reinterpret_cast<OslSaveDirName *>(base+0x4001);
        search.params=reinterpret_cast<OslSaveParam *>(base+0x4101);
        search.infos=reinterpret_cast<OslSaveSearchInfo *>(base+0x4801); put(0x3901,search);
        require(call("sceSaveDataDirNameSearch",base+0x3801,base+0x3901)==0);
        OslSaveSearchResult found{}; std::memcpy(&found,bytes.data()+0x3901,sizeof(found));
        require(found.hits==1 && found.set_count==1 && found.names==search.names && found.params==search.params && found.infos==search.infos);
        require(!std::strcmp(reinterpret_cast<char *>(bytes.data()+0x4001),"TEST"));
        OslSaveParam loaded{}; std::memcpy(&loaded,bytes.data()+0x4101,sizeof(loaded));
        require(!std::strcmp(loaded.title,"Guest Title"));
        require(bytes[0x4000]==0x7b && bytes[0x4021]==0x7b && bytes[0x4100]==0x7b && bytes[0x4101+sizeof(loaded)]==0x7b);
        input.dir=reinterpret_cast<const OslSaveDirName *>(UINT64_MAX); put(0x101,input);
        call("sceSaveDataMount",base+0x101,base+0x181);
        require(memory.fault=="unmapped guest memory" && memory.fault_address==UINT64_MAX);
        require(call("sceSaveDataGetMountInfo",base+0x501,base+0x5601)==0 && memory.fault.empty());
        require(call("sceSaveDataUmount",base+0x501)==0);
        std::printf("{\"guest_storage_checks\":%u,\"failed\":0,\"game_save_verified\":false}\n",checks);
    } catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
