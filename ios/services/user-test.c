/* Contract/stress checks for the shared offline user implementation. */
#include "../../src/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
extern void osl_aot_user_reset(void);
static unsigned checks;
static void require(int value) {
    if (!value) { fprintf(stderr,"user contract check %u failed\n",checks+1); exit(1); }
    ++checks;
}
static void *login(void *value) {
    for (unsigned n=0;n<10000;++n) runtime_user_login((int)(intptr_t)value);
    return NULL;
}
int main(void) {
    int32_t (*initialize)(const void *)=(void *)runtime_user_resolve("sceUserServiceInitialize");
    int32_t (*initial)(int32_t *)=(void *)runtime_user_resolve("sceUserServiceGetInitialUser");
    int32_t (*list)(int32_t *)=(void *)runtime_user_resolve("sceUserServiceGetLoginUserIdList");
    int32_t (*name)(int32_t,char *,uint64_t)=(void *)runtime_user_resolve("sceUserServiceGetUserName");
    int32_t (*event)(int32_t *)=(void *)runtime_user_resolve("sceUserServiceGetEvent");
    int32_t (*terminate)(void)=(void *)runtime_user_resolve("sceUserServiceTerminate");
    require(initialize && initial && list && name && event && terminate);
    require(!runtime_user_resolve("anUnknownUserExport"));
    unsetenv("OSL_USER_NAME");
    osl_aot_user_reset();
    require(initialize(NULL)==0);
    require(initialize(NULL)==0); // Production implementation is idempotent.
    require((uint32_t)initial(NULL)==0x80960005u);
    require((uint32_t)list(NULL)==0x80960005u);
    require((uint32_t)event(NULL)==0x80960005u);
    int32_t id=0, ids[4]={};
    require(initial(&id)==0 && id==1);
    require(list(ids)==0 && ids[0]==1 && ids[1]==-1 && ids[2]==-1 && ids[3]==-1);
    char text[32];
    memset(text,0x7b,sizeof(text));
    require(name(1,text,7)==0 && !strcmp(text,"Player") && text[7]==0x7b);
    memset(text,0x7b,sizeof(text));
    require((uint32_t)name(1,text,6)==0x8096000au);
    require(text[0]==0x7b && text[31]==0x7b);
    require((uint32_t)name(1,text,0)==0x8096000au);
    require((uint32_t)name(1,NULL,32)==0x80960005u);
    const int32_t invalid[]={-1,0,2,INT_MAX};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        require(runtime_user_player(invalid[i])==0);
        require((uint32_t)name(invalid[i],text,sizeof(text))==0x80960005u);
    }
    setenv("OSL_USER_NAME","Odin Test",1);
    require(name(1,text,sizeof(text))==0 && !strcmp(text,"Odin Test"));
    pthread_t workers[8];
    for (int i=0;i<8;++i) require(pthread_create(&workers[i],NULL,login,(void *)(intptr_t)(i%4+1))==0);
    for (int i=0;i<8;++i) require(pthread_join(workers[i],NULL)==0);
    require(list(ids)==0 && ids[0]==1 && ids[1]==2 && ids[2]==3 && ids[3]==4);
    for (int i=1;i<=4;++i) require(runtime_user_player(i)==i);
    require(name(2,text,sizeof(text))==0 && !strcmp(text,"Player 2"));
    unsigned seen=0;
    for (unsigned i=0;i<4;++i) {
        int32_t value[2]={-1,-1};
        require(event(value)==0 && value[0]==0 && value[1]>=1 && value[1]<=4);
        require(!(seen&(1u<<value[1]))); seen|=1u<<value[1];
    }
    int32_t value[2]={0x7b,0x7b};
    require((uint32_t)event(value)==0x80960007u && value[0]==0x7b && value[1]==0x7b);
    runtime_user_login(0); runtime_user_login(5);
    require((uint32_t)event(value)==0x80960007u);
    require(terminate()==0);
    osl_aot_user_reset();
    require(list(ids)==0 && ids[0]==1 && ids[1]==-1);
    require(event(value)==0 && value[0]==0 && value[1]==1);
    printf("{\"user_checks\":%u,\"failed\":0,\"game_booted\":false}\n",checks);
}
