#include "esp_brookesia.hpp"
#include "lvgl.h"
#include "ui.h" // Include your EEZ Studio exported header

using namespace esp_brookesia;

extern const lv_image_dsc_t ui_settings_icon; // Reuses our gear icon

class AppEEZ : public core::App {
public:
    AppEEZ() : App() {
        _name = "My EEZ App";
        _icon = &ui_settings_icon; 
        main_screen = nullptr;
    }

    bool init(void) override {
        // 1. Initialize your EEZ Studio widgets
        ui_init(); 
        
        return true;
    }

    bool resume(void) override {
        // 2. Load your main EEZ screen. 
        // Note: Open your exported "ui.h" file and check what EEZ named your main screen variable.
        // It is usually 'objects.main', 'ui_Main', or 'ui_Screen1'. Replace below as needed:
        #if LVGL_VERSION_MAJOR >= 9
            lv_screen_load(objects.main); // Standard EEZ Studio LVGL 9 structure
        #else
            lv_scr_load(ui_Screen1);
        #endif
        
        return true;
    }

    bool pause(void) override {
        return true;
    }

private:
    lv_obj_t *main_screen;
};

// Auto-register it into the OS grid
ESP_BROOKESIA_REGISTER_APP(AppEEZ)