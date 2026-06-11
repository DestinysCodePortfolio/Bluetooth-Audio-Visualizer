#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// One sample per horizontal pixel for the oscilloscope display.
#define SCOPE_SAMPLES     480

// Shared PCM ring buffer.
// 8192 samples = about 185 ms at 44.1 kHz.  This is intentionally much larger
// than SCOPE_SAMPLES so SD-card reads and Bluetooth decode jitter do not starve
// the PWM consumer.
#define AUDIO_BUFFER_SIZE 8192

#if ((AUDIO_BUFFER_SIZE & (AUDIO_BUFFER_SIZE - 1)) != 0)
#error AUDIO_BUFFER_SIZE must be a power of two
#endif

typedef enum {
    AUDIO_SRC_NONE      = 0,
    AUDIO_SRC_BLUETOOTH = 1,
    AUDIO_SRC_SD_CARD   = 2,
    AUDIO_SRC_SD        = 2,   // alias for compatibility
    AUDIO_SRC_FALLBACK  = 3
} AudioSource;

typedef AudioSource audio_source_t;

void        audio_buf_init(void);

// Push PCM samples into the shared buffer.  This is used by Bluetooth, SD WAV,
// and generated fallback audio.
void        audio_buf_push(const int16_t *samples, uint32_t num_samples,
                           AudioSource source);

// Copy the most-recent SCOPE_SAMPLES samples for the visualizer.  This does not
// advance playback.
AudioSource audio_buf_snapshot(int16_t *dst);

// Read one sample for PWM playback.  This advances the playback cursor.
// Returns 0 when the buffer is empty.
int16_t     audio_buf_read_one(void);

uint32_t    audio_buf_copy_latest(int16_t *out, uint32_t max_samples);
void        audio_buf_stop(void);

AudioSource audio_buf_get_source(void);
bool        audio_buf_has_data(void);

// SD WAV pacing helpers.  These let the SD reader fill only when the playback
// buffer needs data, preventing songs from skipping ahead / sounding too fast.
uint32_t    audio_buf_samples_available(void);
uint32_t    audio_buf_free_space(void);

#ifdef __cplusplus
}
#endif
