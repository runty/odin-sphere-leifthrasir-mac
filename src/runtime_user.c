/* Shared offline users: console-compatible IDs, login events and names.
 * CPU-specific ABI/pointer translation belongs in the caller. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define USER_ID 1
#define USER_INVALID_ARGUMENT ((int32_t)0x80960005)
#define USER_NO_EVENT ((int32_t)0x80960007)
static pthread_mutex_t user_lock=PTHREAD_MUTEX_INITIALIZER;
/* ---- UserService ---- */
#define MAX_USERS 4
#define USER_EVENT_LOGIN 0
static unsigned logged_in=1; /* bit N-1: player N; player 1 from the start */
typedef struct { int32_t type, user; } UserEvent;
static UserEvent user_events[MAX_USERS]={{USER_EVENT_LOGIN,USER_ID}}; /* a player logs in at most once */
static int user_events_pending=1;
static int32_t player_user(int player) { return USER_ID+player-1; }
int runtime_user_player(int32_t user) {
    if (user<USER_ID || user>=USER_ID+MAX_USERS) return 0;
    const int player=user-USER_ID+1;
    pthread_mutex_lock(&user_lock);
    const int known=logged_in>>(player-1)&1;
    pthread_mutex_unlock(&user_lock);
    return known ? player : 0;
}
/* Called when a controller first takes slot `player`; the game opens that user's pad on the LOGIN event. */
void runtime_user_login(int player) {
    if (player<1 || player>MAX_USERS) return;
    pthread_mutex_lock(&user_lock);
    if (!(logged_in>>(player-1)&1)) {
        logged_in|=1u<<(player-1);
        user_events[user_events_pending++]=(UserEvent){USER_EVENT_LOGIN,player_user(player)};
    }
    pthread_mutex_unlock(&user_lock);
}
static ABI int32_t user_initialize(const void *params) { (void)params; puts("Runtime: UserService initialized (user 1 logged in)"); return 0; }
static ABI int32_t user_terminate(void) { return 0; }
static ABI int32_t user_initial(int32_t *id) { if (!id) return USER_INVALID_ARGUMENT; *id=USER_ID; return 0; }
static ABI int32_t user_list(int32_t *ids) {
    if (!ids) return USER_INVALID_ARGUMENT;
    pthread_mutex_lock(&user_lock);
    for (int i=0;i<MAX_USERS;++i) ids[i]=-1;
    for (int player=1,n=0;player<=MAX_USERS;++player) if (logged_in>>(player-1)&1) ids[n++]=player_user(player);
    pthread_mutex_unlock(&user_lock);
    return 0;
}
static ABI int32_t user_name(int32_t id,char *name,uint64_t size) {
    const int player=runtime_user_player(id);
    if (!player || !name) return USER_INVALID_ARGUMENT;
    char numbered[16];
    snprintf(numbered,sizeof(numbered),"Player %d",player);
    const char *value=player>1 ? numbered : getenv("OSL_USER_NAME") ? getenv("OSL_USER_NAME") : "Player";
    if (strlen(value)+1>size) return (int32_t)0x8096000a; /* BUFFER_TOO_SHORT */
    strcpy(name,value); return 0;
}
static ABI int32_t user_event(int32_t *event) {
    if (!event) return USER_INVALID_ARGUMENT;
    pthread_mutex_lock(&user_lock);
    const int pending=user_events_pending;
    if (pending) {
        event[0]=user_events[0].type; event[1]=user_events[0].user;
        memmove(user_events,user_events+1,(size_t)--user_events_pending*sizeof(*user_events));
    }
    pthread_mutex_unlock(&user_lock);
    return pending ? 0 : USER_NO_EVENT;
}


static const RuntimeExport exports[]={
    {"sceUserServiceInitialize",user_initialize}, {"sceUserServiceTerminate",user_terminate},
    {"sceUserServiceGetInitialUser",user_initial}, {"sceUserServiceGetLoginUserIdList",user_list},
    {"sceUserServiceGetUserName",user_name}, {"sceUserServiceGetEvent",user_event},
};
uintptr_t runtime_user_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
#ifdef OSL_AOT_CPU
/* The startup owner holds its process-wide service lease, with no guest workers. */
void osl_aot_user_reset(void) {
    pthread_mutex_lock(&user_lock);
    logged_in=1; user_events_pending=1;
    user_events[0]=(UserEvent){USER_EVENT_LOGIN,USER_ID};
    pthread_mutex_unlock(&user_lock);
}
#endif
