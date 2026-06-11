#include "lvgl_oscilloscope.h"
#include "audio_buffer.h"
#include "lv_conf.h"
#include <lvgl.h>
#include <cmath>
#include <cstring>
 

#define SCR_W       480
#define SCR_H       272
 
// The waveform canvas sits in a box inset from the screen edges
#define CANVAS_X    0
#define CANVAS_Y    30          // leave 30px at top for labels
#define CANVAS_W    SCR_W       // full width
#define CANVAS_H    (SCR_H - 30)
 
#define MID_Y       (CANVAS_H / 2)   // vertical centre of waveform area
 
// Grid — 8 columns × 4 rows
#define GRID_COLS   8
#define GRID_ROWS   4
 
// Maximum int16 value, used for normalisation
#define INT16_MAX_F 32767.0f
 
// Refresh rate of the scope redraw (milliseconds)
#define SCOPE_REFRESH_MS  33    // ~30 fps
 
AudioRingBuf g_audio_buf;
 
namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
 

LVGL_Oscilloscope::LVGL_Oscilloscope(SPIDisplay            *spi_disp,
                                     lv_display_flush_cb_t  fcallback,
                                     lv_tick_get_cb_t       tcallback)
    : CS122_App(spi_disp, fcallback, tcallback),
      canvas(nullptr), label_source(nullptr), label_freq(nullptr),
      peak_bar(nullptr), canvas_data(nullptr), scope_timer(nullptr)
{
    audio_buf_init();
}
 
// quiet → cyan, medium → green, loud → yellow → red

lv_color_t LVGL_Oscilloscope::amplitude_color(float norm)
{
    // norm is 0.0 (silence) … 1.0 (full scale)
    if (norm < 0.33f) {
        // cyan → green
        uint8_t g = (uint8_t)(200 + norm / 0.33f * 55);   // 200→255
        return lv_color_make(0, g, (uint8_t)(255 - norm / 0.33f * 255));
    } else if (norm < 0.66f) {
        // green → yellow
        float t = (norm - 0.33f) / 0.33f;
        return lv_color_make((uint8_t)(t * 255), 255, 0);
    } else {
        // yellow → red
        float t = (norm - 0.66f) / 0.34f;
        return lv_color_make(255, (uint8_t)(255 - t * 255), 0);
    }
}
 

static void canvas_draw_line(lv_obj_t * canvas,
                             const lv_point_precise_t * p1,
                             const lv_point_precise_t * p2,
                             const lv_draw_line_dsc_t * dsc)
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
 

void LVGL_Oscilloscope::build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
 
    // ── Background ──────────────────────────────────────────────────────────
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr,  LV_OPA_COVER,     LV_PART_MAIN);
    lv_obj_invalidate(scr);   // <-- tell LVGL the whole screen is dirty
    
    // ── Top bar ─────────────────────────────────────────────────────────────
    lv_obj_t *topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, SCR_W, 28);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_bg_color(topbar,   lv_color_hex(0x001800), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(topbar,     LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(topbar, 0,                    LV_PART_MAIN);
    lv_obj_set_style_radius(topbar,     0,                      LV_PART_MAIN);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
 
    // Title
    lv_obj_t *lbl_title = lv_label_create(topbar);
    lv_label_set_text(lbl_title, "AUDIO SCOPE");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00FF88), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 8, 0);
 
    // Source label (top-right)
    label_source = lv_label_create(topbar);
    lv_label_set_text(label_source, "NO SIGNAL");
    lv_obj_set_style_text_color(label_source, lv_color_hex(0x556655), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_source, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_source, LV_ALIGN_RIGHT_MID, -8, 0);
 
    // Frequency/info label (top-centre)
    label_freq = lv_label_create(topbar);
    lv_label_set_text(label_freq, "44100 Hz  STEREO");
    lv_obj_set_style_text_color(label_freq, lv_color_hex(0x338833), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_freq, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label_freq, LV_ALIGN_CENTER, 0, 0);
 
    // ── Canvas for waveform ─────────────────────────────────────────────────
    // LV_COLOR_FORMAT_RGB565 → 2 bytes per pixel
    uint32_t canvas_buf_size = (uint32_t)CANVAS_W * CANVAS_H * 2;
    canvas_data = new uint8_t[canvas_buf_size];
    memset(canvas_data, 0, canvas_buf_size);
 
    canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_data, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, CANVAS_X, CANVAS_Y);
 
    // Draw the initial black background + grid onto the canvas
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_invalidate(canvas);   // <-- add this
    // ── Kick off the periodic redraw timer ──────────────────────────────────
    scope_timer = lv_timer_create(scope_timer_cb, SCOPE_REFRESH_MS, this);
}
 

// Timer callback — called every SCOPE_REFRESH_MS ms by LVGL

void LVGL_Oscilloscope::scope_timer_cb(lv_timer_t *timer)
{
    LVGL_Oscilloscope *self =
        reinterpret_cast<LVGL_Oscilloscope *>(lv_timer_get_user_data(timer));
    self->redraw_scope();
}
 

void LVGL_Oscilloscope::redraw_scope(void)
{
    // ── 1. Snapshot audio ────────────────────────────────────────────────────
    static int16_t snap[SCOPE_SAMPLES];
    AudioSource src = audio_buf_snapshot(snap);
 
    // ── 2. Update source label ───────────────────────────────────────────────
    /*if (src == AUDIO_SRC_BLUETOOTH) {
        lv_label_set_text(label_source, LV_SYMBOL_BLUETOOTH " BLUETOOTH");
        lv_obj_set_style_text_color(label_source,
                                    lv_color_hex(0x00CCFF), LV_PART_MAIN);
    } else if (src == AUDIO_SRC_SD_CARD) {
        lv_label_set_text(label_source, LV_SYMBOL_SD_CARD " SD CARD");
        lv_obj_set_style_text_color(label_source,
                                    lv_color_hex(0xFFCC00), LV_PART_MAIN);
    } else {
        lv_label_set_text(label_source, "NO SIGNAL");
        lv_obj_set_style_text_color(label_source,
                                    lv_color_hex(0x334433), LV_PART_MAIN);
    }*/
   src = AUDIO_SRC_BLUETOOTH;
static float phase = 0.0f;
for (int i = 0; i < SCOPE_SAMPLES; i++) {
    snap[i] = (int16_t)(sinf(phase) * 28000.0f);
    phase += 2.0f * 3.14159f * 440.0f / 44100.0f;
    if (phase > 2.0f * 3.14159f) phase -= 2.0f * 3.14159f;
}
 
    // ── 3. Clear canvas to black ─────────────────────────────────────────────
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_invalidate(canvas);   // <-- add this one line

    // ── 4. Draw grid ─────────────────────────────────────────────────────────
    lv_draw_line_dsc_t grid_dsc;
    lv_draw_line_dsc_init(&grid_dsc);
    grid_dsc.color = lv_color_hex(0x003300);   // very dim green
    grid_dsc.width = 1;
    grid_dsc.opa   = LV_OPA_COVER;
 
    // Vertical grid lines
    for (int col = 1; col < GRID_COLS; col++) {
        int x = col * CANVAS_W / GRID_COLS;
        lv_point_precise_t p1 = {(lv_value_precise_t)x, 0};
        lv_point_precise_t p2 = {(lv_value_precise_t)x, (lv_value_precise_t)(CANVAS_H - 1)};
        canvas_draw_line(canvas, &p1, &p2, &grid_dsc);
    }
 
    // Horizontal grid lines
    for (int row = 1; row < GRID_ROWS; row++) {
        int y = row * CANVAS_H / GRID_ROWS;
        lv_point_precise_t p1 = {0,                           (lv_value_precise_t)y};
        lv_point_precise_t p2 = {(lv_value_precise_t)(CANVAS_W - 1), (lv_value_precise_t)y};
        canvas_draw_line(canvas, &p1, &p2, &grid_dsc);
    }
 
    // Centre line (slightly brighter)
    grid_dsc.color = lv_color_hex(0x004400);
    {
        lv_point_precise_t p1 = {0,                           (lv_value_precise_t)MID_Y};
        lv_point_precise_t p2 = {(lv_value_precise_t)(CANVAS_W - 1), (lv_value_precise_t)MID_Y};
        canvas_draw_line(canvas, &p1, &p2, &grid_dsc);
    }
 
    // ── 5. Compute peak amplitude for colour mapping ──────────────────────
    int32_t peak = 0;
    for (int i = 0; i < SCOPE_SAMPLES; i++) {
        int32_t a = snap[i] < 0 ? -snap[i] : snap[i];
        if (a > peak) peak = a;
    }
    float peak_norm = (float)peak / INT16_MAX_F;
    if (peak_norm > 1.0f) peak_norm = 1.0f;
 
    // ── 6. If no signal, draw a flat idle line and return ────────────────────
    if (src == AUDIO_SRC_NONE || peak < 64) {
        lv_draw_line_dsc_t idle_dsc;
        lv_draw_line_dsc_init(&idle_dsc);
        idle_dsc.color = lv_color_hex(0x004400);
        idle_dsc.width = 1;
        idle_dsc.opa   = LV_OPA_COVER;
        lv_point_precise_t p1 = {0,                           (lv_value_precise_t)MID_Y};
        lv_point_precise_t p2 = {(lv_value_precise_t)(CANVAS_W - 1), (lv_value_precise_t)MID_Y};
        canvas_draw_line(canvas, &p1, &p2, &idle_dsc);
        lv_obj_invalidate(canvas);
        return;
    }
 
    // ── 7. Draw waveform ─────────────────────────────────────────────────────
    // We draw SCOPE_SAMPLES-1 line segments connecting adjacent sample points.
    // Each segment is coloured by the local amplitude.
 
    lv_draw_line_dsc_t wave_dsc;
    lv_draw_line_dsc_init(&wave_dsc);
    wave_dsc.width    = 2;
    wave_dsc.opa      = LV_OPA_COVER;
    wave_dsc.round_start = 1;
    wave_dsc.round_end  = 1;
 
    // Scale factor: map int16 range to half the canvas height
    float scale = (float)(CANVAS_H / 2 - 4) / INT16_MAX_F;
 
    int prev_x = 0;
    int prev_y = MID_Y - (int)((float)snap[0] * scale);
    // Clamp
    if (prev_y < 0)           prev_y = 0;
    if (prev_y >= CANVAS_H)   prev_y = CANVAS_H - 1;
 
    for (int i = 1; i < SCOPE_SAMPLES; i++) {
        int cur_x = i * CANVAS_W / SCOPE_SAMPLES;
        int cur_y = MID_Y - (int)((float)snap[i] * scale);
        if (cur_y < 0)          cur_y = 0;
        if (cur_y >= CANVAS_H)  cur_y = CANVAS_H - 1;
 
        // Local amplitude for colour
        float local_amp = (float)(snap[i] < 0 ? -snap[i] : snap[i])
                          / INT16_MAX_F;
        wave_dsc.color = amplitude_color(local_amp);
 
        lv_point_precise_t p1 = {(lv_value_precise_t)prev_x,
                                  (lv_value_precise_t)prev_y};
        lv_point_precise_t p2 = {(lv_value_precise_t)cur_x,
                                  (lv_value_precise_t)cur_y};
        canvas_draw_line(canvas, &p1, &p2, &wave_dsc);
 
        // Optional glow — draw a dimmer, wider line underneath for a "bloom" look
        if (local_amp > 0.4f) {
            lv_draw_line_dsc_t glow_dsc = wave_dsc;
            glow_dsc.width = 4;
            glow_dsc.opa   = LV_OPA_20;
            canvas_draw_line(canvas, &p1, &p2, &glow_dsc);
        }
 
        prev_x = cur_x;
        prev_y = cur_y;
    }
 
    // ── 8. Mark canvas dirty so LVGL flushes it ──────────────────────────────
    lv_obj_invalidate(canvas);
}
 

uint32_t LVGL_Oscilloscope::run()
{
    build_ui();
    return loop();     // inherited from CS122_App — runs lv_timer_handler() loop
}
 
}}}}
 