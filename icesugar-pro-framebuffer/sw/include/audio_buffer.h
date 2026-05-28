#pragma once
#ifdef __cplusplus
extern "C" {
#endif
 
#include <stdint.h>
#include <stdbool.h>
#include "pico/mutex.h"
 
// ── Must match what lvgl.c (lvgl_oscilloscope.cpp) expects ──────────────────
 
#define SCOPE_SAMPLES  480   // one sample per horizontal pixel
 
typedef enum {
    AUDIO_SRC_NONE      = 0,
    AUDIO_SRC_BLUETOOTH = 1,
    AUDIO_SRC_SD_CARD   = 2,
} AudioSource;
 
typedef struct {
    int16_t    samples[SCOPE_SAMPLES];
    uint32_t   write_idx;
    bool       active;
    AudioSource source;
    mutex_t    mtx;
} AudioRingBuf;
 
extern AudioRingBuf g_audio_buf;
 
static inline void audio_buf_init(void) {
    mutex_init(&g_audio_buf.mtx);
    g_audio_buf.write_idx = 0;
    g_audio_buf.active    = false;
    g_audio_buf.source    = AUDIO_SRC_NONE;
}
 
// Push interleaved stereo PCM — left channel only stored
static inline void audio_buf_push(const int16_t *pcm, uint32_t num_frames,
                                   AudioSource src)
{
    mutex_enter_blocking(&g_audio_buf.mtx);
    g_audio_buf.source = src;
    g_audio_buf.active = true;
    for (uint32_t i = 0; i < num_frames; i++) {
        g_audio_buf.samples[g_audio_buf.write_idx % SCOPE_SAMPLES] = pcm[i * 2];
        g_audio_buf.write_idx++;
    }
    mutex_exit(&g_audio_buf.mtx);
}
 
// Return a snapshot of the ring buffer; returns current AudioSource
static inline AudioSource audio_buf_snapshot(int16_t *dst) {
    mutex_enter_blocking(&g_audio_buf.mtx);
    AudioSource src = g_audio_buf.source;
    // Copy in-order starting from oldest sample
    uint32_t start = g_audio_buf.write_idx;
    for (int i = 0; i < SCOPE_SAMPLES; i++)
        dst[i] = g_audio_buf.samples[(start + i) % SCOPE_SAMPLES];
    mutex_exit(&g_audio_buf.mtx);
    return src;
}
 
static inline void audio_buf_stop(void) {
    mutex_enter_blocking(&g_audio_buf.mtx);
    g_audio_buf.active = false;
    g_audio_buf.source = AUDIO_SRC_NONE;
    mutex_exit(&g_audio_buf.mtx);
}
 
#ifdef __cplusplus
}
#endif