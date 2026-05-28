#ifndef _LVGL_CLOCK_H_
#define _LVGL_CLOCK_H_

#include <cs122_app.h>

typedef struct {
    lv_style_t items;
    lv_style_t indicator;
    lv_style_t main;
} section_styles_t;

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_HeartRate : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();

    private:

        void lv_example_scale_10(void);
        void add_section(lv_obj_t * target_scale,
                        int32_t from,
                        int32_t to,
                        const section_styles_t * styles);
        void init_section_styles(section_styles_t * styles, lv_color_t color);
        static void hr_anim_timer_cb(lv_timer_t * timer);
        static lv_color_t get_hr_zone_color(int32_t hr);
                        
    };
}}}}

#endif