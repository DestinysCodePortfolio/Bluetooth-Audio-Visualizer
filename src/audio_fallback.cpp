#include "audio_fallback.h"
#include "audio_buffer.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define FALLBACK_SAMPLE_RATE 44100
#define FALLBACK_BLOCK_SAMPLES 256
#define FALLBACK_TONE_HZ 220.0f
#define PI_F 3.14159265358979323846f

static bool s_enabled = false;
static float s_phase = 0.0f;

void fallback_audio_init(void) {
    s_enabled = false;
    s_phase = 0.0f;
    printf("Generated fallback audio initialized\n");
}

void fallback_audio_set_enabled(bool enabled) {
    s_enabled = enabled;

    if (!s_enabled) {
        audio_buf_stop();
    }
}

bool fallback_audio_is_enabled(void) {
    return s_enabled;
}

void fallback_audio_task(void) {
    if (!s_enabled) {
        return;
    }

    static int16_t samples[FALLBACK_BLOCK_SAMPLES];

    const float phase_step = 2.0f * PI_F * FALLBACK_TONE_HZ / FALLBACK_SAMPLE_RATE;

    for (int i = 0; i < FALLBACK_BLOCK_SAMPLES; i++) {
        float value = sinf(s_phase);

        // Keep it quiet so it does not blast the speaker.
        samples[i] = (int16_t)(value * 3000.0f);

        s_phase += phase_step;

        if (s_phase >= 2.0f * PI_F) {
            s_phase -= 2.0f * PI_F;
        }
    }

    audio_buf_push(samples, FALLBACK_BLOCK_SAMPLES, AUDIO_SRC_FALLBACK);
}