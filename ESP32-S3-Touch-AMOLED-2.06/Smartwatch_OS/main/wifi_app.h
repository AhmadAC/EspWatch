// File: Smartwatch_OS/main/wifi_app.h
#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <stdbool.h>

void init_wifi(void);
void start_ap_mode_task(void);
bool get_wifi_connected_status(void);
int get_reboot_timer(void);
void decrement_reboot_timer(void);

// Wi-Fi initialization state check
bool is_wifi_initialized(void);

// Wi-Fi enable/disable controls
bool is_wifi_enabled(void);
void toggle_wifi(void);

// Sync system time to the hardware PCF85063 RTC
void sync_external_rtc_time(void);

#endif // WIFI_APP_H