#include "../../src/runtime.h"
#include <string.h>
// Startup imports have already been resolved to public names. Do not claim a
// general dynamic NID resolver until the full mobile loader supplies one.
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i)
        if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int32_t *runtime_errno(void) { static __thread int32_t value; return &value; }
