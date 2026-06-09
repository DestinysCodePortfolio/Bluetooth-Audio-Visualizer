#ifndef AUDIO_PWM_OUTPUT_H
#define AUDIO_PWM_OUTPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_pwm_output_init(void);
void audio_pwm_output_set_enabled(bool enabled);
bool audio_pwm_output_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif