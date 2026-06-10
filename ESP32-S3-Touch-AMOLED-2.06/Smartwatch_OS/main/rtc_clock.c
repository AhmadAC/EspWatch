// File: Smartwatch_OS/main/rtc_clock.c
#include "rtc_clock.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "RTC_CLOCK";
static i2c_master_dev_handle_t rtc_handle = NULL;

static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

static uint8_t bcd_to_dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void sync_external_rtc_time(void) {
    if (!rtc_handle) return;
    
    time_t now;
    struct tm t;
    time(&now);
    localtime_r(&now, &t);
    
    uint8_t buf[8];
    buf[0] = 0x03; // Start writing from register 0x03 (Seconds)
    buf[1] = dec_to_bcd(t.tm_sec);
    buf[2] = dec_to_bcd(t.tm_min);
    buf[3] = dec_to_bcd(t.tm_hour);
    buf[4] = dec_to_bcd(t.tm_mday);
    buf[5] = dec_to_bcd(t.tm_wday);
    buf[6] = dec_to_bcd(t.tm_mon + 1);    // 0-11 to 1-12 range
    buf[7] = dec_to_bcd(t.tm_year % 100);  // 2-digit representation
    
    if (i2c_master_transmit(rtc_handle, buf, 8, -1) == ESP_OK) {
        ESP_LOGI(TAG, "Hardware PCF85063 RTC time synced successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to sync Hardware PCF85063 RTC.");
    }
}

static bool rtc_get_time(struct tm *t) {
    if (!rtc_handle) return false;
    uint8_t reg = 0x03;
    uint8_t buf[7] = {0};
    
    if (i2c_master_transmit_receive(rtc_handle, &reg, 1, buf, 7, -1) == ESP_OK) {
        t->tm_sec = bcd_to_dec(buf[0] & 0x7F);
        t->tm_min = bcd_to_dec(buf[1] & 0x7F);
        t->tm_hour = bcd_to_dec(buf[2] & 0x3F);
        t->tm_mday = bcd_to_dec(buf[3] & 0x3F);
        t->tm_wday = bcd_to_dec(buf[4] & 0x07);
        t->tm_mon = bcd_to_dec(buf[5] & 0x1F) - 1;   // 1-12 to 0-11 range
        t->tm_year = bcd_to_dec(buf[6]) + 100;       // Year relative to 1900
        return true;
    }
    return false;
}

void init_pcf85063_rtc(i2c_master_bus_handle_t bus) {
    if (rtc_handle != NULL) return;
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x51, // PCF85063 RTC I2C address
        .scl_speed_hz = 400000,
    };

    if (i2c_master_bus_add_device(bus, &dev_cfg, &rtc_handle) == ESP_OK) {
        ESP_LOGI(TAG, "External PCF85063 RTC attached successfully!");
        
        // Read external RTC clock on boot & apply to system
        struct tm rtc_time = {0};
        if (rtc_get_time(&rtc_time)) {
            if (rtc_time.tm_year >= 126) { // Verify year is valid (>= 2026)
                time_t epoch = mktime(&rtc_time);
                struct timeval tv_now = { .tv_sec = epoch, .tv_usec = 0 };
                settimeofday(&tv_now, NULL);
                ESP_LOGI(TAG, "Restored system time from Hardware RTC: %02d:%02d:%02d", rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to connect to PCF85063 RTC");
    }
}