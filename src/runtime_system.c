/* System settings for the offline virtual console, shared by native and AOT hosts. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define SYSTEM_PARAMETER ((int32_t)0x80A10003)
static int launcher_language=1;
void runtime_set_language(int ps4_language) { launcher_language=ps4_language; }
static int language(void) { const char *v=getenv("OSL_LANGUAGE"); return v ? atoi(v) : launcher_language; }
static ABI int32_t system_param(int32_t id,int32_t *value) {
    if (!value) return SYSTEM_PARAMETER;
    switch (id) {
    case 1: *value=language(); break;           /* language (1 = English US) */
    case 2: *value=1; break;                    /* date format DD/MM/YYYY */
    case 3: *value=1; break;                    /* 24-hour clock */
#ifdef _WIN32
    case 4: *value=(int32_t)(compat_utc_offset(time(NULL))/60); break;
#else
    case 4: { time_t now=time(NULL); struct tm t; localtime_r(&now,&t); *value=(int32_t)(t.tm_gmtoff/60); break; }
#endif
    case 5: *value=0; break;                    /* summer time */
    case 7: *value=0; break;                    /* parental level off */
    case 1000: *value=1; break;                 /* enter button = cross */
    default: fprintf(stderr,"STOP: unsupported system parameter %d\n",id); exit(21);
    }
    return 0;
}
static ABI int32_t system_status(unsigned char *status) {
    if (!status) return SYSTEM_PARAMETER;
    memset(status,0,12); /* event_num=0, no overlay, foreground, normal CPU mode */
    return 0;
}
static ABI int32_t hide_splash(void) { puts("Runtime: SystemService: splash screen hidden"); return 0; }
static ABI int32_t safe_area(float *info) { if (!info) return SYSTEM_PARAMETER; *info=1.0f; return 0; } /* full screen */

int runtime_system_param_supported(int32_t id) {
    return id==1 || id==2 || id==3 || id==4 || id==5 || id==7 || id==1000;
}
static const RuntimeExport exports[]={
    {"sceSystemServiceParamGetInt",system_param},{"sceSystemServiceGetStatus",system_status},
    {"sceSystemServiceHideSplashScreen",hide_splash},{"sceSystemServiceGetDisplaySafeAreaInfo",safe_area},
};
uintptr_t runtime_system_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
