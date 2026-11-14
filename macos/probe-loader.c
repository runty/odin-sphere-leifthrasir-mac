#include "../src/host_darwin.h"
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>

static volatile sig_atomic_t handled;
static void illegal(int signal, siginfo_t *info, void *context) {
    (void)signal; (void)info;
    OSL_GREG(context,REG_RIP)+=2; /* ud2 */
    OSL_GREG(context,REG_RAX)=73;
    handled=1;
}
int main(void) {
    struct sigaction action={0};
    action.sa_sigaction=illegal;
    action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGILL,&action,NULL)) return 1;
    uint64_t result;
    __asm__ volatile("ud2" : "=a"(result));
    if (!handled || result!=73) return 2;
    uint64_t value=42,copy=0;
    if (!osl_read_frame((uintptr_t)&value,&copy,sizeof(copy)) || copy!=42) return 3;
    if (osl_read_frame(1,&copy,sizeof(copy))) return 4;
    size_t size=16384;
    void *page=mmap(NULL,size,PROT_NONE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    if (page==MAP_FAILED) return 5;
    int unexpected=osl_read_frame((uintptr_t)page,&copy,sizeof(copy));
    munmap(page,size);
    if (unexpected) return 6;
    puts("Darwin signal PC/register restoration and guarded memory reads passed");
}
