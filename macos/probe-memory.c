// Exercise the production direct-memory allocator and its shared aliases under Rosetta.
#include "runtime.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int32_t *runtime_errno(void) { static int32_t error; return &error; }

typedef int32_t (*Yield)(uint64_t, uint64_t *);
static Yield yield;
static void fiber_entry(uint64_t initial, uint64_t first) {
    uint64_t resumed=0;
    yield(initial+first,&resumed);
    yield(resumed+1,NULL);
}

static bool fiber_round_trip(void) {
    uint64_t guest[2]={0}, result=0;
    int32_t (*init)(void *,const char *,void (*)(uint64_t,uint64_t),uint64_t,void *,uint64_t,const void *,uint32_t) =
        (void *)runtime_fiber_resolve("_sceFiberInitializeImpl");
    int32_t (*run)(void *,uint64_t,uint64_t *)=(void *)runtime_fiber_resolve("sceFiberRun");
    int32_t (*finish)(void *)=(void *)runtime_fiber_resolve("sceFiberFinalize");
    yield=(void *)runtime_fiber_resolve("sceFiberReturnToThread");
    return init(guest,"mac-probe",fiber_entry,7,NULL,0,NULL,0)==0 &&
        run(guest,5,&result)==0 && result==12 &&
        run(guest,100,&result)==0 && result==101 && finish(guest)==0;
}

int main(void) {
    const size_t size = 16384;
    if (runtime_memory_prepare()) return 1;
    int32_t (*allocate)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *) =
        (void *)runtime_memory_resolve("rTXw65xmLIA#libkernel");
    int32_t (*map)(void **,uint64_t,int,int,int64_t,uint64_t) =
        (void *)runtime_memory_resolve("L-Q3LEjIbgA#libkernel");
    int32_t (*release)(int64_t,uint64_t) = (void *)runtime_memory_resolve("MBuItvba6z8#libkernel");
    int64_t physical, reused;
    void *a=NULL,*b=NULL;
    if (allocate(0,size,size,size,0,&physical) || map(&a,size,3,0,physical,size) ||
        map(&b,size,3,0,physical,size) || a==b) return 2;
    memset(a,0xa5,size);
    if (memcmp(a,b,size) || (uintptr_t)a >= (1ull<<40) || (uintptr_t)b >= (1ull<<40)) return 3;
    if (release(physical,size) || allocate(0,size,size,size,0,&reused) || reused!=physical) return 4;
    a=NULL;
    if (map(&a,size,3,0,reused,size)) return 5;
    for (size_t i=0;i<size;++i) if (((unsigned char *)a)[i]) return 6;
    void *low=runtime_low_map(size,PROT_READ|PROT_WRITE);
    if (!low || (uintptr_t)low >= (1ull<<40)) return 7;
    if (!fiber_round_trip()) return 8;
    puts("fiber switch, yield and resume passed");
    puts("guest reservation, low allocation, direct aliases and zero-on-reuse passed");
    return 0;
}
