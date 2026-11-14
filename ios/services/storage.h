#pragma once
#include "../../src/runtime_savedata_types.h"
#include "../../src/runtime_file_types.h"
#include <string>

class MobileStorage {
    bool configured_=false;
public:
    MobileStorage()=default;
    ~MobileStorage();
    MobileStorage(const MobileStorage &)=delete;
    MobileStorage &operator=(const MobileStorage &)=delete;
    void configure(const std::string &app0,const std::string &user);
    bool configured() const { return configured_; }
};
extern "C" uintptr_t runtime_savedata_resolve(const char *);
extern "C" uintptr_t runtime_file_resolve(const char *);
extern "C" int32_t *runtime_errno();
extern "C" int runtime_file_readonly(const char *);
extern "C" int runtime_file_translate(const char *,char *,size_t);

namespace Storage {
int32_t initialize();
int32_t mount(const OslSaveMount1 *,OslSaveMountResult *);
int32_t unmount(const OslSaveMountPoint *);
int32_t info(const OslSaveMountPoint *,OslSaveSearchInfo *);
int32_t param(const OslSaveMountPoint *,uint32_t,const void *,uint64_t);
int32_t icon(const OslSaveMountPoint *,const OslSaveIcon *);
int32_t search(const OslSaveSearchCond *,OslSaveSearchResult *);
int32_t remove(const OslSaveDelete *);
}
