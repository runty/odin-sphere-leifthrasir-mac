#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OslGuestSession OslGuestSession;
/* Validate the complete prepared image and relocation table before using SDK
 * metadata to initialize the renderer. No guest execution or renderer calls. */
int osl_guest_session_sdk(const char *image,uint32_t *sdk);
OslGuestSession *osl_guest_session_create(void);
/* Main-thread signal ownership brackets renderer initialization through shutdown. */
int osl_guest_session_faults_begin(OslGuestSession *);
int osl_guest_session_faults_end(OslGuestSession *);
/* Run exactly once on an owned worker. Returned JSON lives until destroy. */
const char *osl_guest_session_run(OslGuestSession *,const char *image);
void osl_guest_session_request_stop(OslGuestSession *);
unsigned osl_guest_session_gpu_calls(OslGuestSession *);
/* Only after the worker is joined and fault ownership has ended. */
int osl_guest_session_destroy(OslGuestSession *);
#ifdef __cplusplus
}
#endif
