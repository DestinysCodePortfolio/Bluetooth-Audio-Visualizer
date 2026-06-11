#include "spi_display.h"
#include "lv_conf.h"
#include "lvgl_oscilloscope.h"   // <-- oscilloscope instead of demo widgets
#include "audio_buffer.h"

#include <lvgl.h>

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/binary_info.h>
#include <pico/time.h>
#include <hardware/spi.h>
#include <pico/cyw43_arch.h>
#include <hardware/adc.h>
#include <cmath>

/* ── Timing callback ──────────────────────────────────────────────────────── */
uint32_t cs122_get_millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

/* ── Flush buffer (same as your original) ────────────────────────────────── */
static uint8_t buffer[OLEDRGB_WIDTH * OLEDRGB_HEIGHT / 10];

void cs122_flush_cb_partial(lv_display_t *disp,
                            const lv_area_t *area,
                            uint8_t *px_buf)
{
    ucr::bcoe::SPIDisplay *spi_display =
        reinterpret_cast<ucr::bcoe::SPIDisplay *>(lv_display_get_user_data(disp));

    spi_display->drawBitmap(area->x1, area->y1,
                            area->x2, area->y2,
                            px_buf);

    lv_display_flush_ready(disp);
}

/* ── Stub: inject a fake sine wave so the scope renders something ─────────
 *   Remove / replace this once real ADC/I2S audio is wired up.
 * ─────────────────────────────────────────────────────────────────────────── */
static void feed_fake_audio(void)
{
    static float phase = 0.0f;
    const float freq_hz   = 440.0f;          // A4 tone
    const float sample_hz = 44100.0f;
    const float inc       = 2.0f * (float)M_PI * freq_hz / sample_hz;

    int16_t samples[SCOPE_SAMPLES];
    for (int i = 0; i < SCOPE_SAMPLES; i++) {
        samples[i] = (int16_t)(sinf(phase) * 28000.0f);
        phase += inc;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }

    // Push into the ring buffer as if it came from Bluetooth
    audio_buf_push(samples, SCOPE_SAMPLES, AUDIO_SRC_BLUETOOTH);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    stdio_init_all();
    cyw43_arch_init();
    adc_init();

    ucr::bcoe::SPIDisplay spi_display(480, 272, 1000000, 20);
    spi_display.begin();
    spi_display.clear();

    static uint16_t black_line[480];
    memset(black_line, 0x00, sizeof(black_line));
    for (int y = 0; y < 272; y++) {
        spi_display.drawBitmap(0, y, 479, y,
                               reinterpret_cast<uint8_t*>(black_line));
    }
    // Pre-fill the audio buffer with one frame so the scope isn't blank
    // on the very first draw.
    feed_fake_audio();

    ucr::bcoe::cs::cs122::LVGL_Oscilloscope app(
        &spi_display,
        cs122_flush_cb_partial,
        cs122_get_millis
    );

    // The oscilloscope's run() calls build_ui() then enters the LVGL timer loop.
    // While that loop is spinning you need to keep topping up the audio buffer.
    // The simplest way is to override loop() or call feed_fake_audio() in a
    // repeating alarm, but for a quick smoke-test you can just call run() and
    // accept a single static waveform frame.
    app.run();   // blocks forever (LVGL event loop inside)
} 