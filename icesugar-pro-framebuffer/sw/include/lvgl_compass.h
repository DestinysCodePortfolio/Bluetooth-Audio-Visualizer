#ifndef _LVGL_COMPASS_H_
#define _LVGL_COMPASS_H_

#include <cs122_app.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_Compass : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();

    private:
        lv_obj_t * scale;
        lv_obj_t * label;

        void lv_example_scale_12(void);
        const char * heading_to_cardinal(int32_t heading);

        friend void draw_event_cb(lv_event_t * e);
        friend void set_heading_value(void * obj, int32_t v);
    };
}}}}

#endif