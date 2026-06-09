#include "audio_fallback.h"

#include <math.h>
#include "pico/stdlib.h"
#include "audio_buffer.h"

#define FALLBACK_BLOCK_SIZE 256

static bool fallback_enabled = false;
static int16_t fallback_samples[FALLBACK_BLOCK_SIZE];

static float phase = 0.0f;
static uint32_t tick_count = 0;

void fallback_audio_init(void) {
    fallback_enabled = false;
    phase = 0.0f;
    tick_count = 0;
}

void fallback_audio_set_enabled(bool enabled) {
    fallback_enabled = enabled;

    if (!enabled) {
        audio_buf_stop();
    }
}

bool fallback_audio_is_enabled(void) {
    return fallback_enabled;
}

void fallback_audio_task(void) {
    if (!fallback_enabled) {
        return;
    }

    tick_count++;

    for (int i = 0; i < FALLBACK_BLOCK_SIZE; i++) {
        float sine = sinf(phase);

        // Fake beat pulse every 40 timer ticks
        float beat_gain = 1.0f;
        if ((tick_count % 40) < 5) {
            beat_gain = 2.4f;
        }

        float sample = sine * beat_gain * 9000.0f;

        if (sample > 32767.0f) {
            sample = 32767.0f;
        } else if (sample < -32768.0f) {
            sample = -32768.0f;
        }

        fallback_samples[i] = (int16_t)sample;

        phase += 0.08f;

        if (phase >= 6.2831853f) {
            phase -= 6.2831853f;
        }
    }

    audio_buf_push(
        fallback_samples,
        FALLBACK_BLOCK_SIZE,
        AUDIO_SRC_FALLBACK
    );
}