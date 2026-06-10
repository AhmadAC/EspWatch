// File: Smartwatch_OS/main/battery.h
#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

typedef struct {
    float alpha;             // Filtering factor (e.g., 0.05f). Smaller = smoother, slower.
    float filtered_voltage;  // Smoothed battery voltage in Volts
    bool is_initialized;     // Flag to check if we need to seed the filter
} battery_tracker_t;

bool axp2101_init_pmu(i2c_master_bus_handle_t bus);
float axp2101_get_voltage(void);
bool axp2101_is_battery_present(void);
bool axp2101_is_charging(void);
bool axp2101_check_short_press(void);
uint8_t axp2101_get_internal_percentage(void);
void battery_update(battery_tracker_t *tracker, float *out_voltage, float *out_percentage);

#endif // BATTERY_H