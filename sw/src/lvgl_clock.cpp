#include <lvgl_clock.h>
#include "lv_conf.h"
#include <lvgl.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {

#if LV_USE_FLOAT
    #define my_PRIprecise "f"
#else
    #define my_PRIprecise LV_PRId32
#endif

    void timer_cb(lv_timer_t * timer) {
        LVGL_Clock *clock = reinterpret_cast<LVGL_Clock*>(lv_timer_get_user_data(timer));

        int32_t &minute = clock->minute;
        int32_t &hour  = clock->hour;
        auto minute_hand = clock->minute_hand;
        auto hour_hand = clock->hour_hand;
        auto minute_hand_points = clock->minute_hand_points;
        auto scale = clock->scale;

        minute++;
        if(minute > 59) {
            minute = 0;
            hour++;
            if(hour > 11) {
                hour = 0;
            }
        }

        /**
         * the scale will store the needle line points in the existing
         * point array if one was set with `lv_line_set_points_mutable`.
         * Otherwise, it will allocate the needle line points.
         */

        /* the scale will store the minute hand line points in `minute_hand_points` */
        lv_scale_set_line_needle_value(scale, minute_hand, 80, minute);
        /* log the points that were stored in the array */
        LV_LOG_USER(
            "minute hand points - "
            "0: (%" my_PRIprecise ", %" my_PRIprecise "), "
            "1: (%" my_PRIprecise ", %" my_PRIprecise ")",
            minute_hand_points[0].x, minute_hand_points[0].y,
            minute_hand_points[1].x, minute_hand_points[1].y
        );

        /* the scale will allocate the hour hand line points */
        lv_scale_set_line_needle_value(scale, hour_hand, 60, hour * 5 + (minute / 12));
    }

    /**
     * A round scale with multiple needles, resembling a clock
     */
    void LVGL_Clock::lv_example_scale_6(void) {
        scale = lv_scale_create(lv_screen_active());

        lv_obj_set_size(scale, 200, 200);
        lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
        lv_obj_set_style_bg_opa(scale, LV_OPA_60, 0);
        lv_obj_set_style_bg_color(scale, lv_color_black(), 0);
        lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(scale, true, 0);
        lv_obj_center(scale);

        lv_scale_set_label_show(scale, true);

        lv_scale_set_total_tick_count(scale, 61);
        lv_scale_set_major_tick_every(scale, 5);

        static const char * hour_ticks[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL};
        lv_scale_set_text_src(scale, hour_ticks);

        static lv_style_t indicator_style;
        lv_style_init(&indicator_style);

        /* Label style properties */
        lv_style_set_text_font(&indicator_style, LV_FONT_DEFAULT);
        lv_style_set_text_color(&indicator_style, lv_palette_main(LV_PALETTE_YELLOW));

        /* Major tick properties */
        lv_style_set_line_color(&indicator_style, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_length(&indicator_style, 8); /* tick length */
        lv_style_set_line_width(&indicator_style, 2); /* tick width */
        lv_obj_add_style(scale, &indicator_style, LV_PART_INDICATOR);

        /* Minor tick properties */
        static lv_style_t minor_ticks_style;
        lv_style_init(&minor_ticks_style);
        lv_style_set_line_color(&minor_ticks_style, lv_palette_main(LV_PALETTE_YELLOW));
        lv_style_set_length(&minor_ticks_style, 6); /* tick length */
        lv_style_set_line_width(&minor_ticks_style, 2); /* tick width */
        lv_obj_add_style(scale, &minor_ticks_style, LV_PART_ITEMS);

        /* Main line properties */
        static lv_style_t main_line_style;
        lv_style_init(&main_line_style);
        lv_style_set_arc_color(&main_line_style, lv_color_black());
        lv_style_set_arc_width(&main_line_style, 5);
        lv_obj_add_style(scale, &main_line_style, LV_PART_MAIN);

        lv_scale_set_range(scale, 0, 60);

        lv_scale_set_angle_range(scale, 360);
        lv_scale_set_rotation(scale, 270);

        minute_hand = lv_line_create(scale);
        lv_line_set_points_mutable(minute_hand, minute_hand_points, 2);

        lv_obj_set_style_line_width(minute_hand, 3, 0);
        lv_obj_set_style_line_rounded(minute_hand, true, 0);
        lv_obj_set_style_line_color(minute_hand, lv_color_white(), 0);

        hour_hand = lv_line_create(scale);

        lv_obj_set_style_line_width(hour_hand, 5, 0);
        lv_obj_set_style_line_rounded(hour_hand, true, 0);
        lv_obj_set_style_line_color(hour_hand, lv_palette_main(LV_PALETTE_RED), 0);

        hour = 11;
        minute = 5;
        lv_timer_t * timer = lv_timer_create(timer_cb, 1000, this);
        lv_timer_ready(timer);
    }

    uint32_t LVGL_Clock::run() {
        lv_example_scale_6();
        return loop();
    }
}}}}