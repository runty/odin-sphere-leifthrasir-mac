#include "storage.h"
#include <filesystem>
#include <stdexcept>
extern "C" void runtime_file_configure(const char *,const char *);
extern "C" void runtime_savedata_configure(const char *);
extern "C" void osl_aot_file_reset();
extern "C" void osl_aot_savedata_reset();
MobileStorage::~MobileStorage() { osl_aot_savedata_reset(); osl_aot_file_reset(); }
void MobileStorage::configure(const std::string &app0,const std::string &user) {
    if (app0.empty() || user.empty() || app0.size()>=512 || user.size()+61>=512)
        throw std::runtime_error("storage path exceeds native mount bounds");
    osl_aot_savedata_reset(); osl_aot_file_reset(); configured_=false;
    std::filesystem::create_directories(app0);
    std::filesystem::create_directories(user);
    if (!std::filesystem::is_directory(app0) || !std::filesystem::is_directory(user))
        throw std::runtime_error("storage roots must be directories");
    runtime_file_configure(app0.c_str(),user.c_str());
    runtime_savedata_configure("CUSA05083");
    configured_=true;
}
namespace Storage {
int32_t initialize() {
    return reinterpret_cast<int32_t (*)(const void *)>(runtime_savedata_resolve("sceSaveDataInitialize"))(nullptr);
}
int32_t mount(const OslSaveMount1 *in,OslSaveMountResult *out) {
    return reinterpret_cast<int32_t (*)(const OslSaveMount1 *,OslSaveMountResult *)>(runtime_savedata_resolve("sceSaveDataMount"))(in,out);
}
int32_t unmount(const OslSaveMountPoint *point) {
    return reinterpret_cast<int32_t (*)(const OslSaveMountPoint *)>(runtime_savedata_resolve("sceSaveDataUmount"))(point);
}
int32_t info(const OslSaveMountPoint *point,OslSaveSearchInfo *out) {
    return reinterpret_cast<int32_t (*)(const OslSaveMountPoint *,OslSaveSearchInfo *)>(runtime_savedata_resolve("sceSaveDataGetMountInfo"))(point,out);
}
int32_t param(const OslSaveMountPoint *point,uint32_t type,const void *in,uint64_t size) {
    return reinterpret_cast<int32_t (*)(const OslSaveMountPoint *,uint32_t,const void *,uint64_t)>(runtime_savedata_resolve("sceSaveDataSetParam"))(point,type,in,size);
}
int32_t icon(const OslSaveMountPoint *point,const OslSaveIcon *in) {
    return reinterpret_cast<int32_t (*)(const OslSaveMountPoint *,const OslSaveIcon *)>(runtime_savedata_resolve("sceSaveDataSaveIcon"))(point,in);
}
int32_t search(const OslSaveSearchCond *in,OslSaveSearchResult *out) {
    return reinterpret_cast<int32_t (*)(const OslSaveSearchCond *,OslSaveSearchResult *)>(runtime_savedata_resolve("sceSaveDataDirNameSearch"))(in,out);
}
int32_t remove(const OslSaveDelete *in) {
    return reinterpret_cast<int32_t (*)(const OslSaveDelete *)>(runtime_savedata_resolve("sceSaveDataDelete"))(in);
}
}
