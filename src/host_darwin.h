#pragma once
#ifdef __APPLE__
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <mach/mach.h>
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE
#include <mach/mach_vm.h>
#endif
#include <sys/ucontext.h>
static inline uint64_t osl_thread_id(void) {
    uint64_t id=0;
    pthread_threadid_np(NULL,&id);
    return id;
}
#define gettid osl_thread_id
#if defined(__x86_64__)
/* x86-64 Darwin signal registers are contiguous in the ABI-defined thread state. */
enum { REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RDI, REG_RSI, REG_RBP, REG_RSP,
       REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
       REG_RIP, REG_EFL };
#define OSL_GREG(ctx,reg) (((uint64_t *)&((ucontext_t *)(ctx))->uc_mcontext->__ss)[reg])
#endif
static inline int osl_read_frame(uintptr_t address, void *out, size_t size) {
#if TARGET_OS_IPHONE
    vm_size_t read=0;
    return vm_read_overwrite(mach_task_self(),address,size,(vm_address_t)out,&read)==KERN_SUCCESS && read==size;
#else
    mach_vm_size_t read=0;
    return mach_vm_read_overwrite(mach_task_self(),address,size,(mach_vm_address_t)out,&read)==KERN_SUCCESS && read==size;
#endif
}
#endif
