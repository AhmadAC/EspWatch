#include "app_clock.hpp"
#include "esp_log.h"
#include <time.h>

using namespace esp_brookesia;

static const char *TAG = "AppClock";

// Declare the Montserrat font
LV_FONT_DECLARE(lv_font_montserrat_48);

// Use the new API constructor. 
// false = Do not use the default screen since we create our own main_screen below
AppClock::AppClock() : esp_brookesia::systems::phone::App("Clock", &ui_clock_icon, false) {
    main_screen = nullptr;
    time_container = nullptr;
    time_label = nullptr;
    update_timer = nullptr;
}

AppClock::~AppClock() {
    if (update_timer) {
        lv_timer_delete(update_timer);
    }
    if (main_screen) {
        lv_obj_delete(main_screen);
    }
}

bool AppClock::init(void) {
    ESP_LOGI(TAG, "Initializing Fullscreen AMOLED Clock");

    // 1. Create main screen and set to PURE BLACK (turning off AMOLED pixels)
    main_screen = lv_obj_create(nullptr);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, 0);

    // 2. Create the 80% center space container (Pure Black & Borderless)
    time_container = lv_obj_create(main_screen);
    lv_obj_set_size(time_container, LV_PCT(80), LV_PCT(80));
    lv_obj_center(time_container);
    
    lv_obj_set_style_bg_color(time_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(time_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(time_container, 0, 0);
    lv_obj_set_style_pad_all(time_container, 0, 0);

    // 3. Create the Digital Time Label (White, brightly lit text)
    time_label = lv_label_create(time_container);
    lv_obj_center(time_label);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);

    // 4. Set up the dynamic 1-second interval update timer
    update_timer = lv_timer_create(update_time_cb, 1000, this);
    lv_timer_pause(update_timer);

    // Run first instant update
    update_time_cb(update_timer);

    return true;
}

void AppClock::update_time_cb(lv_timer_t *timer) {
    AppClock *app = (AppClock *)timer->user_data;
    if (!app || !app->time_label) {
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    lv_label_set_text(app->time_label, time_str);
}

// ---------------- NEW: Run and Back Implementations ----------------
bool AppClock::run(void) {
    // Show the clock screen
    if (main_screen) {
        lv_scr_load(main_screen);
    }
    // Resume the timer so it updates while active
    if (update_timer) {
        lv_timer_resume(update_timer);
    }
    return true;
}

bool AppClock::back(void) {
    // Pause the timer to save CPU when running in the background
    if (update_timer) {
        lv_timer_pause(update_timer);
    }
    // Return false to tell the OS to close us and go back to launcher
    return false;
}
