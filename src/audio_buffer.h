#pragma once

#include <stdint.h>
#include <stdbool.h>


#ifdef __cplusplus
extern "C" {
#endif

// -- Size constants -----------------------------------------------------------

// One sample per horizontal pixel for the oscilloscope display.
#define SCOPE_SAMPLES     480

// Ring buffer size for audio playback.
// Must be large enough to absorb jitter between the SD/BT producer and the
// 44100 Hz PWM consumer.  8192 samples ≈ 185 ms @ 44.1 kHz — plenty of
// headroom.  Keep it a power-of-two so the modulo in push/read is fast.
// IMPORTANT: do NOT set this equal to SCOPE_SAMPLES (480) — that was the
// original bug that starved the PWM IRQ within milliseconds.
#define AUDIO_BUFFER_SIZE 8192

// -- Source enum --------------------------------------------------------------

typedef enum {
    AUDIO_SRC_NONE      = 0,
    AUDIO_SRC_BLUETOOTH = 1,
    AUDIO_SRC_SD_CARD   = 2,
    AUDIO_SRC_SD        = 2,   // alias for compatibility
    AUDIO_SRC_FALLBACK  = 3
} AudioSource;

// Legacy alias so existing code compiles unchanged.
typedef AudioSource audio_source_t;

// -- API ----------------------------------------------------------------------

void        audio_buf_init(void);

// Push num_samples PCM samples into the ring buffer from the given source.
void        audio_buf_push(const int16_t *samples, uint32_t num_samples,
                           AudioSource source);

// Copy the most-recent SCOPE_SAMPLES samples into dst (for the oscilloscope).
// Does NOT advance the playback read cursor.
AudioSource audio_buf_snapshot(int16_t *dst);

// Read the next single sample for PWM playback.
// Advances the internal read cursor by one.
// Returns 0 (silence) when no new data is available.
int16_t     audio_buf_read_one(void);

// Legacy bulk-copy helper (kept for compatibility, not used for PWM output).
uint32_t    audio_buf_copy_latest(int16_t *out, uint32_t max_samples);

// Clear the ring buffer and reset all state.
void        audio_buf_stop(void);

AudioSource audio_buf_get_source(void);
bool        audio_buf_has_data(void);

uint32_t audio_buf_samples_available(void);
uint32_t audio_buf_free_space(void);


#ifdef __cplusplus
}
#endif