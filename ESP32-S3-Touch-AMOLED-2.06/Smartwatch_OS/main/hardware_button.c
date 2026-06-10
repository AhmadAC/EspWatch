// File: Smartwatch_OS/main/hardware_button.c
#include "hardware_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "ui_app.h"

#define USER_BUTTON_PIN GPIO_NUM_0 
static const char *TAG = "HW_BUTTON";

volatile bool is_screen_on = true;

static void button_task(void *pvParameters) {
    bool last_state = true;
    uint32_t press_time = 0;
    uint32_t release_time = 0;
    int click_count = 0;
    bool waiting_for_double = false;
    
    while (1) {
        bool current_state = gpio_get_level(USER_BUTTON_PIN);
        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // Button Pressed (Active Low)
        if (last_state == true && current_state == false) { 
            press_time = now_ms;
        }
        // Button Released
        else if (last_state == false && current_state == true) { 
            release_time = now_ms;
            if (release_time - press_time > 20) { // Debounce threshold
                click_count++;
                waiting_for_double = true;
            }
        }
        
        // Process clicks after timeout to differentiate single vs double click
        if (waiting_for_double && (now_ms - release_time > 300)) {
            if (click_count == 1) {
                // Single click toggles screen state
                is_screen_on = !is_screen_on;
                if (is_screen_on) bsp_display_backlight_on();
                else bsp_display_backlight_off();
            } else if (click_count >= 2) {
                // Double click forces return to home screen
                ESP_LOGI(TAG, "BOOT Button Double-Click Detected!");
                trigger_return_home();
            }
            click_count = 0;
            waiting_for_double = false;
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll highly responsively every 10ms
    }
}

void init_hardware_button(void) {
    gpio_config_t io_conf = { 
        .pin_bit_mask = (1ULL << USER_BUTTON_PIN), 
        .mode = GPIO_MODE_INPUT, 
        .pull_up_en = 1 
    };
    gpio_config(&io_conf);
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);
}