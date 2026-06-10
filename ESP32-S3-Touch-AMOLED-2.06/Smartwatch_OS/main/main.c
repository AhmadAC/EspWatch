// File: Smartwatch_OS/main/main.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "ui_app.h"
#include "wifi_app.h"

static const char *TAG = "SmartwatchOS";

void app_main(void) {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Init Time (Fallback to PDT)
    time_t now;
    time(&now);
    if (now < 1767225600) {
        // Fallback to June 8, 2026 15:51:00 PDT
        struct timeval tv_time = { .tv_sec = 1775688660 };
        settimeofday(&tv_time, NULL);
        ESP_LOGI(TAG, "Initializing to fallback: Jun 8, 2026 15:51:00 PDT");
    }

    // 3. Load Persistent Timezone
    nvs_handle_t my_handle;
    char saved_tz[32] = {0};
    if (nvs_open("smartwatch", NVS_READONLY, &my_handle) == ESP_OK) {
        size_t required_size = sizeof(saved_tz);
        nvs_get_str(my_handle, "wifi_tz", saved_tz, &required_size);
        nvs_close(my_handle);
    }
    if (strlen(saved_tz) > 0) {
        setenv("TZ", saved_tz, 1);
        ESP_LOGI(TAG, "Restored persistent timezone: %s", saved_tz);
    } else {
        setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
        ESP_LOGI(TAG, "Defaulting to PST8PDT (Los Angeles).");
    }
    tzset();

    // 4. Init Screen and Display Hardware
    ESP_LOGI(TAG, "Initializing Board Support Package...");
    if (bsp_display_start() != NULL) {
        bsp_display_backlight_on();
    }

    // 5. Build Graphic UI (1000ms timeout prevents hard FreeRTOS mutex crashes on boot)
    bsp_display_lock(1000);
    build_ui();
    bsp_display_unlock();

    // 6. Delay and start background Wi-Fi (prevents hardware race conditions)
    vTaskDelay(pdMS_TO_TICKS(1000));
    init_wifi();
}