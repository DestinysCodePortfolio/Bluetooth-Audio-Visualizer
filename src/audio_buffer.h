#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// -- Type names (used by both main.cpp and lvgl_oscilloscope.cpp) --------------

#define SCOPE_SAMPLES  480   // one sample per horizontal pixel = AUDIO_BUFFER_SIZE

typedef enum {
    AUDIO_SRC_NONE      = 0,
    AUDIO_SRC_BLUETOOTH = 1,
    AUDIO_SRC_SD_CARD   = 2,
    AUDIO_SRC_SD        = 2,   // alias for compatibility
    AUDIO_SRC_FALLBACK  = 3
} AudioSource;

// Legacy alias so main.cpp code compiles unchanged
typedef AudioSource audio_source_t;

#define AUDIO_BUFFER_SIZE SCOPE_SAMPLES

// -- API -----------------------------------------------------------------------

void        audio_buf_init(void);
void        audio_buf_push(const int16_t *samples, uint32_t num_samples, AudioSource source);
AudioSource audio_buf_snapshot(int16_t *dst);
uint32_t    audio_buf_copy_latest(int16_t *out, uint32_t max_samples);
void        audio_buf_stop(void);
AudioSource audio_buf_get_source(void);
bool        audio_buf_has_data(void);

#ifdef __cplusplus
}
#endif
