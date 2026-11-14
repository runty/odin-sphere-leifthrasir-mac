/* Kernel event flags (sceKernel*EventFlag), adapted from gr2_pc's runtime_tlg.c. */
#define _GNU_SOURCE
#include "runtime.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define KERR_ESRCH KERR(3)
#define KERR_EINTR KERR(4)
#define KERR_EPERM KERR(1)
#define KERR_EBUSY KERR(16)
#define KERR_EINVAL KERR(22)
#define KERR_ENOMEM KERR(12)
#define KERR_ETIMEDOUT KERR(60)
#define KERR_ECANCELED KERR(85)

#define EF_MAGIC UINT32_C(0x45564654)
#define EF_ATTR_MULTI 0x20
#define EF_WAIT_AND 1
#define EF_WAIT_OR 2
#define EF_CLEAR_ALL 0x10
#define EF_CLEAR_PAT 0x20
typedef struct EfWaiter {
    uint64_t pattern, result;
    uint32_t mode;
    int released, canceled;
    struct EfWaiter *next;
} EfWaiter;
typedef struct EventFlag {
    uintptr_t handle;
    unsigned refs;
    struct EventFlag *next;
    uint32_t magic, attr;
    uint64_t bits;
    int waiters, deleted; /* waiters: threads inside ef_wait, which still read the object */
    EfWaiter *queue;      /* blocked waiters not yet satisfied, oldest first */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    char name[32];
} EventFlag;

/* Kernel handles are opaque, never host object addresses. A monotonically
 * increasing token prevents a deleted handle from aliasing a reused allocation.
 * The registry owns one reference; each operation pins the object until done.
 * Delete removes the handle, wakes waiters and releases the registry reference.
 */
#define EF_BUCKETS 256
static pthread_mutex_t registry_lock=PTHREAD_MUTEX_INITIALIZER;
static EventFlag *registry[EF_BUCKETS];
static uintptr_t next_handle=1;
static uint64_t live_flags;
uint64_t runtime_eventflag_live_count(void) {
    pthread_mutex_lock(&registry_lock);
    uint64_t count=live_flags;
    pthread_mutex_unlock(&registry_lock);
    return count;
}
static unsigned ef_bucket(uintptr_t handle) { return (unsigned)((handle>>4)&(EF_BUCKETS-1)); }
static EventFlag *ef_get(const EventFlag *handle) {
    pthread_mutex_lock(&registry_lock);
    EventFlag *ef=registry[ef_bucket((uintptr_t)handle)];
    while (ef && ef->handle!=(uintptr_t)handle) ef=ef->next;
    if (ef) ++ef->refs;
    pthread_mutex_unlock(&registry_lock);
    return ef;
}
static void ef_put(EventFlag *ef) {
    pthread_mutex_lock(&registry_lock);
    int dispose=!--ef->refs;
    if (dispose) --live_flags;
    pthread_mutex_unlock(&registry_lock);
    if (dispose) {
        pthread_cond_destroy(&ef->cond);
        pthread_mutex_destroy(&ef->lock);
        free(ef);
    }
}
static ABI int32_t ef_create(EventFlag **out,const char *name,uint32_t attr,uint64_t init,const void *param) {
    if (!out || param || (attr & ~UINT32_C(0x33))) return KERR_EINVAL;
    EventFlag *ef=calloc(1,sizeof(*ef));
    if (!ef) return KERR_ENOMEM;
    ef->magic=EF_MAGIC; ef->attr=attr; ef->bits=init; ef->refs=1;
    if (pthread_mutex_init(&ef->lock,NULL)) { free(ef); return KERR_ENOMEM; }
    if (pthread_cond_init(&ef->cond,NULL)) { pthread_mutex_destroy(&ef->lock); free(ef); return KERR_ENOMEM; }
    snprintf(ef->name,sizeof(ef->name),"%s",name ? name : "");
    pthread_mutex_lock(&registry_lock);
    ++live_flags;
    /* All handles fit the guest's 40-bit address representation; never wrap. */
    if (next_handle>=((uintptr_t)1<<36)) {
        pthread_mutex_unlock(&registry_lock);
        ef_put(ef); return KERR_ENOMEM;
    }
    ef->handle=next_handle++<<4;
    unsigned bucket=ef_bucket(ef->handle);
    ef->next=registry[bucket]; registry[bucket]=ef;
    const uintptr_t handle=ef->handle;
    pthread_mutex_unlock(&registry_lock);
    *out=(EventFlag *)handle; return 0;
}
static ABI int32_t ef_delete(EventFlag *handle) {
    pthread_mutex_lock(&registry_lock);
    EventFlag **link=&registry[ef_bucket((uintptr_t)handle)];
    while (*link && (*link)->handle!=(uintptr_t)handle) link=&(*link)->next;
    EventFlag *ef=*link;
    if (ef) *link=ef->next;
    pthread_mutex_unlock(&registry_lock);
    if (!ef) return KERR_ESRCH;
    /* The removed registry reference now belongs to this deleting call. */
    pthread_mutex_lock(&ef->lock);
    ef->deleted=1;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return 0;
}
static int ef_match(uint64_t bits,uint64_t pattern,uint32_t mode) {
    return (mode & EF_WAIT_AND) ? (bits & pattern)==pattern : (bits & pattern)!=0;
}
static void ef_consume(EventFlag *ef,uint64_t pattern,uint32_t mode,uint64_t *result) {
    if (result) *result=ef->bits;
    if (mode & EF_CLEAR_ALL) ef->bits=0;
    else if (mode & EF_CLEAR_PAT) ef->bits&=~pattern;
}
static int ef_single_in_use(const EventFlag *ef) { return ef->queue && !(ef->attr & EF_ATTR_MULTI); }
static void ef_unlink(EventFlag *ef,EfWaiter *w) {
    for (EfWaiter **link=&ef->queue;*link;link=&(*link)->next)
        if (*link==w) { *link=w->next; return; }
}
/* Satisfied waiters are released by the setter, as the kernel does: their clear mode applies at once and
 * they stop counting as waiters before they run, so a poll right after the set cannot report EPERM. */
static ABI int32_t ef_set(EventFlag *handle,uint64_t bits) {
    EventFlag *ef=ef_get(handle);
    if (!ef) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    if (ef->deleted) { pthread_mutex_unlock(&ef->lock); ef_put(ef); return KERR_ESRCH; }
    ef->bits|=bits;
    for (EfWaiter **link=&ef->queue;*link;) {
        EfWaiter *w=*link;
        if (!ef_match(ef->bits,w->pattern,w->mode)) { link=&w->next; continue; }
        ef_consume(ef,w->pattern,w->mode,&w->result);
        w->released=1; *link=w->next;
    }
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return 0;
}
static ABI int32_t ef_clear(EventFlag *handle,uint64_t mask) {
    EventFlag *ef=ef_get(handle);
    if (!ef) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    if (ef->deleted) { pthread_mutex_unlock(&ef->lock); ef_put(ef); return KERR_ESRCH; }
    ef->bits&=mask;
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return 0;
}
/* sceKernelCancelEventFlag: the waiters return ECANCELED, the bits become `pattern`. */
static ABI int32_t ef_cancel(EventFlag *handle,uint64_t pattern,int32_t *waiters) {
    EventFlag *ef=ef_get(handle);
    if (!ef) return KERR_ESRCH;
    pthread_mutex_lock(&ef->lock);
    if (ef->deleted) { pthread_mutex_unlock(&ef->lock); ef_put(ef); return KERR_ESRCH; }
    int32_t count=0;
    for (EfWaiter *w=ef->queue;w;w=w->next) { w->canceled=1; w->result=pattern; ++count; }
    ef->queue=NULL;
    if (waiters) *waiters=count;
    ef->bits=pattern;
    pthread_cond_broadcast(&ef->cond);
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return 0;
}
static int ef_mode_ok(uint32_t mode) {
    uint32_t wait=mode & 3, clear=mode & 0x30;
    return (wait==EF_WAIT_AND || wait==EF_WAIT_OR) && clear!=0x30 && !(mode & ~UINT32_C(0x33));
}
static void ef_now(struct timespec *value) {
#ifdef __APPLE__
    clock_gettime(CLOCK_MONOTONIC,value);
#else
    clock_gettime(CLOCK_REALTIME,value);
#endif
}
static int ef_timed_wait(EventFlag *ef,const struct timespec *deadline) {
#ifdef __APPLE__
    struct timespec now; ef_now(&now);
    int64_t ns=(int64_t)(deadline->tv_sec-now.tv_sec)*1000000000+deadline->tv_nsec-now.tv_nsec;
    if (ns<=0) return ETIMEDOUT;
    const struct timespec remaining={.tv_sec=(time_t)(ns/1000000000),.tv_nsec=(long)(ns%1000000000)};
    return pthread_cond_timedwait_relative_np(&ef->cond,&ef->lock,&remaining);
#else
    return pthread_cond_timedwait(&ef->cond,&ef->lock,deadline);
#endif
}
static ABI int32_t ef_poll(EventFlag *handle,uint64_t pattern,uint32_t mode,uint64_t *result) {
    EventFlag *ef=ef_get(handle);
    if (!ef) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) { ef_put(ef); return KERR_EINVAL; }
    pthread_mutex_lock(&ef->lock);
    int32_t r=0;
    /* Odin spins on this EPERM to learn that its single-waiter worker is parked (LIGHTCALC). */
    if (ef->deleted) r=KERR_ESRCH;
    else if (ef_single_in_use(ef)) r=KERR_EPERM;
    else if (ef_match(ef->bits,pattern,mode)) ef_consume(ef,pattern,mode,result);
    else { r=KERR_EBUSY; if (result) *result=ef->bits; }
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return r;
}
static int32_t ef_wait_internal(EventFlag *handle,uint64_t pattern,uint32_t mode,uint64_t *result,uint32_t *timeout_us,
                                int (*interrupt)(void *),void *context) {
    EventFlag *ef=ef_get(handle);
    if (!ef) return KERR_ESRCH;
    if (!pattern || !ef_mode_ok(mode)) { ef_put(ef); return KERR_EINVAL; }
    pthread_mutex_lock(&ef->lock);
    if (ef->deleted || ef_single_in_use(ef)) {
        int32_t r=ef->deleted ? KERR_ESRCH : KERR_EPERM;
        pthread_mutex_unlock(&ef->lock); ef_put(ef); return r;
    }
    struct timespec start, deadline;
    ef_now(&start);
    if (timeout_us) {
        uint64_t ns=(uint64_t)start.tv_nsec+(uint64_t)*timeout_us*1000;
        deadline.tv_sec=start.tv_sec+(time_t)(ns/1000000000); deadline.tv_nsec=(long)(ns%1000000000);
    }
    int32_t r=0;
    EfWaiter self={.pattern=pattern,.mode=mode};
    ++ef->waiters;
    if (ef_match(ef->bits,pattern,mode)) { ef_consume(ef,pattern,mode,&self.result); self.released=1; }
    else {
        EfWaiter **tail=&ef->queue;
        while (*tail) tail=&(*tail)->next;
        *tail=&self;
        while (!self.released && !self.canceled && !ef->deleted) {
            if (interrupt && interrupt(context)) { r=KERR_EINTR; break; }
            int e;
            if (interrupt) {
                // Keep the SAME waiter enqueued across host guard checks, so
                // FIFO/clear modes and single-waiter EPERM remain atomic.
                struct timespec slice;ef_now(&slice);
                slice.tv_nsec+=5000000;
                if(slice.tv_nsec>=1000000000){++slice.tv_sec;slice.tv_nsec-=1000000000;}
                if(timeout_us && (deadline.tv_sec<slice.tv_sec ||
                   (deadline.tv_sec==slice.tv_sec && deadline.tv_nsec<slice.tv_nsec)))slice=deadline;
                e=ef_timed_wait(ef,&slice);
                if(e==ETIMEDOUT && !self.released && !self.canceled) {
                    struct timespec now;ef_now(&now);
                    if(timeout_us && (now.tv_sec>deadline.tv_sec ||
                       (now.tv_sec==deadline.tv_sec && now.tv_nsec>=deadline.tv_nsec))){r=KERR_ETIMEDOUT;break;}
                }
            } else {
                e=timeout_us ? ef_timed_wait(ef,&deadline) : pthread_cond_wait(&ef->cond,&ef->lock);
                if(e==ETIMEDOUT && !self.released && !self.canceled){r=KERR_ETIMEDOUT;break;}
            }
        }
        if (!self.released && !self.canceled) ef_unlink(ef,&self);
    }
    --ef->waiters;
    if (self.released) { r=0; if (result) *result=self.result; }
    else if (ef->deleted) r=KERR_ESRCH;
    else if (self.canceled) { r=KERR_ECANCELED; if (result) *result=self.result; }
    else if (result) *result=ef->bits;
    pthread_mutex_unlock(&ef->lock);
    if (timeout_us) {
        if (r==KERR_ETIMEDOUT) *timeout_us=0;
        else {
            struct timespec now; ef_now(&now);
            int64_t spent=(int64_t)(now.tv_sec-start.tv_sec)*1000000+(now.tv_nsec-start.tv_nsec)/1000;
            *timeout_us=spent>=(int64_t)*timeout_us ? 0 : (uint32_t)(*timeout_us-spent);
        }
    }
    ef_put(ef);
    return r;
}

static ABI int32_t ef_wait(EventFlag *handle,uint64_t pattern,uint32_t mode,uint64_t *result,uint32_t *timeout_us) {
    return ef_wait_internal(handle,pattern,mode,result,timeout_us,NULL,NULL);
}
/* Host-only guard: callback must not block, throw or unwind through this C lock.
 * This is not a new guest export and does not change the legacy wait ABI. */
int32_t runtime_eventflag_wait_guarded(void *handle,uint64_t pattern,uint32_t mode,uint64_t *result,uint32_t *timeout_us,
                                     int (*interrupt)(void *),void *context) {
    return ef_wait_internal((EventFlag *)handle,pattern,mode,result,timeout_us,interrupt,context);
}

unsigned runtime_eventflag_waiters(uint64_t handle) {
    EventFlag *ef=ef_get((EventFlag *)(uintptr_t)handle);
    if (!ef) return 0;
    pthread_mutex_lock(&ef->lock);
    unsigned count=ef->deleted ? 0 : (unsigned)ef->waiters;
    pthread_mutex_unlock(&ef->lock);
    ef_put(ef);
    return count;
}

static const RuntimeExport exports[] = {
    {"sceKernelCreateEventFlag", ef_create}, {"sceKernelDeleteEventFlag", ef_delete},
    {"sceKernelSetEventFlag", ef_set}, {"sceKernelClearEventFlag", ef_clear},
    {"sceKernelPollEventFlag", ef_poll}, {"sceKernelWaitEventFlag", ef_wait},
    {"sceKernelCancelEventFlag", ef_cancel},
};
uintptr_t runtime_eventflag_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
