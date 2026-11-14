#include "storage.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <signal.h>
#include <sys/resource.h>
static unsigned checks;
static void require(bool value) {
    if (!value) throw std::runtime_error("storage contract check "+std::to_string(checks+1));
    ++checks;
}
static int64_t openFile(const char *path,int flags) {
    return reinterpret_cast<int64_t (*)(const char *,int,int)>(runtime_file_resolve("sceKernelOpen"))(path,flags,0600);
}
static int64_t closeFile(int fd) {
    return reinterpret_cast<int64_t (*)(int)>(runtime_file_resolve("sceKernelClose"))(fd);
}
static int64_t io(const char *name,int fd,void *bytes,uint64_t size) {
    return reinterpret_cast<int64_t (*)(int,void *,uint64_t)>(runtime_file_resolve(name))(fd,bytes,size);
}
static OslSaveMountResult mountSlot(const char *name,uint32_t mode) {
    OslSaveDirName dir{}; std::snprintf(dir.data,sizeof(dir.data),"%s",name);
    OslSaveMount1 input{}; input.user=1; input.dir=&dir; input.blocks=256; input.mode=mode;
    OslSaveMountResult result{}; require(Storage::mount(&input,&result)==0); return result;
}
static uint32_t search(OslSaveParam *param=nullptr) {
    OslSaveSearchCond condition{}; condition.user=1;
    OslSaveSearchResult result{}; OslSaveDirName name{};
    result.names=&name; result.names_capacity=1; result.params=param;
    require(Storage::search(&condition,&result)==0);
    if (result.hits==1) require(!std::strcmp(name.data,"TEST"));
    return result.hits;
}
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    try {
        const std::filesystem::path root=argv[1]; const std::string mode=argv[2];
        MobileStorage owner; owner.configure((root/"App0").string(),(root/"User").string());
        OslSaveDirName dir{}; std::strcpy(dir.data,"TEST");
        OslSaveMount1 input{}; input.user=1; input.dir=&dir; input.blocks=256;
        OslSaveMountResult result{};
        require(uint32_t(Storage::mount(&input,&result))==0x809f0001u);
        require(Storage::initialize()==0);
        if (mode=="create") {
            require(search()==0);
            OslSaveDirName bad{}; std::strcpy(bad.data,"../escape"); input.dir=&bad; input.mode=4;
            require(uint32_t(Storage::mount(&input,&result))==0x809f0000u);
            input.dir=&dir;
            OslSaveTitleId title{}; std::strcpy(title.data,"../bad"); input.title=&title;
            require(uint32_t(Storage::mount(&input,&result))==0x809f0000u); input.title=nullptr;
            result=mountSlot("TEST",4); require(result.status==1);
            OslSaveMountResult duplicate{};
            require(uint32_t(Storage::mount(&input,&duplicate))==0x809f0007u);
            OslSaveDelete remove{}; remove.user=1; remove.dir=&dir;
            require(uint32_t(Storage::remove(&remove))==0x809f000du);
            OslSaveMountPoint malformed{}; std::strcpy(malformed.data,"/savedata0junk");
            require(uint32_t(Storage::unmount(&malformed))==0x809f0008u);
            std::strcpy(malformed.data,"/savedata00");
            require(uint32_t(Storage::unmount(&malformed))==0x809f0008u);
            std::strcpy(malformed.data,"/savedata+0");
            require(uint32_t(Storage::unmount(&malformed))==0x809f0008u);
            std::ofstream(root/"App0/asset.bin",std::ios::binary)<<"immutable asset";
            require(uint32_t(openFile("/app0/asset.bin",0x602))==0x8002001eu);
            require(uint32_t(openFile("/data/../escape",0x602))==0x8002000du);
            std::vector<uint8_t> bytes(1156096);
            for (size_t i=0;i<bytes.size();++i) bytes[i]=uint8_t(i*17+23);
            int fd=static_cast<int>(openFile("/savedata0/save.dat",0x602)); require(fd>=3);
            require(io("sceKernelWrite",fd,bytes.data(),bytes.size())==int64_t(bytes.size()));
            char patch[]="PATCH";
            require(reinterpret_cast<int64_t (*)(int,void *,uint64_t,int64_t)>(runtime_file_resolve("sceKernelPwrite"))(fd,patch,5,12345)==5);
            require(closeFile(fd)==0);
            OslSaveParam param{}; std::strcpy(param.title,"Fixture Title"); param.user_param=42;
            require(Storage::param(&result.point,0,&param,sizeof(param))==0);
            std::vector<uint8_t> icon(2048,0x5a);
            OslSaveIcon value{}; value.buffer=icon.data(); value.buffer_size=icon.size(); value.data_size=icon.size();
            require(Storage::icon(&result.point,&value)==0);
            value.data_size=value.buffer_size+1;
            require(uint32_t(Storage::icon(&result.point,&value))==0x809f0000u);
            OslSaveSearchInfo info{}; require(Storage::info(&result.point,&info)==0 && info.blocks==256 && info.free_blocks<info.blocks);
            std::vector<OslSaveMountResult> extra;
            for (unsigned i=1;i<16;++i) { char name[32]; std::snprintf(name,sizeof(name),"SLOT_%u",i); extra.push_back(mountSlot(name,4)); }
            OslSaveDirName excess{}; std::strcpy(excess.data,"EXCESS"); input.dir=&excess;
            require(uint32_t(Storage::mount(&input,&duplicate))==0x809f000cu);
            for (unsigned i=1;i<16;++i) {
                require(Storage::unmount(&extra[i-1].point)==0);
                char name[32]; std::snprintf(name,sizeof(name),"SLOT_%u",i);
                OslSaveDirName slot{}; std::strcpy(slot.data,name); remove.dir=&slot;
                require(Storage::remove(&remove)==0);
            }
            require(Storage::unmount(&result.point)==0);
            std::ofstream(root/"User/savedata/1/CUSA05083/not_a_slot",std::ios::binary)<<"plain file";
            OslSaveParam loaded{}; require(search(&loaded)==1 && loaded.user_param==42 && !std::strcmp(loaded.title,"Fixture Title"));
        } else if (mode=="verify" || mode=="failure" || mode=="permission") {
            OslSaveParam loaded{};
            require(search(&loaded)==1 && loaded.user_param==42 && !std::strcmp(loaded.title,"Fixture Title"));
            result=mountSlot("TEST",mode=="verify" ? 1 : 0); require(result.status==0);
            if (mode!="verify") {
                if (mode=="failure") {
                    struct rlimit limit{}; require(getrlimit(RLIMIT_FSIZE,&limit)==0);
                    limit.rlim_cur=512; require(setrlimit(RLIMIT_FSIZE,&limit)==0); signal(SIGXFSZ,SIG_IGN);
                }
                loaded.user_param=99;
                require(uint32_t(Storage::param(&result.point,0,&loaded,sizeof(loaded)))==0x809f000bu);
                std::vector<uint8_t> bytes(4096,0x66);
                OslSaveIcon value{}; value.buffer=bytes.data(); value.buffer_size=value.data_size=bytes.size();
                require(uint32_t(Storage::icon(&result.point,&value))==0x809f000bu);
                if (mode=="failure") {
                    OslSaveDirName failed{}; std::strcpy(failed.data,"FAIL_NEW");
                    OslSaveMount1 request{}; request.user=1; request.dir=&failed; request.blocks=16; request.mode=4;
                    OslSaveMountResult output{};
                    require(uint32_t(Storage::mount(&request,&output))==0x809f000bu);
                    require(!std::filesystem::exists(root/"User/savedata/1/CUSA05083/FAIL_NEW"));
                    require(!std::filesystem::exists(root/"User/savedata/1/CUSA05083/FAIL_NEW.sce_sys"));
                }
            } else {
                require(uint32_t(openFile("/savedata0/save.dat",0x602))==0x8002001eu);
                require(uint32_t(Storage::param(&result.point,0,&loaded,sizeof(loaded)))==0x809f0000u);
                std::vector<uint8_t> bytes(1156096);
                int fd=static_cast<int>(openFile("/savedata0/save.dat",0)); require(fd>=3);
                require(io("sceKernelRead",fd,bytes.data(),bytes.size())==int64_t(bytes.size()));
                const char *patch="PATCH";
                for (size_t i=0;i<bytes.size();++i) if (bytes[i]!=(i>=12345 && i<12350 ? uint8_t(patch[i-12345]) : uint8_t(i*17+23))) throw std::runtime_error("persisted bytes differ");
                require(closeFile(fd)==0);
            }
            require(Storage::unmount(&result.point)==0);
        } else if (mode=="delete") {
            OslSaveDelete remove{}; remove.user=1; remove.dir=&dir;
            require(Storage::remove(&remove)==0); require(search()==0);
        } else return 2;
        std::printf("{\"native_storage_checks\":%u,\"failed\":0,\"game_save_verified\":false}\n",checks);
    } catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
