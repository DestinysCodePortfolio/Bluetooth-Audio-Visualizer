#ifndef _LVGL_CLOCK_H_
#define _LVGL_CLOCK_H_
        
#include "demos/lv_demos.h"
#include <cs122_app.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_DemoWidgets : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();                        
    };
}}}}

#endif