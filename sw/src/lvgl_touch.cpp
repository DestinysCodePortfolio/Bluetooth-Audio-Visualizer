#include "lvgl_touch.h"

#include <stdio.h>
#include <stdlib.h>

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 272

// You must calibrate these! 
// Resistive screens rarely go all the way to 0 or 4095.
// Use a test script to press the corners and find your actual min/max ADC values.
#define ADC_X_MIN 315
#define ADC_X_MAX 3750
#define ADC_Y_MIN 600
#define ADC_Y_MAX 3350

uint _x_pos, _x_neg, _y_pos, _y_neg;

// Function to check if the screen is currently being pressed
bool is_touched() {
  // Ground the X- layer
  gpio_init(_x_neg);
  gpio_set_dir(_x_neg, GPIO_OUT);
  gpio_put(_x_neg, 0);

  // Set Y+ as an input and turn on the Pico's internal pull-up resistor
  gpio_init(_y_pos);
  gpio_set_dir(_y_pos, GPIO_IN);
  gpio_pull_up(_y_pos);

  // Set other pins to high-impedance (floating) so they don't interfere
  gpio_init(_x_pos);
  gpio_set_dir(_x_pos, GPIO_IN);
  gpio_disable_pulls(_x_pos);
  gpio_init(_y_neg);
  gpio_set_dir(_y_neg, GPIO_IN);
  gpio_disable_pulls(_y_neg);

  sleep_us(10);  // Give the voltage a tiny fraction of a second to settle

  // If the screen is touched, the grounded X- layer touches the Y+ layer,
  // overcoming the pull-up resistor and pulling the pin LOW (0).
  bool touched = !gpio_get(_y_pos);
  return touched;
}

// Function to read the X coordinate
uint16_t read_x() {
  // 1. Create the X-gradient: X+ to High (3.3V), X- to Low (GND)
  gpio_init(_x_pos);
  gpio_set_dir(_x_pos, GPIO_OUT);
  gpio_put(_x_pos, 1);
  gpio_init(_x_neg);
  gpio_set_dir(_x_neg, GPIO_OUT);
  gpio_put(_x_neg, 0);

  // 2. Set Y- to high-impedance so it doesn't interfere
  gpio_init(_y_neg);
  gpio_set_dir(_y_neg, GPIO_IN);
  gpio_disable_pulls(_y_neg);

  // 3. Set Y+ to ADC mode to read the voltage
  adc_gpio_init(_y_pos);
  adc_select_input(1);  // ADC1 corresponds to GP27 (Y_POS)

  sleep_us(10);  // Settle time  
  return adc_read();
}

// Function to read the Y coordinate
uint16_t read_y() {
  // 1. Create the Y-gradient: Y+ to High (3.3V), Y- to Low (GND)
  gpio_init(_y_pos);
  gpio_set_dir(_y_pos, GPIO_OUT);
  gpio_put(_y_pos, 1);
  gpio_init(_y_neg);
  gpio_set_dir(_y_neg, GPIO_OUT);
  gpio_put(_y_neg, 0);

  // 2. Set X- to high-impedance so it doesn't interfere
  gpio_init(_x_neg);
  gpio_set_dir(_x_neg, GPIO_IN);
  gpio_disable_pulls(_x_neg);

  // 3. Set X+ to ADC mode to read the voltage
  adc_gpio_init(_x_pos);
  adc_select_input(0);  // ADC0 corresponds to GP26 (X_POS)

  sleep_us(10);  // Settle time
  return adc_read();
}

// Take 10 samples and average them to kill random noise spikes
uint16_t get_smoothed_x() {
    uint32_t sum = 0;
    const int samples = 10;
    for(int i = 0; i < samples; i++) {
        sum += read_x(); // From the previous code
    }
    return sum / samples;
}

uint16_t get_smoothed_y() {
    uint32_t sum = 0;
    const int samples = 10;
    for(int i = 0; i < samples; i++) {
        sum += read_y(); // From the previous code
    }
    return sum / samples;
}

// Standard Arduino-style map function to convert ADC ranges to Pixel ranges
int32_t map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

lv_indev_t* indev;

bool touch_init() { return touch_init(X_POS, X_NEG, Y_POS, Y_NEG); }

bool touch_init(int x_pos, int x_neg, int y_pos, int y_neg) {
  _x_pos = x_pos;
  _x_neg = x_neg;
  _y_pos = y_pos;
  _y_neg = y_neg;

  indev = lv_indev_create();
  if (!indev) {
    printf("Unable to allocate indev object for touch\n");
    return -1;
  }
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read);

  return 0;
}

void touch_read(lv_indev_t* indev, lv_indev_data_t* data) {
  // Static variables persist between function calls to remember history
    static int16_t last_valid_x = 0;
    static int16_t last_valid_y = 0;
    static bool was_pressed = false;
    
    if(is_touched()) {
        uint16_t raw_x = get_smoothed_x();
        uint16_t raw_y = get_smoothed_y();

        int16_t pixel_x = map(raw_x, ADC_X_MIN, ADC_X_MAX, 0, SCREEN_WIDTH);
        int16_t pixel_y = map(raw_y, ADC_Y_MIN, ADC_Y_MAX, 0, SCREEN_HEIGHT);
        
        // Clamp to screen boundaries
        if(pixel_x < 0) pixel_x = 0;
        if(pixel_x > SCREEN_WIDTH - 1) pixel_x = SCREEN_WIDTH - 1;
        if(pixel_y < 0) pixel_y = 0;
        if(pixel_y > SCREEN_HEIGHT - 1) pixel_y = SCREEN_HEIGHT - 1;
        
        // --- THE LIFT-OFF FILTER ---
        if (was_pressed) {
            // Calculate how far the coordinate jumped since the last 20ms frame
            int diff_x = abs(pixel_x - last_valid_x);
            int diff_y = abs(pixel_y - last_valid_y);
            
            // If the jump is larger than a reasonable threshold (e.g., 40 pixels),
            // it's almost certainly lift-off noise.
            if (diff_x > 40 || diff_y > 40) {
                // Abort the press. Pass the last known good coordinates and force a release.
                data->point.x = last_valid_x;
                data->point.y = last_valid_y;
                return; // Exit the function early
            }
        }
        
        // If it passed the filter, it's a valid, stable press.
        last_valid_x = pixel_x;
        last_valid_y = pixel_y;
        
        data->point.x = SCREEN_WIDTH - pixel_x;
        data->point.y = SCREEN_HEIGHT - pixel_y;
        data->state = LV_INDEV_STATE_PR;
        was_pressed = true;
        
    } else {
        // The screen is physically not being touched
        data->point.x = last_valid_x;
        data->point.y = last_valid_y;
        data->state = LV_INDEV_STATE_REL;
        last_valid_x = 0;
        last_valid_y = 0;
        was_pressed = false;
    }
}