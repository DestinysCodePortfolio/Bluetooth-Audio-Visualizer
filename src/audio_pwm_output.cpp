#include "audio_pwm_output.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#define AUDIO_LEFT_PIN   26
#define AUDIO_RIGHT_PIN  27

static bool enabled = true;
static bool level = false;
static repeating_timer_t tone_timer;

static bool tone_timer_cb(repeating_timer_t *rt) {
    (void)rt;

    if (!enabled) {
        gpio_put(AUDIO_LEFT_PIN, 0);
        gpio_put(AUDIO_RIGHT_PIN, 0);
        return true;
    }

    level = !level;

    gpio_put(AUDIO_LEFT_PIN, level);
    gpio_put(AUDIO_RIGHT_PIN, level);

    return true;
}

void audio_pwm_output_init(void) {
    gpio_init(AUDIO_LEFT_PIN);
    gpio_set_dir(AUDIO_LEFT_PIN, GPIO_OUT);

    gpio_init(AUDIO_RIGHT_PIN);
    gpio_set_dir(AUDIO_RIGHT_PIN, GPIO_OUT);

    gpio_put(AUDIO_LEFT_PIN, 0);
    gpio_put(AUDIO_RIGHT_PIN, 0);

    // Toggle every ~1136 us gives about 440 Hz full cycle.
    add_repeating_timer_us(-1136, tone_timer_cb, NULL, &tone_timer);

    printf("GPIO TEST TONE enabled on GP26 and GP27\n");
}

void audio_pwm_output_set_enabled(bool e) {
    enabled = e;
}

bool audio_pwm_output_is_enabled(void) {
    return enabled;
}