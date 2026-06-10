#include "audio_fallback.h"
#include "audio_buffer.h"
#include "audio_pwm_output.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define FALLBACK_SAMPLE_RATE 44100
#define FALLBACK_BLOCK_SAMPLES 256
#define FALLBACK_TONE_HZ 220.0f
#define PI_F 3.14159265358979323846f
#define CUE_NOTE_SAMPLES 4410

static bool s_enabled = false;
static bool s_cue_active = false;
static float s_phase = 0.0f;
static uint32_t s_cue_sample_index = 0;

static const float s_cue_notes[] = {
    523.25f,
    659.25f,
    783.99f,
    1046.50f
};

static const uint32_t s_cue_note_count = sizeof(s_cue_notes) / sizeof(s_cue_notes[0]);

void fallback_audio_init(void) {
    s_enabled = false;
    s_phase = 0.0f;
    printf("Generated fallback audio initialized\n");
}

void fallback_audio_set_enabled(bool enabled) {
    s_enabled = enabled;

    if (s_enabled) {
        audio_pwm_output_set_sample_rate(FALLBACK_SAMPLE_RATE);
    } else {
        s_cue_active = false;
        s_cue_sample_index = 0;
    }

    if (!s_enabled) {
        audio_buf_stop();
    }
}

bool fallback_audio_is_enabled(void) {
    return s_enabled;
}

void fallback_audio_start_bt_arpeggio(void) {
    audio_pwm_output_set_sample_rate(FALLBACK_SAMPLE_RATE);
    audio_buf_stop();

    s_enabled = false;
    s_cue_active = true;
    s_cue_sample_index = 0;
    s_phase = 0.0f;

    printf("BT cue: arpeggio\n");
}

bool fallback_audio_cue_is_active(void) {
    return s_cue_active;
}

void fallback_audio_task(void) {
    if (!s_enabled && !s_cue_active) {
        return;
    }

    static int16_t samples[FALLBACK_BLOCK_SAMPLES];

    if (s_cue_active) {
        uint32_t total_cue_samples = s_cue_note_count * CUE_NOTE_SAMPLES;

        for (int i = 0; i < FALLBACK_BLOCK_SAMPLES; i++) {
            if (s_cue_sample_index >= total_cue_samples) {
                samples[i] = 0;
                s_cue_active = false;
                continue;
            }

            uint32_t note_index = s_cue_sample_index / CUE_NOTE_SAMPLES;
            uint32_t note_pos = s_cue_sample_index % CUE_NOTE_SAMPLES;
            float env = 1.0f;

            if (note_pos < 220) {
                env = (float)note_pos / 220.0f;
            } else if (note_pos > CUE_NOTE_SAMPLES - 441) {
                env = (float)(CUE_NOTE_SAMPLES - note_pos) / 441.0f;
            }

            float phase_step = 2.0f * PI_F * s_cue_notes[note_index] / FALLBACK_SAMPLE_RATE;
            float value = sinf(s_phase);

            samples[i] = (int16_t)(value * env * 5000.0f);

            s_phase += phase_step;
            if (s_phase >= 2.0f * PI_F) {
                s_phase -= 2.0f * PI_F;
            }

            s_cue_sample_index++;
        }

        audio_buf_push(samples, FALLBACK_BLOCK_SAMPLES, AUDIO_SRC_FALLBACK);
        return;
    }

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
