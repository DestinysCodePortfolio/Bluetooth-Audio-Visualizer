#include <lvgl_demo_widgets.h>
#include "lv_conf.h"
#include <lvgl.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {

#if LV_USE_FLOAT
    #define my_PRIprecise "f"
#else
    #define my_PRIprecise LV_PRId32
#endif

uint32_t LVGL_DemoWidgets::run() {
    lv_demo_widgets();
    return loop();
}

}}}}