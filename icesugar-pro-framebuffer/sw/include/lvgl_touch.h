#ifndef __LVGL_TOUCH_H__
#define __LVGL_TOUCH_H__

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <lvgl.h>

// Define the pins connected to the touchscreen
#define X_POS 26 // Must be an ADC pin (ADC0)
#define Y_POS 27 // Must be an ADC pin (ADC1)
#define X_NEG 16 // Standard GPIO
#define Y_NEG 17 // Standard GPIO

bool is_touched();
uint16_t read_x();
uint16_t read_y();

bool touch_init();
bool touch_init(int x_pos, int x_neg, int y_pos, int y_neg);
void touch_read(lv_indev_t * indev, lv_indev_data_t * data);
#endif