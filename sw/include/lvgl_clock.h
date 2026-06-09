#ifndef _LVGL_CLOCK_H_
#define _LVGL_CLOCK_H_

#include <cs122_app.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_Clock : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();

    private:
        lv_obj_t *scale;
        lv_obj_t *minute_hand;
        lv_obj_t *hour_hand;
        lv_point_precise_t minute_hand_points[2];
        int32_t hour;
        int32_t minute;

        void lv_example_scale_6(void);

        friend void timer_cb(lv_timer_t * timer);
    };
}}}}

#endif