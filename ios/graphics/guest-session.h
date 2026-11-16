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
/* Engineering deadline, 100..600000 ms, or 0 for cancellable continuous mode; invalid budgets return null. The
 * default factory retains 2000 ms. Configure before handing ownership to a worker. */
OslGuestSession *osl_guest_session_create_with_budget(uint32_t milliseconds);
/* Main-thread signal ownership brackets renderer initialization through shutdown. */
int osl_guest_session_faults_begin(OslGuestSession *);
int osl_guest_session_faults_end(OslGuestSession *);
/* Run exactly once on an owned worker. Returned JSON lives until destroy. */
const char *osl_guest_session_run(OslGuestSession *,const char *image);
void osl_guest_session_request_stop(OslGuestSession *);
unsigned osl_guest_session_gpu_calls(OslGuestSession *);
/* Developer fixture only: pause at seven real callbacks for at most one second,
 * bounded by the guest deadline, so a scene transition can cancel a live guest. */
int osl_guest_session_test_gate_after_buffers(OslGuestSession *);
/* Only after the worker is joined and fault ownership has ended. */
int osl_guest_session_destroy(OslGuestSession *);
#ifdef __cplusplus
}
#endif
