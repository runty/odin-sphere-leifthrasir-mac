#ifndef OSL_RUNTIME_SAVEDATA_TYPES_H
#define OSL_RUNTIME_SAVEDATA_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
#define OSL_SAVE_ASSERT static_assert
#else
#define OSL_SAVE_ASSERT _Static_assert
#endif
typedef struct { char data[10]; char pad[6]; } OslSaveTitleId;
typedef struct { char data[32]; } OslSaveDirName;
typedef struct { char data[16]; } OslSaveMountPoint;
typedef struct {
    char title[128], subtitle[128], detail[1024];
    uint32_t user_param; int32_t pad;
    int64_t mtime;
    uint8_t reserved[32];
} OslSaveParam;
typedef struct { int32_t user; int32_t pad; const OslSaveTitleId *title; const OslSaveDirName *dir; const void *fingerprint;
                 uint64_t blocks; uint32_t mode; uint8_t reserved[32]; } OslSaveMount1;
typedef struct { int32_t user; int32_t pad; const OslSaveDirName *dir; uint64_t blocks; uint32_t mode;
                 uint8_t reserved[32]; int32_t pad2; } OslSaveMount2;
typedef struct { OslSaveMountPoint point; uint64_t required_blocks; uint32_t unused, status; uint8_t reserved[28]; int32_t pad; } OslSaveMountResult;
typedef struct { int32_t user; int32_t pad; const OslSaveTitleId *title; const OslSaveDirName *dir; uint32_t unused;
                 uint8_t reserved[32]; int32_t pad2; } OslSaveDelete;
typedef struct { int32_t user; int32_t pad; const OslSaveTitleId *title; const OslSaveDirName *dir; uint32_t key, order;
                 uint8_t reserved[32]; } OslSaveSearchCond;
typedef struct { uint64_t blocks, free_blocks; uint8_t reserved[32]; } OslSaveSearchInfo;
typedef struct { uint32_t hits; int32_t pad; OslSaveDirName *names; uint32_t names_capacity, set_count;
                 OslSaveParam *params; OslSaveSearchInfo *infos; uint8_t reserved[12]; int32_t pad2; } OslSaveSearchResult;
typedef struct { const void *buffer; uint64_t buffer_size, data_size; uint8_t reserved[32]; } OslSaveIcon;
OSL_SAVE_ASSERT(sizeof(OslSaveParam)==1328,"OrbisSaveDataParam layout");
OSL_SAVE_ASSERT(sizeof(OslSaveMount1)==80 && sizeof(OslSaveMount2)==64,"OrbisSaveDataMount layouts");
OSL_SAVE_ASSERT(sizeof(OslSaveMountResult)==64,"OrbisSaveDataMountResult layout");
OSL_SAVE_ASSERT(sizeof(OslSaveSearchCond)==64 && sizeof(OslSaveSearchResult)==56,"dir name search layouts");


OSL_SAVE_ASSERT(sizeof(OslSaveIcon)==56,"OrbisSaveDataIcon layout");
#undef OSL_SAVE_ASSERT
#endif
