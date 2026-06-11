// File: Smartwatch_OS/main/ui_app.c
#include "ui_app.h"
#include "wifi_app.h"
#include "battery.h"
#include "rtc_clock.h"
#include "hardware_button.h"
#include "camera_recv.h"
#include "file_explorer.h"
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
#include "../apps/my_eez_ui/ui.h"

#define LCD_H_RES 410
#define LCD_V_RES 502

extern const lv_font_t arial_160;

#ifdef __cplusplus
extern "C" {
#endif
void esp_restart(void); 
lv_obj_t * lv_indev_get_active_obj(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_err_t bsp_i2c_init(void);
#ifdef __cplusplus
}
#endif

static lv_obj_t * tv;
static lv_obj_t * tile_launcher;
static lv_obj_t * tile_clock;
static lv_obj_t * tile_settings;
static lv_obj_t * tile_camera;
static lv_obj_t * tile_tools;

static lv_obj_t * label_clock_hours;
static lv_obj_t * label_clock_minutes;
static lv_obj_t * lbl_battery;
static lv_obj_t * lbl_wifi;
static lv_obj_t * btn_wifi_toggle;
static lv_obj_t * lbl_wifi_toggle;
static lv_obj_t * reboot_overlay = NULL;
static lv_obj_t * canvas = NULL;
static lv_obj_t * lbl_cam_status = NULL;

void action_open_clock(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_clock, LV_ANIM_ON);
}

void action_open_settings(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_settings, LV_ANIM_ON);
}

void action_camera(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_camera, LV_ANIM_ON);
    
    // Hide video canvas and show loading text
    if (canvas) {
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (!lbl_cam_status) {
        lbl_cam_status = lv_label_create(tile_camera);
        lv_obj_set_style_text_color(lbl_cam_status, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_cam_status, &lv_font_montserrat_20, 0);
        lv_obj_align(lbl_cam_status, LV_ALIGN_CENTER, 0, -40);
    }
    lv_label_set_text(lbl_cam_status, "Searching for Cam...");
    lv_obj_remove_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN);

    start_camera_stream();
}

void action_tools(lv_event_t * e) {
    // Only transition if the SD card actually successfully mounts
    if (mount_sd_card()) {
        lv_obj_set_tile(tv, tile_tools, LV_ANIM_ON);
        start_file_explorer();
    } else {
        ESP_LOGE("UI", "SD Card Mount Failed. Aborting Tools App entry.");
    }
}

void action_custom_action(lv_event_t * e) {
    // Unused but declared by EEZ
}

static void btn_back_settings_cb(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

static void btn_back_camera_cb(lv_event_t * e) {
    stop_camera_stream();
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

static void btn_back_tools_cb(lv_event_t * e) {
    close_file_explorer();
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

static void btn_capture_cb(lv_event_t * e) {
    save_photo_to_sd();
}

static void tile_click_cb(lv_event_t * e) {
    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_ON);
}

void trigger_return_home(void) {
    if (bsp_display_lock(1000)) {
        lv_obj_t * launcher_scr = lv_obj_get_screen(tv);
        if (lv_screen_active() != launcher_scr) {
            lv_scr_load_anim(launcher_scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
        }
        stop_camera_stream();
        close_file_explorer();
        lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
        bsp_display_unlock();
    }
}

static void indev_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        lv_obj_t * launcher_scr = lv_obj_get_screen(tv);
        if (lv_screen_active() != launcher_scr) {
            lv_obj_t * act_obj = lv_indev_get_active_obj();
            if (act_obj == lv_screen_active() || act_obj == NULL) {
                lv_scr_load_anim(launcher_scr, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
                stop_camera_stream();
                close_file_explorer();
                lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
            }
        }
    }
}

static void update_wifi_toggle_button_ui(void) {
    if (is_wifi_enabled()) {
        lv_obj_set_style_bg_color(btn_wifi_toggle, lv_color_make(0, 150, 255), 0);
        lv_label_set_text(lbl_wifi_toggle, LV_SYMBOL_WIFI " Wi-Fi: ON");
    } else {
        lv_obj_set_style_bg_color(btn_wifi_toggle, lv_color_make(80, 80, 80), 0);
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

static void hardware_poll_timer_cb(lv_timer_t * timer) {
    if (axp2101_check_short_press()) {
        is_screen_on = !is_screen_on;
        if (is_screen_on) {
            bsp_display_backlight_on();
        } else {
            bsp_display_backlight_off();
        }
    }

    if (!is_screen_on) return;

    time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
    char h[8], m[8]; snprintf(h, sizeof(h), "%d", timeinfo.tm_hour); snprintf(m, sizeof(m), "%02d", timeinfo.tm_min);
    if (label_clock_hours) lv_label_set_text(label_clock_hours, h);
    if (label_clock_minutes) lv_label_set_text(label_clock_minutes, m);

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

    static battery_tracker_t my_battery = {
        .alpha = 0.05f,
        .filtered_voltage = 0.0f,
        .is_initialized = false
    };
    static uint32_t battery_poll_ticks = 0;
    static float battery_voltage = 0.0f;
    static float battery_percentage = 0.0f;

    if (battery_poll_ticks % 100 == 0) { 
        battery_update(&my_battery, &battery_voltage, &battery_percentage);
    }
    battery_poll_ticks++;

    if (lbl_battery) {
        bool present = axp2101_is_battery_present();
        bool charging = axp2101_is_charging();
        
        if (present) { 
            char b[32]; 
            snprintf(b, sizeof(b), "%s %.0f%%", charging ? LV_SYMBOL_CHARGE : "Bat:", battery_percentage); 
            lv_label_set_text(lbl_battery, b); 
        } else { 
            lv_label_set_text(lbl_battery, charging ? LV_SYMBOL_CHARGE " USB" : "Bat: --"); 
        }
    }
    
    if (lbl_wifi) lv_label_set_text(lbl_wifi, get_wifi_connected_status() ? LV_SYMBOL_WIFI : "");

    // Check camera connection status to show/hide loading label
    if (is_camera_connected()) {
        if (lbl_cam_status && !lv_obj_has_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN);
            if (canvas) {
                lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        if (canvas && !lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
            if (lbl_cam_status) {
                lv_label_set_text(lbl_cam_status, "Searching for Cam...");
                lv_obj_remove_flag(lbl_cam_status, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Real-Time Camera MJPEG rendering engine (safe main loop update context)
    if (new_frame_ready && canvas != NULL && is_camera_connected()) {
        new_frame_ready = false;
        if (bsp_display_lock(100)) {
            JDEC jd;
            jpeg_decode_t dec = {
                .data = latest_frame_buffer,
                .len = latest_frame_len,
                .offset = 0,
                .out_buf = (uint16_t *)canvas_buffer,
                .out_width = CAM_WIDTH
            };
            
            uint8_t *work_buf = malloc(3100);
            if (work_buf) {
                if (jd_prepare(&jd, jpg_input_func, work_buf, 3100, &dec) == JDR_OK) {
                    jd_decomp(&jd, jpg_output_func, 0);
                    lv_obj_invalidate(canvas);
                }
                free(work_buf);
            }
            bsp_display_unlock();
        }
    }

    ui_tick();
}

void build_ui(void) {
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        bsp_i2c_init();
        bus = bsp_i2c_get_handle();
    }

    if (bus != NULL) {
        axp2101_init_pmu(bus);
        init_pcf85063_rtc(bus);
    }

    init_hardware_button();

    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    
    tv = lv_tileview_create(scr);
    lv_obj_remove_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    
    tile_clock = lv_tileview_add_tile(tv, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_clock, lv_color_black(), 0);
    
    tile_launcher = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_launcher, lv_color_black(), 0);
    
    tile_settings = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_settings, lv_color_black(), 0);

    tile_camera = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_camera, lv_color_black(), 0);

    tile_tools = lv_tileview_add_tile(tv, 4, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_set_style_bg_color(tile_tools, lv_color_black(), 0);

    // --- CLOCK TILE ---
    label_clock_hours = lv_label_create(tile_clock);
    lv_obj_set_style_text_color(label_clock_hours, lv_color_make(0, 150, 255), 0);
    lv_obj_set_style_text_font(label_clock_hours, &arial_160, 0);
    lv_obj_align(label_clock_hours, LV_ALIGN_CENTER, 0, -110); 

    label_clock_minutes = lv_label_create(tile_clock);
    lv_obj_set_style_text_color(label_clock_minutes, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_clock_minutes, &arial_160, 0);
    lv_obj_align(label_clock_minutes, LV_ALIGN_CENTER, 0, 110); 

    lv_obj_add_flag(tile_clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile_clock, tile_click_cb, LV_EVENT_CLICKED, NULL);

    // --- EEZ UI LAUNCHER INTERACTION ---
    create_screens();
    if (objects.main != NULL) {
        lv_obj_set_style_bg_opa(objects.main, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(objects.main, 0, LV_PART_MAIN);

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

        uint32_t child_cnt = lv_obj_get_child_count(objects.main);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = lv_obj_get_child(objects.main, 0);
            if (child != NULL) {
                lv_obj_set_parent(child, tile_launcher);
            }
        }
        
        if (objects.obj0__app_settings_icon_1 != NULL) {
            lv_obj_add_event_cb(objects.obj0__app_settings_icon_1, action_open_settings, LV_EVENT_CLICKED, NULL);
        }
        if (objects.app_settings_icon_2 != NULL) {
            lv_obj_add_event_cb(objects.app_settings_icon_2, action_open_clock, LV_EVENT_CLICKED, NULL);
        }
        if (objects.app_cam_icon != NULL) {
            lv_obj_add_event_cb(objects.app_cam_icon, action_camera, LV_EVENT_CLICKED, NULL);
        }
        if (objects.app_tools_icon != NULL) {
            lv_obj_add_event_cb(objects.app_tools_icon, action_tools, LV_EVENT_CLICKED, NULL);
        }

        lbl_battery = lv_label_create(tile_launcher);
        lv_obj_set_style_text_color(lbl_battery, lv_color_white(), 0);
        lv_obj_align(lbl_battery, LV_ALIGN_TOP_MID, 40, 20);
        
        lbl_wifi = lv_label_create(tile_launcher);
        lv_obj_set_style_text_color(lbl_wifi, lv_color_white(), 0);
        lv_obj_align(lbl_wifi, LV_ALIGN_TOP_MID, -40, 20);
    }

    // --- SETTINGS TILE ---
    lv_obj_add_flag(tile_settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile_settings, tile_click_cb, LV_EVENT_CLICKED, NULL);

    btn_wifi_toggle = lv_button_create(tile_settings);
    lv_obj_set_size(btn_wifi_toggle, 240, 60);
    lv_obj_align(btn_wifi_toggle, LV_ALIGN_CENTER, 0, -70);
    
    lbl_wifi_toggle = lv_label_create(btn_wifi_toggle);
    lv_obj_set_style_text_color(lbl_wifi_toggle, lv_color_white(), 0);
    lv_obj_center(lbl_wifi_toggle);
    
    update_wifi_toggle_button_ui();
    lv_obj_add_event_cb(btn_wifi_toggle, btn_wifi_toggle_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_ap = lv_button_create(tile_settings);
    lv_obj_set_size(btn_ap, 240, 60);
    lv_obj_set_style_bg_color(btn_ap, lv_color_make(50, 50, 50), 0);
    lv_obj_align(btn_ap, LV_ALIGN_CENTER, 0, 10);
    
    lv_obj_t * lbl_ap = lv_label_create(btn_ap);
    lv_label_set_text(lbl_ap, LV_SYMBOL_SETTINGS " Setup AP Mode");
    lv_obj_set_style_text_color(lbl_ap, lv_color_white(), 0);
    lv_obj_center(lbl_ap);
    lv_obj_add_event_cb(btn_ap, btn_ap_mode_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_back_settings = lv_button_create(tile_settings);
    lv_obj_set_size(btn_back_settings, 240, 60);
    lv_obj_set_style_bg_color(btn_back_settings, lv_color_make(100, 100, 100), 0);
    lv_obj_align(btn_back_settings, LV_ALIGN_CENTER, 0, 90);
    
    lv_obj_t * lbl_back_settings = lv_label_create(btn_back_settings);
    lv_label_set_text(lbl_back_settings, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(lbl_back_settings, lv_color_white(), 0);
    lv_obj_center(lbl_back_settings);
    lv_obj_add_event_cb(btn_back_settings, btn_back_settings_cb, LV_EVENT_CLICKED, NULL);

    // --- CAMERA TILE ---
    canvas = lv_canvas_create(tile_camera);
    if (canvas_buffer == NULL) {
        canvas_buffer = heap_caps_malloc(CAM_WIDTH * CAM_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    }
    lv_canvas_set_buffer(canvas, canvas_buffer, CAM_WIDTH, CAM_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t * btn_capture = lv_button_create(tile_camera);
    lv_obj_set_size(btn_capture, 120, 50);
    lv_obj_align(btn_capture, LV_ALIGN_BOTTOM_LEFT, 30, -20);
    lv_obj_t * lbl_capture = lv_label_create(btn_capture);
    lv_label_set_text(lbl_capture, LV_SYMBOL_SAVE " Save");
    lv_obj_center(lbl_capture);
    lv_obj_add_event_cb(btn_capture, btn_capture_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_back_camera = lv_button_create(tile_camera);
    lv_obj_set_size(btn_back_camera, 120, 50);
    lv_obj_align(btn_back_camera, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
    lv_obj_t * lbl_back_camera = lv_label_create(btn_back_camera);
    lv_label_set_text(lbl_back_camera, LV_SYMBOL_LEFT " Exit");
    lv_obj_center(lbl_back_camera);
    lv_obj_add_event_cb(btn_back_camera, btn_back_camera_cb, LV_EVENT_CLICKED, NULL);

    // --- TOOLS (FILE EXPLORER) TILE ---
    lv_obj_t * btn_back_tools = lv_button_create(tile_tools);
    lv_obj_set_size(btn_back_tools, 120, 45);
    lv_obj_align(btn_back_tools, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_t * lbl_back_tools = lv_label_create(btn_back_tools);
    lv_label_set_text(lbl_back_tools, LV_SYMBOL_LEFT " Close");
    lv_obj_center(lbl_back_tools);
    lv_obj_add_event_cb(btn_back_tools, btn_back_tools_cb, LV_EVENT_CLICKED, NULL);

    // Setup global touchpad indev event cb
    lv_indev_t * indev = lv_indev_get_next(NULL);
    if (indev) {
        lv_indev_add_event_cb(indev, indev_event_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_set_tile(tv, tile_launcher, LV_ANIM_OFF);
    lv_timer_create(hardware_poll_timer_cb, 50, NULL);
}