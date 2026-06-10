// File: Smartwatch_OS/main/ui_app.c
#include "ui_app.h"
#include "wifi_app.h"
#include "battery.h"
#include "rtc_clock.h"
#include "hardware_button.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

// Include the EEZ UI header from the my_eez_ui component
#include "ui.h"

#define LCD_H_RES 410
#define LCD_V_RES 502

extern const lv_font_t arial_160;

#ifdef __cplusplus
extern "C" {
#endif
void esp_restart(void); 
lv_obj_t * lv_indev_get_active_obj(void); // Guarantee availability for LVGL 9
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_err_t bsp_i2c_init(void);
#ifdef __cplusplus
}
#endif

static lv_obj_t * tv;
static lv_obj_t * tile_launcher;
static lv_obj_t * tile_clock;
static lv_obj_t * tile_settings;
static lv_obj_t * label_clock_hours;
static lv_obj_t * label_clock_minutes;
static lv_obj_t * lbl_battery;
static lv_obj_t * lbl_wifi;
static lv_obj_t * btn_wifi_toggle;
static lv_obj_t * lbl_wifi_toggle;
static lv_obj_t * reboot_overlay = NULL;

// EEZ Studio Click Bindings (Automatically triggered by EEZ UI actions)
void action_open_clock(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_clock, LV_ANIM_ON);
}

void action_open_settings(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_settings, LV_ANIM_ON);
}

// Fallback empty action placeholder to prevent compile failures if registered
void action_custom_action(lv_event_t * e) {
    // Unused but declared by EEZ
}

static void btn_back_settings_cb(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

// Global Single Click event to return to launcher from tiles
static void tile_click_cb(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

void trigger_return_home(void) {
    if (bsp_display_lock(1000)) {
        lv_obj_t * launcher_scr = lv_obj_get_screen(tv);
        if (lv_screen_active() != launcher_scr) {
            lv_scr_load_anim(launcher_scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
        }
        lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
        bsp_display_unlock();
    }
}

// Global Touchpad Event Callback (Catches single-taps anywhere on the screen)
static void indev_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t * launcher_scr = lv_obj_get_screen(tv);
        
        // If we are currently in an app (not the core launcher screen)
        if (lv_screen_active() != launcher_scr) {
            lv_obj_t * act_obj = lv_indev_get_active_obj();
            
            // If the active object clicked is the screen itself, or null (empty space).
            // (If the user taps a button, act_obj will be that button, bypassing this logic)
            if (act_obj == lv_screen_active() || act_obj == NULL) {
                lv_scr_load_anim(launcher_scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
                lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
            }
        }
    }
}

static void update_wifi_toggle_button_ui(void) {
    if (is_wifi_enabled()) {
        lv_obj_set_style_bg_color(btn_wifi_toggle, lv_color_make(0, 150, 255), 0); // Vibrant Blue (ON)
        lv_label_set_text(lbl_wifi_toggle, LV_SYMBOL_WIFI " Wi-Fi: ON");
    } else {
        lv_obj_set_style_bg_color(btn_wifi_toggle, lv_color_make(80, 80, 80), 0); // Muted Dark Gray (OFF)
        lv_label_set_text(lbl_wifi_toggle, LV_SYMBOL_WIFI " Wi-Fi: OFF");
    }
}

static void btn_wifi_toggle_cb(lv_event_t * e) {
    toggle_wifi();
    update_wifi_toggle_button_ui();
}

static void btn_ap_mode_cb(lv_event_t * e) {
    start_ap_mode_task();
    lv_obj_t * ap_overlay = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(ap_overlay);
    lv_obj_set_size(ap_overlay, LCD_H_RES, LCD_V_RES);
    lv_obj_set_style_bg_color(ap_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ap_overlay, LV_OPA_COVER, 0);
    
    lv_obj_t * lbl = lv_label_create(ap_overlay);
    lv_label_set_text(lbl, "Network Setup\n\nSSID: Smartwatch_AP\nPass: 12345678\n\nConnect on your phone\nto configure Wi-Fi.");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 15);
}

// ----------------------------------------------------
// Core GUI & Logic
// ----------------------------------------------------
static void hardware_poll_timer_cb(lv_timer_t * timer) {
    // 1. Listen for side Power (PWR) Button short-clicks directly from AXP2101 interrupts over I2C
    if (axp2101_check_short_press()) {
        is_screen_on = !is_screen_on;
        if (is_screen_on) {
            bsp_display_backlight_on();
        } else {
            bsp_display_backlight_off();
        }
    }

    if (!is_screen_on) return;

    // 2. Refresh Time
    time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
    char h[8], m[8]; snprintf(h, sizeof(h), "%d", timeinfo.tm_hour); snprintf(m, sizeof(m), "%02d", timeinfo.tm_min);
    if (label_clock_hours) lv_label_set_text(label_clock_hours, h);
    if (label_clock_minutes) lv_label_set_text(label_clock_minutes, m);

    // 3. Reboot Timer
    int r_timer = get_reboot_timer();
    if (r_timer >= 0) {
        if (!reboot_overlay) {
            reboot_overlay = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(reboot_overlay);
            lv_obj_set_size(reboot_overlay, LCD_H_RES, LCD_V_RES);
            lv_obj_set_style_bg_color(reboot_overlay, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(reboot_overlay, LV_OPA_COVER, 0);
            lv_obj_t * l = lv_label_create(reboot_overlay);
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_32, 0);
            lv_obj_center(l);
        }
        if (r_timer == 0) esp_restart();
        char buf[64]; snprintf(buf, sizeof(buf), "Connecting...\nRebooting in %ds", r_timer);
        lv_label_set_text(lv_obj_get_child(reboot_overlay, 0), buf);
        lv_obj_set_style_text_align(lv_obj_get_child(reboot_overlay, 0), LV_TEXT_ALIGN_CENTER, 0);
        decrement_reboot_timer();
    }

    // 4. Update Battery via the new LUT & EMA Tracker
    static battery_tracker_t my_battery = {
        .alpha = 0.05f,
        .filtered_voltage = 0.0f,
        .is_initialized = false
    };
    static uint32_t battery_poll_ticks = 0;
    static float battery_voltage = 0.0f;
    static float battery_percentage = 0.0f;

    // LVGL Timers fire every 50ms. 50ms * 100 = 5 seconds
    if (battery_poll_ticks % 100 == 0) { 
        battery_update(&my_battery, &battery_voltage, &battery_percentage);
    }
    battery_poll_ticks++;

    if (lbl_battery) {
        bool present = axp2101_is_battery_present();
        bool charging = axp2101_is_charging();
        
        if (present) { 
            char b[32]; 
            // Format: "Bat: 85%"  OR  "(Lightning Icon) 85%"
            snprintf(b, sizeof(b), "%s %.0f%%", charging ? LV_SYMBOL_CHARGE : "Bat:", battery_percentage); 
            lv_label_set_text(lbl_battery, b); 
        } else { 
            lv_label_set_text(lbl_battery, charging ? LV_SYMBOL_CHARGE " USB" : "Bat: --"); 
        }
    }
    
    // 5. Update Wi-Fi Indicator
    if (lbl_wifi) lv_label_set_text(lbl_wifi, get_wifi_connected_status() ? LV_SYMBOL_WIFI : "");

    // 6. Tick the EEZ UI screen logic periodically (handles background screen ticks)
    ui_tick();
}

void build_ui(void) {
    // 1. Establish the common I2C bus handle
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        bsp_i2c_init();
        bus = bsp_i2c_get_handle();
    }

    // 2. Initialize PMU & RTC Clock using the common bus
    if (bus != NULL) {
        axp2101_init_pmu(bus);
        init_pcf85063_rtc(bus);
    }

    // 3. Initialize physical button task
    init_hardware_button();

    // ==========================================
    // FORCE DARK MODE ACROSS ENTIRE UI
    // ==========================================
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    
    tv = lv_tileview_create(scr);
    
    // ==========================================
    // DISABLE USER SWIPE GESTURES
    // ==========================================
    lv_obj_remove_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    
    tile_clock = lv_tileview_add_tile(tv, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_clock, lv_color_black(), 0);
    
    tile_launcher = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_launcher, lv_color_black(), 0);
    
    tile_settings = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_settings, lv_color_black(), 0);

    // --- CLOCK TILE ---
    label_clock_hours = lv_label_create(tile_clock);
    lv_obj_set_style_text_color(label_clock_hours, lv_color_make(0, 150, 255), 0);
    lv_obj_set_style_text_font(label_clock_hours, &arial_160, 0);
    lv_obj_align(label_clock_hours, LV_ALIGN_CENTER, 0, -110); 

    label_clock_minutes = lv_label_create(tile_clock);
    lv_obj_set_style_text_color(label_clock_minutes, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_clock_minutes, &arial_160, 0);
    lv_obj_align(label_clock_minutes, LV_ALIGN_CENTER, 0, 110); 

    // Make clock tile single-clickable to return to launcher (since swiping is disabled)
    lv_obj_add_flag(tile_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile_clock, tile_click_cb, LV_EVENT_CLICKED, NULL);

    // ==========================================
    // INITIALIZE EEZ UI AND EMBED IT AS LAUNCHER
    // ==========================================
    create_screens(); // Initialize objects.main widgets but DO NOT set objects.main as the active display root
    if (objects.main != NULL) {
        
        // 1. Force the background of the EEZ Main screen container to be transparent
        lv_obj_set_style_bg_opa(objects.main, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(objects.main, 0, LV_PART_MAIN);

        // 2. Make all sub-containers transparent to keep AMOLED black (turns off pixels completely)
        if (objects.app_settings_2 != NULL) {
            lv_obj_set_style_bg_opa(objects.app_settings_2, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(objects.app_settings_2, 0, LV_PART_MAIN);
        }
        if (objects.obj0 != NULL) {
            lv_obj_set_style_bg_opa(objects.obj0, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(objects.obj0, 0, LV_PART_MAIN);
        }
        if (objects.obj0__app_settings_1 != NULL) {
            lv_obj_set_style_bg_opa(objects.obj0__app_settings_1, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(objects.obj0__app_settings_1, 0, LV_PART_MAIN);
        }

        // 3. Move the child widgets of the EEZ Main screen directly to our home tile launcher
        // This bypasses screen-level reparenting bugs and lets the standard hit-test pipeline handle clicks flawlessly.
        uint32_t child_cnt = lv_obj_get_child_count(objects.main);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(objects.main, 0); // Always index 0 as they migrate to the new parent
            if (child != NULL) {
                lv_obj_set_parent(child, tile_launcher);
            }
        }
        
        // 4. Directly bind the click events to the actual interactive buttons generated by EEZ
        if (objects.obj0__app_settings_icon_1 != NULL) {
            lv_obj_add_event_cb(objects.obj0__app_settings_icon_1, action_open_settings, LV_EVENT_CLICKED, NULL);
        }
        if (objects.app_settings_icon_2 != NULL) {
            lv_obj_add_event_cb(objects.app_settings_icon_2, action_open_clock, LV_EVENT_CLICKED, NULL);
        }

        // 5. Dynamically inject Battery and Wi-Fi status on top of your imported layout!
        lbl_battery = lv_label_create(tile_launcher);
        lv_obj_set_style_text_color(lbl_battery, lv_color_white(), 0);
        lv_obj_align(lbl_battery, LV_ALIGN_TOP_MID, 40, 20);
        
        lbl_wifi = lv_label_create(tile_launcher);
        lv_obj_set_style_text_color(lbl_wifi, lv_color_white(), 0);
        lv_obj_align(lbl_wifi, LV_ALIGN_TOP_MID, -40, 20);
    }

    // --- SETTINGS TILE ---
    // Make settings tile single-clickable to return to launcher (since swiping is disabled)
    lv_obj_add_flag(tile_settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile_settings, tile_click_cb, LV_EVENT_CLICKED, NULL);

    // Wi-Fi Toggle Button (Positioned at Y = -70 for top-alignment)
    btn_wifi_toggle = lv_button_create(tile_settings);
    lv_obj_set_size(btn_wifi_toggle, 240, 60);
    lv_obj_align(btn_wifi_toggle, LV_ALIGN_CENTER, 0, -70);
    
    lbl_wifi_toggle = lv_label_create(btn_wifi_toggle);
    lv_obj_set_style_text_color(lbl_wifi_toggle, lv_color_white(), 0);
    lv_obj_center(lbl_wifi_toggle);
    
    update_wifi_toggle_button_ui(); // Sync initial UI color/text
    lv_obj_add_event_cb(btn_wifi_toggle, btn_wifi_toggle_cb, LV_EVENT_CLICKED, NULL);

    // AP Mode Setup Button (Positioned at Y = 10 for middle-alignment)
    lv_obj_t * btn_ap = lv_button_create(tile_settings);
    lv_obj_set_size(btn_ap, 240, 60);
    lv_obj_set_style_bg_color(btn_ap, lv_color_make(50, 50, 50), 0); // Muted Dark Gray button
    lv_obj_align(btn_ap, LV_ALIGN_CENTER, 0, 10);
    
    lv_obj_t * lbl_ap = lv_label_create(btn_ap);
    lv_label_set_text(lbl_ap, LV_SYMBOL_SETTINGS " Setup AP Mode");
    lv_obj_set_style_text_color(lbl_ap, lv_color_white(), 0);
    lv_obj_center(lbl_ap);
    lv_obj_add_event_cb(btn_ap, btn_ap_mode_cb, LV_EVENT_CLICKED, NULL);

    // Back Button on Settings (Positioned at Y = 90 for bottom-alignment to safely return to launcher)
    lv_obj_t * btn_back_settings = lv_button_create(tile_settings);
    lv_obj_set_size(btn_back_settings, 240, 60);
    lv_obj_set_style_bg_color(btn_back_settings, lv_color_make(100, 100, 100), 0);
    lv_obj_align(btn_back_settings, LV_ALIGN_CENTER, 0, 90);
    
    lv_obj_t * lbl_back_settings = lv_label_create(btn_back_settings);
    lv_label_set_text(lbl_back_settings, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(lbl_back_settings, lv_color_white(), 0);
    lv_obj_center(lbl_back_settings);
    lv_obj_add_event_cb(btn_back_settings, btn_back_settings_cb, LV_EVENT_CLICKED, NULL);

    // Register global touch Input Device event callback
    lv_indev_t * indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, indev_event_cb, LV_EVENT_CLICKED, NULL);
    }

    // Default to the Launcher showing
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
    lv_timer_create(hardware_poll_timer_cb, 50, NULL);
}