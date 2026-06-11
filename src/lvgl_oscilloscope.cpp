#include "lvgl_oscilloscope.h"
#include "audio_buffer.h"
#include "lv_conf.h"

#include <lvgl.h>
#include <cmath>
#include <cstring>
#include <stdint.h>
#include <stdio.h>

#define SCR_W       480
#define SCR_H       272

#define CANVAS_X    0
#define CANVAS_Y    30
#define CANVAS_W    SCR_W
#define CANVAS_H    (SCR_H - 30)
#define MID_Y       (CANVAS_H / 2)

#define GRID_COLS   8
#define GRID_ROWS   4
#define BAR_COUNT   32

#define INT16_MAX_F 32767.0f
#define SCOPE_REFRESH_MS  33

#define VIS_MODE_SCOPE 0
#define VIS_MODE_BARS  1
#define VIS_MODE_COUNT 2

static volatile uint8_t g_visualizer_mode = VIS_MODE_SCOPE;

extern "C" void lvgl_visualizer_next_mode(void)
{
    g_visualizer_mode = (uint8_t)((g_visualizer_mode + 1u) % VIS_MODE_COUNT);
    printf("Visualizer mode: %s\n", lvgl_visualizer_mode_name());
}

extern "C" uint8_t lvgl_visualizer_get_mode(void)
{
    return g_visualizer_mode;
}

extern "C" const char *lvgl_visualizer_mode_name(void)
{
    switch (g_visualizer_mode) {
        case VIS_MODE_BARS:  return "BARS";
        case VIS_MODE_SCOPE:
        default:             return "SCOPE";
    }
}

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {

LVGL_Oscilloscope::LVGL_Oscilloscope(SPIDisplay            *spi_disp,
                                     lv_display_flush_cb_t  fcallback,
                                     lv_tick_get_cb_t       tcallback)
    : CS122_App(spi_disp, fcallback, tcallback),
      canvas(nullptr),
      label_source(nullptr),
      label_mode(nullptr),
      label_info(nullptr),
      canvas_data(nullptr),
      scope_timer(nullptr)
{
    // main.cpp initializes the shared audio buffer once before starting display.
}

static inline int32_t abs_i16(int16_t v)
{
    return v < 0 ? -(int32_t)v : (int32_t)v;
}

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Dynamic color mapping from the proposal:
// quiet = cyan/blue, medium = purple/magenta, loud/beat = yellow/red.
lv_color_t LVGL_Oscilloscope::amplitude_color(float norm)
{
    norm = clamp01(norm);

    if (norm < 0.30f) {
        float t = norm / 0.30f;
        return lv_color_make(0, (uint8_t)(210 + 45 * t), 255);
    }

    if (norm < 0.62f) {
        float t = (norm - 0.30f) / 0.32f;
        return lv_color_make((uint8_t)(80 + 175 * t),
                             (uint8_t)(120 - 80 * t),
                             255);
    }

    if (norm < 0.84f) {
        float t = (norm - 0.62f) / 0.22f;
        return lv_color_make(255,
                             (uint8_t)(40 + 120 * t),
                             (uint8_t)(255 - 220 * t));
    }

    float t = (norm - 0.84f) / 0.16f;
    return lv_color_make(255, (uint8_t)(160 - 160 * t), 0);
}

lv_color_t LVGL_Oscilloscope::band_color(uint32_t band, float norm, bool beat_flash)
{
    float x = (float)band / (float)(BAR_COUNT - 1);
    norm = clamp01(norm);

    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (x < 0.25f) {
        float t = x / 0.25f;
        r = (uint8_t)(0 + 70 * t);
        g = (uint8_t)(230 - 80 * t);
        b = 255;
    } else if (x < 0.55f) {
        float t = (x - 0.25f) / 0.30f;
        r = (uint8_t)(70 + 185 * t);
        g = (uint8_t)(150 - 120 * t);
        b = 255;
    } else if (x < 0.78f) {
        float t = (x - 0.55f) / 0.23f;
        r = 255;
        g = (uint8_t)(30 + 60 * t);
        b = (uint8_t)(255 - 150 * t);
    } else {
        float t = (x - 0.78f) / 0.22f;
        r = (uint8_t)(180 - 40 * t);
        g = (uint8_t)(80 + 120 * t);
        b = 255;
    }

    // Louder bars get brighter, beat flash gets a small warm boost.
    float gain = 0.40f + 0.60f * norm;
    if (beat_flash) gain = 1.0f;

    r = (uint8_t)fminf(255.0f, r * gain + (beat_flash ? 25.0f : 0.0f));
    g = (uint8_t)fminf(255.0f, g * gain + (beat_flash ? 10.0f : 0.0f));
    b = (uint8_t)fminf(255.0f, b * gain + (beat_flash ? 25.0f : 0.0f));

    return lv_color_make(r, g, b);
}

static void canvas_draw_line(lv_obj_t *canvas,
                             const lv_point_precise_t *p1,
                             const lv_point_precise_t *p2,
                             const lv_draw_line_dsc_t *dsc)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_line_dsc_t line_dsc = *dsc;
    line_dsc.p1 = *p1;
    line_dsc.p2 = *p2;
    line_dsc.points = NULL;
    line_dsc.point_cnt = 0;

    lv_draw_line(&layer, &line_dsc);
    lv_canvas_finish_layer(canvas, &layer);
}

static void draw_hline(lv_obj_t *canvas, int x1, int x2, int y,
                       lv_color_t color, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = opa;
    dsc.round_start = 0;
    dsc.round_end = 0;

    lv_point_precise_t p1 = {(lv_value_precise_t)x1, (lv_value_precise_t)y};
    lv_point_precise_t p2 = {(lv_value_precise_t)x2, (lv_value_precise_t)y};
    canvas_draw_line(canvas, &p1, &p2, &dsc);
}

static void draw_vline(lv_obj_t *canvas, int x, int y1, int y2,
                       lv_color_t color, int width, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa = opa;
    dsc.round_start = 1;
    dsc.round_end = 1;

    lv_point_precise_t p1 = {(lv_value_precise_t)x, (lv_value_precise_t)y1};
    lv_point_precise_t p2 = {(lv_value_precise_t)x, (lv_value_precise_t)y2};
    canvas_draw_line(canvas, &p1, &p2, &dsc);
}

static void draw_grid(lv_obj_t *canvas, bool beat_flash)
{
    lv_color_t grid = beat_flash ? lv_color_hex(0x332244) : lv_color_hex(0x003300);

    for (int col = 1; col < GRID_COLS; col++) {
        int x = col * CANVAS_W / GRID_COLS;
        draw_vline(canvas, x, 0, CANVAS_H - 1, grid, 1, LV_OPA_COVER);
    }

    for (int row = 1; row < GRID_ROWS; row++) {
        int y = row * CANVAS_H / GRID_ROWS;
        draw_hline(canvas, 0, CANVAS_W - 1, y, grid, 1, LV_OPA_COVER);
    }

    draw_hline(canvas, 0, CANVAS_W - 1, MID_Y,
               beat_flash ? lv_color_hex(0x553355) : lv_color_hex(0x004400),
               1, LV_OPA_COVER);
}

void LVGL_Oscilloscope::build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, SCR_W, 28);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(0x001018), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(topbar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(topbar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(topbar);
    lv_label_set_text(lbl_title, "AUDIO VISUALIZER");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00FFCC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 8, 0);

    label_source = lv_label_create(topbar);
    lv_label_set_text(label_source, "NO SIGNAL");
    lv_obj_set_style_text_color(label_source, lv_color_hex(0x556655), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_source, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_source, LV_ALIGN_RIGHT_MID, -8, 0);

    label_mode = lv_label_create(topbar);
    lv_label_set_text(label_mode, "SCOPE");
    lv_obj_set_style_text_color(label_mode, lv_color_hex(0xFF66FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_mode, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_mode, LV_ALIGN_CENTER, -58, 0);

    label_info = lv_label_create(topbar);
    lv_label_set_text(label_info, "BTN GP5");
    lv_obj_set_style_text_color(label_info, lv_color_hex(0x44AAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_info, LV_ALIGN_CENTER, 60, 0);

    uint32_t canvas_buf_size = (uint32_t)CANVAS_W * CANVAS_H * 2;
    canvas_data = new uint8_t[canvas_buf_size];
    memset(canvas_data, 0, canvas_buf_size);

    canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_data, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, CANVAS_X, CANVAS_Y);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

    scope_timer = lv_timer_create(scope_timer_cb, SCOPE_REFRESH_MS, this);
}

void LVGL_Oscilloscope::scope_timer_cb(lv_timer_t *timer)
{
    LVGL_Oscilloscope *self =
        reinterpret_cast<LVGL_Oscilloscope *>(lv_timer_get_user_data(timer));

    if (self) {
        self->redraw_scope();
    }
}

void LVGL_Oscilloscope::redraw_scope(void)
{
    static int16_t snap[SCOPE_SAMPLES];
    static float bar_smooth[BAR_COUNT];
    static float beat_env = 0.0f;
    static uint8_t beat_flash = 0;

    audio_source_t src = audio_buf_snapshot(snap);

    switch (src) {
        case AUDIO_SRC_BLUETOOTH:
            lv_label_set_text(label_source, "BLUETOOTH");
            lv_obj_set_style_text_color(label_source,
                                        lv_color_hex(0x00CCFF), LV_PART_MAIN);
            break;

        case AUDIO_SRC_SD:
            lv_label_set_text(label_source, "SD CARD");
            lv_obj_set_style_text_color(label_source,
                                        lv_color_hex(0xFFCC00), LV_PART_MAIN);
            break;

        case AUDIO_SRC_FALLBACK:
            lv_label_set_text(label_source, "FALLBACK");
            lv_obj_set_style_text_color(label_source,
                                        lv_color_hex(0xCC88FF), LV_PART_MAIN);
            break;

        default:
            lv_label_set_text(label_source, "NO SIGNAL");
            lv_obj_set_style_text_color(label_source,
                                        lv_color_hex(0x334433), LV_PART_MAIN);
            break;
    }

    int32_t peak = 0;
    uint64_t energy = 0;

    for (int i = 0; i < SCOPE_SAMPLES; i++) {
        int32_t a = abs_i16(snap[i]);
        if (a > peak) peak = a;
        energy += (uint32_t)a;
    }

    float peak_norm = clamp01((float)peak / INT16_MAX_F);
    float avg_norm = (float)energy / ((float)SCOPE_SAMPLES * INT16_MAX_F);

    beat_env = beat_env * 0.91f + peak_norm * 0.09f;

    if (src != AUDIO_SRC_NONE && peak > 600 && peak_norm > 0.30f &&
        peak_norm > beat_env * 1.20f) {
        beat_flash = 6;
    } else if (beat_flash > 0) {
        beat_flash--;
    }

    bool flash = beat_flash > 0;
    uint8_t mode = lvgl_visualizer_get_mode();

    lv_label_set_text(label_mode, mode == VIS_MODE_BARS ? "BARS" : "SCOPE");
    lv_obj_set_style_text_color(label_mode,
                                mode == VIS_MODE_BARS ? lv_color_hex(0xFF44FF)
                                                      : lv_color_hex(0x00FFCC),
                                LV_PART_MAIN);

    if (src == AUDIO_SRC_NONE || peak < 64) {
        lv_label_set_text(label_info, "WAITING");
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
        draw_grid(canvas, false);
        draw_hline(canvas, 0, CANVAS_W - 1, MID_Y, lv_color_hex(0x004400), 1,
                   LV_OPA_COVER);
        lv_obj_invalidate(canvas);
        return;
    }

    lv_label_set_text_fmt(label_info, "P%02d A%02d", (int)(peak_norm * 99.0f),
                          (int)(avg_norm * 99.0f));

    // ------------------------------------------------------------
    // Mode 1: oscilloscope trace with dynamic color mapping.
    // ------------------------------------------------------------
    if (mode == VIS_MODE_SCOPE) {
        lv_canvas_fill_bg(canvas,
                          flash ? lv_color_hex(0x120018) : lv_color_black(),
                          LV_OPA_COVER);
        draw_grid(canvas, flash);

        lv_draw_line_dsc_t wave_dsc;
        lv_draw_line_dsc_init(&wave_dsc);
        wave_dsc.width = flash ? 3 : 2;
        wave_dsc.opa = LV_OPA_COVER;
        wave_dsc.round_start = 1;
        wave_dsc.round_end = 1;

        float scale = (float)(CANVAS_H / 2 - 6) / INT16_MAX_F;
        int prev_x = 0;
        int prev_y = MID_Y - (int)((float)snap[0] * scale);
        if (prev_y < 0) prev_y = 0;
        if (prev_y >= CANVAS_H) prev_y = CANVAS_H - 1;

        for (int i = 1; i < SCOPE_SAMPLES; i++) {
            int cur_x = i * CANVAS_W / SCOPE_SAMPLES;
            int cur_y = MID_Y - (int)((float)snap[i] * scale);
            if (cur_y < 0) cur_y = 0;
            if (cur_y >= CANVAS_H) cur_y = CANVAS_H - 1;

            float local_amp = (float)abs_i16(snap[i]) / INT16_MAX_F;
            wave_dsc.color = flash ? lv_color_hex(0xFF66FF)
                                   : amplitude_color(local_amp);

            lv_point_precise_t p1 = {(lv_value_precise_t)prev_x,
                                     (lv_value_precise_t)prev_y};
            lv_point_precise_t p2 = {(lv_value_precise_t)cur_x,
                                     (lv_value_precise_t)cur_y};
            canvas_draw_line(canvas, &p1, &p2, &wave_dsc);

            if (local_amp > 0.38f) {
                lv_draw_line_dsc_t glow_dsc = wave_dsc;
                glow_dsc.width = flash ? 7 : 5;
                glow_dsc.opa = LV_OPA_20;
                canvas_draw_line(canvas, &p1, &p2, &glow_dsc);
            }

            prev_x = cur_x;
            prev_y = cur_y;
        }

        lv_obj_invalidate(canvas);
        return;
    }

    // ------------------------------------------------------------
    // Mode 2: stock-video-style 32-bar visualizer.
    // This is a PCM-reactive bar display.  It uses the same Bluetooth/SD
    // snapshot, so both sources behave identically except for the source label.
    // ------------------------------------------------------------
    lv_canvas_fill_bg(canvas,
                      flash ? lv_color_hex(0x100018) : lv_color_hex(0x000816),
                      LV_OPA_COVER);

    // Ground glow / baseline.
    int baseline = CANVAS_H - 36;
    draw_hline(canvas, 16, CANVAS_W - 16, baseline + 1,
               flash ? lv_color_hex(0x662266) : lv_color_hex(0x102044), 2,
               LV_OPA_COVER);

    const int band_w = CANVAS_W / BAR_COUNT;
    const int seg_h = 5;
    const int seg_gap = 3;
    const int max_segments = 22;

    for (uint32_t b = 0; b < BAR_COUNT; b++) {
        int start = b * SCOPE_SAMPLES / BAR_COUNT;
        int end   = (b + 1) * SCOPE_SAMPLES / BAR_COUNT;
        if (end <= start) end = start + 1;

        uint32_t acc = 0;
        int32_t local_peak = 0;

        for (int i = start; i < end; i++) {
            int32_t a = abs_i16(snap[i]);
            acc += (uint32_t)a;
            if (a > local_peak) local_peak = a;
        }

        float level = ((float)acc / (float)(end - start)) / INT16_MAX_F;
        float transient = (float)local_peak / INT16_MAX_F;

        // Make quiet sections still show movement but keep loud parts punchy.
        level = sqrtf(level * 0.70f + transient * 0.30f);
        level = clamp01(level * 1.65f);

        if (level > bar_smooth[b]) {
            bar_smooth[b] = bar_smooth[b] * 0.35f + level * 0.65f;
        } else {
            bar_smooth[b] = bar_smooth[b] * 0.82f + level * 0.18f;
        }

        int segments = 1 + (int)(bar_smooth[b] * (float)max_segments);
        if (segments > max_segments) segments = max_segments;

        int x = b * band_w + band_w / 2;
        int width = band_w - 4;
        if (width < 3) width = 3;

        lv_color_t color = band_color(b, bar_smooth[b], flash);

        for (int s = 0; s < segments; s++) {
            int y = baseline - s * (seg_h + seg_gap);
            draw_vline(canvas, x, y, y - seg_h, color, width, LV_OPA_COVER);
        }

        // Reflection below the baseline, dimmer and shorter like the reference.
        int reflection = segments / 3;
        if (reflection > 7) reflection = 7;
        for (int s = 0; s < reflection; s++) {
            int y = baseline + 8 + s * (seg_h + seg_gap);
            lv_opa_t opa = (lv_opa_t)(70 - s * 8);
            draw_vline(canvas, x, y, y + seg_h, color, width, opa);
        }
    }

    // Small beat flash line across the bottom.
    if (flash) {
        draw_hline(canvas, 20, CANVAS_W - 20, CANVAS_H - 5,
                   lv_color_hex(0xFF44FF), 3, LV_OPA_60);
    }

    lv_obj_invalidate(canvas);
}

uint32_t LVGL_Oscilloscope::run()
{
    build_ui();
    return loop();
}

}}}}
