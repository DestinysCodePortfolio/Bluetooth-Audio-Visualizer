#include "audio_pwm_output.h"
#include "audio_buffer.h"

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#define AUDIO_LEFT_PIN   26
#define AUDIO_RIGHT_PIN  27
#define SAMPLE_RATE      44100

// PWM wrap value chosen as a power-of-two for simple int16 -> level mapping.
// sys_clock / (divider * PWM_WRAP) = SAMPLE_RATE
// At 132 MHz: divider = 132000000 / (44100 * 2048) ≈ 1.46
#define PWM_WRAP         2048

static bool s_enabled = false;
static uint s_slice_l;
static uint s_slice_r;

static void pwm_irq_handler(void) {
    // Clear the interrupt on the left slice (right follows the same clock).
    pwm_clear_irq(s_slice_l);

    if (!s_enabled) {
        pwm_set_chan_level(s_slice_l, PWM_CHAN_A, PWM_WRAP / 2);
        pwm_set_chan_level(s_slice_r, PWM_CHAN_A, PWM_WRAP / 2);
        return;
    }

    // Pull the *next* sample from the ring buffer using the advancing read
    // cursor.  The old code called audio_buf_copy_latest() which always
    // returned the newest sample — meaning every IRQ replayed the same value
    // instead of streaming forward through the buffer.
    int16_t sample = audio_buf_read_one();

    // Map int16 [-32768, 32767] → [0, PWM_WRAP]
    uint16_t level = (uint16_t)(((int32_t)sample + 32768) * PWM_WRAP / 65536);

    pwm_set_chan_level(s_slice_l, PWM_CHAN_A, level);
    pwm_set_chan_level(s_slice_r, PWM_CHAN_A, level);
}

void audio_pwm_output_init(void) {
    gpio_set_function(AUDIO_LEFT_PIN,  GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_RIGHT_PIN, GPIO_FUNC_PWM);

    s_slice_l = pwm_gpio_to_slice_num(AUDIO_LEFT_PIN);
    s_slice_r = pwm_gpio_to_slice_num(AUDIO_RIGHT_PIN);

    pwm_config cfg = pwm_get_default_config();

    float sys_clk = (float)clock_get_hz(clk_sys);
    float divider  = sys_clk / (SAMPLE_RATE * PWM_WRAP);
    pwm_config_set_clkdiv(&cfg, divider);
    pwm_config_set_wrap(&cfg, PWM_WRAP - 1);

    pwm_init(s_slice_l, &cfg, false);
    pwm_init(s_slice_r, &cfg, false);

    // Start both channels at mid-scale (silence).
    pwm_set_chan_level(s_slice_l, PWM_CHAN_A, PWM_WRAP / 2);
    pwm_set_chan_level(s_slice_r, PWM_CHAN_A, PWM_WRAP / 2);

    // IRQ fires once per PWM wrap = once per output sample @ SAMPLE_RATE Hz.
    pwm_clear_irq(s_slice_l);
    pwm_set_irq_enabled(s_slice_l, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // Start both slices simultaneously.
    pwm_set_mask_enabled((1u << s_slice_l) | (1u << s_slice_r));

    s_enabled = true;
    printf("Audio PWM output init: GP%d GP%d @ %d Hz, wrap=%d, div=%.4f\n",
           AUDIO_LEFT_PIN, AUDIO_RIGHT_PIN, SAMPLE_RATE, PWM_WRAP,
           (double)divider);
}

void audio_pwm_output_set_enabled(bool e) {
    s_enabled = e;
}

bool audio_pwm_output_is_enabled(void) {
    return s_enabled;
}