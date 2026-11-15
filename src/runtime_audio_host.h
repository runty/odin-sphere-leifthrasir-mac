#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private host API for guest marshallers. Never exported as a guest NID.
 * Query before copying PCM, then supply that same format and exact byte count.
 * A reopened port has a new generation even if it has the same guest handle.
 * The interrupt callback must only inspect cancellation/deadline state. */
typedef struct {
    uint64_t generation;
    size_t bytes;
} OslAudioInput;
#define OSL_AUDIO_INTERRUPTED INT32_MIN
int32_t runtime_audio_input_format(int32_t handle,OslAudioInput *input);
int32_t runtime_audio_output_copy(int32_t handle,const void *data,size_t bytes,const OslAudioInput *input,
                                  int pace,int (*interrupted)(void *),void *context);
#ifdef __cplusplus
}
#endif
