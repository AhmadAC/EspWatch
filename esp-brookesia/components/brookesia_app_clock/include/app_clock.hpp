#pragma once

#include "esp_brookesia.hpp"
#include "lvgl.h"

extern const lv_image_dsc_t ui_clock_icon;

class AppClock : public esp_brookesia::systems::phone::App {
public:
    AppClock();
    ~AppClock();

    bool init(void) override;
    
    // Override pure virtual functions
    bool run(void) override;
    bool back(void) override;

private:
    static void update_time_cb(lv_timer_t *timer);

    lv_obj_t *main_screen;
    lv_obj_t *time_container;
    lv_obj_t *time_label;
    lv_timer_t *update_timer;
};
