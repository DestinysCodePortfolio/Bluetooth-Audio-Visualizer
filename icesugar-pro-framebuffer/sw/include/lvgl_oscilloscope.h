#pragma once
 
#include "cs122_app.h"
#include "audio_buffer.h"
#include <lvgl.h>
 
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
    static void       scope_timer_cb(lv_timer_t *timer);
 
    lv_obj_t   *canvas       = nullptr;
    lv_obj_t   *label_source = nullptr;
    lv_obj_t   *label_freq   = nullptr;
    lv_obj_t   *peak_bar     = nullptr;
    uint8_t    *canvas_data  = nullptr;
    lv_timer_t *scope_timer  = nullptr;
};
 
}}}}