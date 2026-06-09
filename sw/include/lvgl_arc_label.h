#ifndef _LVGL_CLOCK_H_
#define _LVGL_CLOCK_H_

#include <cs122_app.h>

namespace ucr { namespace bcoe { namespace cs { namespace cs122 {
    class LVGL_ArcLabel : CS122_App {
    public:
        using CS122_App::CS122_App;
        virtual uint32_t run();

    private:

        void lv_example_arclabel_1(void);
    };
}}}}

#endif