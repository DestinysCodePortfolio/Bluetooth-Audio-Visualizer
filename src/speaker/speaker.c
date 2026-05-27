#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <math.h>

#define AUDIO_PIN_L 0        // GP0 - LEFT SPEAKER
#define AUDIO_PIN_R 1        // GP1  - RIGHT SPEAKER 
#define SAMPLE_RATE 44100
#define PWM_RANGE 256        // 0 - 255

// AUDIO INITALIZATION
void audio_init() {
    // Setup LEFT channel
    gpio_set_function(AUDIO_PIN_L, GPIO_FUNC_PWM); // SLICES CHANNELS 
    uint slice_l = pwm_gpio_to_slice_num(AUDIO_PIN_L);
    
    // Setup RIGHT channel
    gpio_set_function(AUDIO_PIN_R, GPIO_FUNC_PWM);
    uint slice_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);

    // Set PWM frequency
    // clk_sys (125MHz) / (PWM_RANGE * SAMPLE_RATE) = divider
    float divider = (float)clock_get_hz(clk_sys) / (SAMPLE_RATE * PWM_RANGE);
    
    pwm_set_clkdiv(slice_l, divider);
    pwm_set_clkdiv(slice_r, divider);
    
    pwm_set_wrap(slice_l, PWM_RANGE - 1);
    pwm_set_wrap(slice_r, PWM_RANGE - 1);
    
    pwm_set_enabled(slice_l, true);
    pwm_set_enabled(slice_r, true);
}

// Play a tone on both channels
// freq = frequency in Hz, duration_ms = how long to play
void play_tone(uint freq, uint duration_ms) {
    uint slice_l = pwm_gpio_to_slice_num(AUDIO_PIN);
    uint slice_r = pwm_gpio_to_slice_num(AUDIO_PIN_R);
    
    uint chan_l = pwm_gpio_to_channel(AUDIO_PIN);
    uint chan_r = pwm_gpio_to_channel(AUDIO_PIN_R);

    // How many samples per wave cycle
    uint samples_per_cycle = SAMPLE_RATE / freq;
    uint total_samples = (SAMPLE_RATE * duration_ms) / 1000;

    for (uint i = 0; i < total_samples; i++) {
        // Generate sine wave sample, scaled to PWM range
        float t = (float)(i % samples_per_cycle) / samples_per_cycle;
        uint16_t sample = (uint16_t)((sinf(2.0f * M_PI * t) + 1.0f) * (PWM_RANGE / 2));

        pwm_set_chan_level(slice_l, chan_l, sample);
        pwm_set_chan_level(slice_r, chan_r, sample);

        // Wait for next sample period
        sleep_us(1000000 / SAMPLE_RATE);
    }

    // Silence after tone
    pwm_set_chan_level(slice_l, chan_l, PWM_RANGE / 2);  // midpoint = silence
    pwm_set_chan_level(slice_r, chan_r, PWM_RANGE / 2);
}

int main() {
    stdio_init_all();
    audio_init();

    while (true) {
        play_tone(440, 500);   // A4 note, 0.5s
        sleep_ms(200);
        play_tone(523, 500);   // C5 note, 0.5s
        sleep_ms(200);
        play_tone(659, 500);   // E5 note, 0.5s
        sleep_ms(500);
    }
}