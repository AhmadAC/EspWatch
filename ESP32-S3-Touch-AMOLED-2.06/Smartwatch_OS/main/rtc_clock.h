// File: Smartwatch_OS/main/rtc_clock.h
#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

void init_pcf85063_rtc(i2c_master_bus_handle_t bus);
void sync_external_rtc_time(void);

#endif // RTC_CLOCK_H