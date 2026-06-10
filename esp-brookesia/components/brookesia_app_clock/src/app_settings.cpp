#include "app_settings.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"

using namespace esp_brookesia;

static const char *TAG = "AppSettings";

// Pass the app parameters straight into the inherited App constructor
AppSettings::AppSettings() : esp_brookesia::systems::phone::App("Settings", &ui_settings_icon, false) {
    main_screen = nullptr;
    menu_list = nullptr;
}

AppSettings::~AppSettings() {
    if (main_screen) {
        lv_obj_delete(main_screen);
    }
}

bool AppSettings::init(void) {
    ESP_LOGI(TAG, "Initializing Settings App");

    // Create the main screen object for this app
    main_screen = lv_obj_create(nullptr);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x111111), 0);

    buildMainMenu();
    
    return true;
}

void AppSettings::buildMainMenu() {
    lv_obj_clean(main_screen);

    lv_obj_t * title = lv_label_create(main_screen);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    menu_list = lv_list_create(main_screen);
    lv_obj_set_size(menu_list, LV_PCT(90), LV_PCT(75));
    lv_obj_align(menu_list, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t * btn_wifi = lv_list_add_btn(menu_list, LV_SYMBOL_WIFI, " WiFi");
    lv_obj_add_event_cb(btn_wifi, on_menu_wifi_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t * btn_time = lv_list_add_btn(menu_list, LV_SYMBOL_SETTINGS, " Time & Date");
    (void)btn_time; // Fixes the unused variable warning
    
    lv_obj_t * btn_info = lv_list_add_btn(menu_list, LV_SYMBOL_FILE, " Device Info");
    lv_obj_add_event_cb(btn_info, on_menu_info_clicked, LV_EVENT_CLICKED, this);
}

void AppSettings::buildInfoMenu() {
    lv_obj_clean(main_screen);

    lv_obj_t * btn_back = lv_button_create(main_screen);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t * info_label = lv_label_create(main_screen);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_label, LV_PCT(90));
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xFFFFFF), 0);

    updateHardwareInfo(info_label);
}

void AppSettings::updateHardwareInfo(lv_obj_t* label) {
    // 1. Get RAM
    size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    size_t total_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;

    // 2. Get CPU Temperature
    float temp_c = 0.0;
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&temp_sensor_config, &temp_sensor) == ESP_OK) {
        temperature_sensor_enable(temp_sensor);
        temperature_sensor_get_celsius(temp_sensor, &temp_c);
        temperature_sensor_disable(temp_sensor);
        temperature_sensor_uninstall(temp_sensor);
    }

    char info_buf[256];
    snprintf(info_buf, sizeof(info_buf),
             "System Info:\n\n"
             "RAM: %d KB / %d KB\n"
             "CPU Temp: %.1f C\n"
             "Battery: 100%% (Mock)\n"
             "Storage: OK",
             (int)free_ram, (int)total_ram, temp_c);

    lv_label_set_text(label, info_buf);
}

void AppSettings::buildWifiMenu() {
    lv_obj_clean(main_screen);

    lv_obj_t * btn_back = lv_button_create(main_screen);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT " Back");
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(btn_back, on_back_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t * title = lv_label_create(main_screen);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t * btn_ap = lv_button_create(main_screen);
    lv_obj_t * lbl_ap = lv_label_create(btn_ap);
    lv_label_set_text(lbl_ap, "Set Up via Phone (SoftAP)");
    lv_obj_align(btn_ap, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_event_cb(btn_ap, on_start_ap_clicked, LV_EVENT_CLICKED, this);
}

void AppSettings::startWifiAPProvisioning() {
    ESP_LOGI(TAG, "Starting SoftAP for WiFi Provisioning...");
    
    lv_obj_t * msgbox = lv_msgbox_create(main_screen);
    lv_msgbox_add_title(msgbox, "AP Mode Started");
    lv_msgbox_add_text(msgbox, "Connect phone to 'EspWatch_Setup'\nGo to 192.168.4.1");
    lv_msgbox_add_close_button(msgbox);
    lv_obj_center(msgbox);
}

/* ---------------- Callbacks ---------------- */
void AppSettings::on_menu_info_clicked(lv_event_t *e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    app->buildInfoMenu();
}

void AppSettings::on_menu_wifi_clicked(lv_event_t *e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    app->buildWifiMenu();
}

void AppSettings::on_back_clicked(lv_event_t *e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    app->buildMainMenu();
}

void AppSettings::on_start_ap_clicked(lv_event_t *e) {
    AppSettings *app = (AppSettings *)lv_event_get_user_data(e);
    app->startWifiAPProvisioning();
}

// ---------------- NEW: Run and Back Implementations ----------------
bool AppSettings::run(void) {
    // Show the settings screen
    if (main_screen) {
        lv_scr_load(main_screen);
    }
    return true;
}

bool AppSettings::back(void) {
    // Return false to tell the OS to close us and go back to launcher
    return false;
}
