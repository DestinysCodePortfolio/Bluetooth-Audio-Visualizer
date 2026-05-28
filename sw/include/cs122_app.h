#ifndef _CS122_APP_H_
#define _CS122_APP_H_

#include "lv_conf.h"
#include <lvgl.h>

namespace ucr { namespace bcoe { 
    class SPIDisplay;
    namespace cs { namespace cs122{
    class CS122_App {
    public:
        CS122_App(SPIDisplay *spi_disp, lv_display_flush_cb_t fcallback, lv_tick_get_cb_t tcallback);
        virtual uint32_t run() = 0;

    private:
        SPIDisplay *spi_display;
        uint8_t *framebuffer;
        lv_display_t *display;
        lv_display_flush_cb_t flush_callback;
        lv_tick_get_cb_t tick_callback;
        bool running;

    protected:
        uint32_t loop();
    };
}}}}

#endif