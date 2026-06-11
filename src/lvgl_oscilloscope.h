#pragma once

#include "cs122_app.h"
#include "audio_buffer.h"
#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Call this from a GPIO button handler to switch display modes.
void lvgl_visualizer_next_mode(void);
uint8_t lvgl_visualizer_get_mode(void);
const char *lvgl_visualizer_mode_name(void);

#ifdef __cplusplus
}
#endif

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {

class LVGL_Oscilloscope : public CS122_App {
public:
    LVGL_Oscilloscope(SPIDisplay            *spi_disp,
                      lv_display_flush_cb_t  fcallback,
                      lv_tick_get_cb_t       tcallback);

    uint32_t run() override;

private:
    void       build_ui();
    void       redraw_scope();

    static lv_color_t amplitude_color(float norm);
    static lv_color_t band_color(uint32_t band, float norm, bool beat_flash);
    static void       scope_timer_cb(lv_timer_t *timer);

    lv_obj_t   *canvas       = nullptr;
    lv_obj_t   *label_source = nullptr;
    lv_obj_t   *label_mode   = nullptr;
    lv_obj_t   *label_info   = nullptr;
    uint8_t    *canvas_data  = nullptr;
    lv_timer_t *scope_timer  = nullptr;
};

}}}}
