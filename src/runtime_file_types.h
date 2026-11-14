#ifndef OSL_RUNTIME_FILE_TYPES_H
#define OSL_RUNTIME_FILE_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
#define OSL_FILE_ASSERT static_assert
#else
#define OSL_FILE_ASSERT _Static_assert
#endif
typedef struct { int64_t sec, nsec; } OslFileTimespec;
typedef struct {
    uint32_t dev, ino;
    uint16_t mode, nlink;
    uint32_t uid, gid, rdev;
    OslFileTimespec atime, mtime, ctime;
    int64_t size, blocks;
    uint32_t blksize, flags, gen;
    int32_t lspare;
    OslFileTimespec birthtime;
} OslFileStat;
OSL_FILE_ASSERT(sizeof(OslFileStat)==120,"FreeBSD stat layout");


#undef OSL_FILE_ASSERT
#endif
