#include "audio_pwm_output.h"
#include "audio_buffer.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#define AUDIO_LEFT_PIN   26
#define AUDIO_RIGHT_PIN  27
#define SAMPLE_RATE      44100
#define PWM_WRAP         2047

// Lower volume to reduce clipping/wobble through PAM8403.
// Try 4 first. If too quiet, change to 2.
#define VOLUME_DIVIDER   4

static bool s_enabled = false;

static uint s_slice_l;
static uint s_slice_r;
static uint s_chan_l;
static uint s_chan_r;

static int16_t last_sample = 0;
static uint32_t s_sample_rate = 44100;

static void pwm_irq_handler(void) {
    pwm_clear_irq(s_slice_l);

    if (!s_enabled) {
        pwm_set_chan_level(s_slice_l, s_chan_l, PWM_WRAP / 2);
        pwm_set_chan_level(s_slice_r, s_chan_r, PWM_WRAP / 2);
        return;
    }

    int16_t sample = audio_buf_read_one();

    // If the buffer underruns, audio_buf_read_one() returns 0.
    // Instead of jumping hard to silence, smooth it slightly.
    if (sample == 0) {
        sample = last_sample / 2;
    }

    last_sample = sample;

    sample = sample / VOLUME_DIVIDER;

    uint16_t level =
        (uint16_t)(((int32_t)sample + 32768) * PWM_WRAP / 65535);

    if (level > PWM_WRAP) {
        level = PWM_WRAP;
    }

    pwm_set_chan_level(s_slice_l, s_chan_l, level);
    pwm_set_chan_level(s_slice_r, s_chan_r, level);
}

void audio_pwm_output_init(void) {
    gpio_set_function(AUDIO_LEFT_PIN, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_RIGHT_PIN, GPIO_FUNC_PWM);

    s_slice_l = pwm_gpio_to_slice_num(AUDIO_LEFT_PIN);
    s_slice_r = pwm_gpio_to_slice_num(AUDIO_RIGHT_PIN);

    s_chan_l = pwm_gpio_to_channel(AUDIO_LEFT_PIN);
    s_chan_r = pwm_gpio_to_channel(AUDIO_RIGHT_PIN);

    pwm_config cfg = pwm_get_default_config();

    float sys_clk = (float)clock_get_hz(clk_sys);
    float divider = sys_clk / ((float)s_sample_rate * (float)(PWM_WRAP + 1));

    pwm_config_set_clkdiv(&cfg, divider);
    pwm_config_set_wrap(&cfg, PWM_WRAP);

    pwm_init(s_slice_l, &cfg, false);

    if (s_slice_r != s_slice_l) {
        pwm_init(s_slice_r, &cfg, false);
    }

    pwm_set_chan_level(s_slice_l, s_chan_l, PWM_WRAP / 2);
    pwm_set_chan_level(s_slice_r, s_chan_r, PWM_WRAP / 2);

    pwm_clear_irq(s_slice_l);
    pwm_set_irq_enabled(s_slice_l, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_set_mask_enabled((1u << s_slice_l) | (1u << s_slice_r));

    s_enabled = true;

    printf("Audio PWM output init: GP%d slice=%u chan=%u, GP%d slice=%u chan=%u @ %d Hz, wrap=%d, div=%.4f\n",
           AUDIO_LEFT_PIN,
           s_slice_l,
           s_chan_l,
           AUDIO_RIGHT_PIN,
           s_slice_r,
           s_chan_r,
           s_sample_rate,
           PWM_WRAP,
           (double)divider);
}

void audio_pwm_output_set_enabled(bool e) {
    s_enabled = e;
}

bool audio_pwm_output_is_enabled(void) {
    return s_enabled;
}

void audio_pwm_output_set_sample_rate(uint32_t sample_rate) {
    if (sample_rate == 0) {
        sample_rate = 44100;
    }

    
    if (sample_rate == s_sample_rate) {
        return;
    }

    s_sample_rate = sample_rate;

    pwm_config cfg = pwm_get_default_config();

    float sys_clk = (float)clock_get_hz(clk_sys);
    float divider = sys_clk / ((float)s_sample_rate * (float)(PWM_WRAP + 1));

    pwm_config_set_clkdiv(&cfg, divider);
    pwm_config_set_wrap(&cfg, PWM_WRAP);

    bool was_enabled = s_enabled;
    s_enabled = false;

    pwm_init(s_slice_l, &cfg, false);

    if (s_slice_r != s_slice_l) {
        pwm_init(s_slice_r, &cfg, false);
    }

    pwm_set_chan_level(s_slice_l, s_chan_l, PWM_WRAP / 2);
    pwm_set_chan_level(s_slice_r, s_chan_r, PWM_WRAP / 2);

    pwm_set_mask_enabled((1u << s_slice_l) | (1u << s_slice_r));

    s_enabled = was_enabled;

    printf("PWM sample rate changed to %lu Hz, div=%.4f\n",
           (unsigned long)s_sample_rate,
           (double)divider);
}