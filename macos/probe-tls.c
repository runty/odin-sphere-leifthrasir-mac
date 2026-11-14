// Use the actual TLS service with the observed libc and Fios2 module layouts.
#include "../src/runtime.c"

typedef struct { uintptr_t addr[2]; int passed; } Sample;
static void *worker(void *arg) {
    Sample *sample=arg;
    static const uint64_t libc_start[]={2,0}, libc_last[]={2,1167}, fios[]={3,63};
    unsigned char *base=guest_tls_get_addr(libc_start), *last=guest_tls_get_addr(libc_last);
    unsigned char *fio=guest_tls_get_addr(fios);
    sample->addr[0]=(uintptr_t)base;
    sample->addr[1]=(uintptr_t)fio;
    if (base[415]!=42 || last!=base+1167 || *last || *fio || base[416]) return NULL;
    *last=63;
    sample->passed=*(unsigned char *)guest_tls_get_addr(libc_last)==63 &&
        sample->addr[0] < (1ull<<40) && sample->addr[1] < (1ull<<40);
    return NULL;
}
int main(void) {
    unsigned char data[416]={0};
    data[415]=42;
    runtime_set_module_tls(2,data,416,1168);
    runtime_set_module_tls(3,data,0,64);
    Sample samples[2]={0};
    pthread_t threads[2];
    for (int i=0;i<2;++i) if (pthread_create(&threads[i],NULL,worker,&samples[i])) return 1;
    for (int i=0;i<2;++i) if (pthread_join(threads[i],NULL) || !samples[i].passed) return 2;
    if (samples[0].addr[0]==samples[1].addr[0] || samples[0].addr[1]==samples[1].addr[1]) return 3;
    puts("libc and Fios2 dynamic TLS copied, zero-filled and isolated on two threads");
    return 0;
}
