#pragma once

#include "esp_brookesia.hpp"
#include "lvgl.h"

extern const lv_image_dsc_t ui_settings_icon;

class AppSettings : public esp_brookesia::systems::phone::App {
public:
    AppSettings();
    ~AppSettings();

    bool init(void) override;

    // Override pure virtual functions
    bool run(void) override;
    bool back(void) override;

private:
    void buildMainMenu();
    void buildInfoMenu();
    void buildWifiMenu();

    // LVGL Screen & Container Objects
    lv_obj_t *main_screen;
    lv_obj_t *menu_list;

    // Callbacks
    static void on_menu_info_clicked(lv_event_t *e);
    static void on_menu_wifi_clicked(lv_event_t *e);
    static void on_back_clicked(lv_event_t *e);
    static void on_start_ap_clicked(lv_event_t *e);

    // Hardware functions
    void updateHardwareInfo(lv_obj_t* label);
    void startWifiAPProvisioning();
};
