#ifndef FALLBACK_AUDIO_H
#define FALLBACK_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void fallback_audio_init(void);
void fallback_audio_set_enabled(bool enabled);
bool fallback_audio_is_enabled(void);
void fallback_audio_start_bt_arpeggio(void);
bool fallback_audio_cue_is_active(void);
void fallback_audio_task(void);

#ifdef __cplusplus
}
#endif

#endif
