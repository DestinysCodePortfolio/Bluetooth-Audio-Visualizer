#ifndef _LVGL_PIE_CHART_H_
#define _LVGL_PIE_CHART_H_

#include <cs122_app.h>

#define CHART_SIZE 200
#define SLICE_OFFSET 20


namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_PieChart;

    typedef struct {
        int start_angle;
        int end_angle;
        int mid_angle;
        lv_point_t home;
        bool out;
        LVGL_PieChart *parent;
    } slice_info_t;

    typedef struct {
        lv_obj_t * obj;
        int start_x;
        int start_y;
        int end_x;
        int end_y;
    } slice_anim_data_t;

    class LVGL_PieChart : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();

    private:
        float angle_accum = 0.0f;
        slice_info_t * active_info = NULL;
        lv_obj_t * active_arc = NULL;

        void lv_example_arc_3(void);
        void create_slice(lv_obj_t * parent, int percentage, lv_color_t color);

        friend void anim_move_cb(void * var, int32_t v);
        friend void anim_cleanup_cb(lv_anim_t * a);
        friend void arc_click_cb(lv_event_t * e);
    };
}}}}

#endif