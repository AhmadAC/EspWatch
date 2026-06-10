// File: Smartwatch_OS/main/battery.c
#include "battery.h"
#include "esp_log.h"

static const char *TAG = "BATTERY_DRV";
static i2c_master_dev_handle_t axp_handle = NULL;

typedef struct {
    float voltage;
    float percentage;
} lut_point_t;

static const lut_point_t lipo_lut[] = {
    {4.18f, 100.0f},
    {4.10f, 90.0f},
    {4.00f, 80.0f},
    {3.90f, 65.0f},
    {3.82f, 50.0f},
    {3.77f, 40.0f},
    {3.73f, 30.0f},
    {3.70f, 20.0f},
    {3.65f, 10.0f},
    {3.55f, 5.0f},
    {3.30f, 0.0f}
};
#define LUT_SIZE (sizeof(lipo_lut) / sizeof(lipo_lut[0]))

static inline bool axp_write(uint8_t reg, uint8_t val) {
    if (!axp_handle) return false;
    uint8_t data[2] = { reg, val };
    return i2c_master_transmit(axp_handle, data, sizeof(data), -1) == ESP_OK;
}

static inline bool axp_read(uint8_t reg, uint8_t *val) {
    if (!axp_handle) return false;
    return i2c_master_transmit_receive(axp_handle, &reg, 1, val, 1, -1) == ESP_OK;
}

bool axp2101_init_pmu(i2c_master_bus_handle_t bus) {
    if (axp_handle != NULL) return true;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34,
        .scl_speed_hz = 400000,
    };

    if (i2c_master_bus_add_device(bus, &dev_cfg, &axp_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach AXP2101 to I2C bus");
        axp_handle = NULL;
        return false;
    }

    uint8_t reg_val;
    if (axp_read(0x18, &reg_val)) {
        axp_write(0x18, reg_val | 0x08);
        ESP_LOGI(TAG, "AXP2101 initialized and Fuel Gauge enabled!");
    } else {
        ESP_LOGW(TAG, "AXP2101 not responding at address 0x34");
        return false;
    }

    // Apply configuration parameters sourced from XPowersLib
    
    // Disable unused power channels (DC2, DC3, DC4, DC5)
    if (axp_read(0x80, &reg_val)) {
        reg_val &= ~(0x1E); 
        axp_write(0x80, reg_val);
    }
    
    // Disable ALDO1-4, BLDO1-2, CPUSLDO, DLDO1
    axp_write(0x90, 0x00);
    // Disable DLDO2
    if (axp_read(0x91, &reg_val)) {
        reg_val &= ~(0x01);
        axp_write(0x91, reg_val);
    }

    // Set DC1 Voltage to 3300mV and enable it
    if (axp_read(0x82, &reg_val)) {
        axp_write(0x82, (reg_val & 0xE0) | 0x12); // (3300-1500)/100 = 18 = 0x12
    }
    if (axp_read(0x80, &reg_val)) {
        axp_write(0x80, reg_val | 0x01);
    }

    // Set ALDO1 Voltage to 3300mV and enable it
    if (axp_read(0x92, &reg_val)) {
        axp_write(0x92, (reg_val & 0xE0) | 0x1C); // (3300-500)/100 = 28 = 0x1C
    }
    if (axp_read(0x90, &reg_val)) {
        axp_write(0x90, reg_val | 0x01);
    }

    // Enable ADCs (VBUS, BATT, SYS, TEMP) but disable TS pin
    if (axp_read(0x30, &reg_val)) {
        reg_val |= (1 << 2) | (1 << 0) | (1 << 3) | (1 << 4);
        reg_val &= ~(1 << 1); // Disable TS pin
        axp_write(0x30, reg_val);
    }

    // Disable all IRQs
    axp_write(0x40, 0x00);
    axp_write(0x41, 0x00);
    axp_write(0x42, 0x00);

    // Clear IRQ Status
    axp_write(0x48, 0xFF);
    axp_write(0x49, 0xFF);
    axp_write(0x4A, 0xFF);

    // Enable specific IRQs: BAT_INSERT, BAT_REMOVE, VBUS_INSERT, VBUS_REMOVE, PKEY_SHORT, PKEY_LONG, BAT_CHG_DONE, BAT_CHG_START
    axp_write(0x41, (1 << 5) | (1 << 4) | (1 << 7) | (1 << 6) | (1 << 3) | (1 << 2));
    axp_write(0x42, (1 << 4) | (1 << 3));

    // Set precharge current to 50mA
    if (axp_read(0x61, &reg_val)) {
        axp_write(0x61, (reg_val & 0xFC) | 0x02);
    }

    // Set constant charge current to 400mA
    if (axp_read(0x62, &reg_val)) {
        axp_write(0x62, (reg_val & 0xE0) | 0x07);
    }

    // Set termination current to 25mA
    if (axp_read(0x63, &reg_val)) {
        axp_write(0x63, (reg_val & 0xF0) | 0x01);
    }

    // Set charge target voltage to 4.2V
    if (axp_read(0x64, &reg_val)) {
        axp_write(0x64, (reg_val & 0xFC) | 0x02);
    }

    return true;
}

float axp2101_get_voltage(void) {
    uint8_t hi = 0, lo = 0;
    if (!axp_read(0x34, &hi)) return 0.0f; // AXP2101_REG_VBAT_H
    if (!axp_read(0x35, &lo)) return 0.0f; // AXP2101_REG_VBAT_L
    
    // AXP2101 ADC is 13-bit for Battery Voltage: High 5 bits in 0x34, Low 8 bits in 0x35
    uint16_t raw = ((hi & 0x1F) << 8) | lo;
    
    // 1 LSB = 1 mV
    return raw / 1000.0f;
}

bool axp2101_is_battery_present(void) {
    // Fallback to strict voltage check because the AXP internal status bit is notoriously 
    // flaky if the NTC/TS pin on the battery is unpopulated or floating.
    return axp2101_get_voltage() > 2.5f;
}

bool axp2101_is_charging(void) {
    uint8_t st1 = 0, st2 = 0;
    if (axp_read(0x00, &st1) && axp_read(0x01, &st2)) {
        bool vbus_present = (st1 & 0x10) != 0; // Bit 4: VBUS_present
        bool charging_active = ((st2 & 0x60) == 0x20); // Bits 6:5 = 0b01 (Charging)
        return vbus_present && charging_active;
    }
    return false;
}

bool axp2101_check_short_press(void) {
    uint8_t irq_status1 = 0;
    uint8_t irq_status2 = 0;
    uint8_t irq_status3 = 0;
    bool pressed = false;

    // AXP2101 groups interrupts into 3 registers. We read & drain them so they don't pile up.
    if (axp_read(0x48, &irq_status1) && irq_status1 != 0) {
        axp_write(0x48, irq_status1);
    }
    
    if (axp_read(0x49, &irq_status2) && irq_status2 != 0) {
        if (irq_status2 & 0x08) { // Bit 3 is Short press
            pressed = true;
        }
        axp_write(0x49, irq_status2);
    }
    
    if (axp_read(0x4A, &irq_status3) && irq_status3 != 0) {
        axp_write(0x4A, irq_status3);
    }

    return pressed;
}

uint8_t axp2101_get_internal_percentage(void) {
    uint8_t soc = 0;
    if (axp_read(0xA4, &soc)) { // AXP2101_REG_SOC
        if (soc > 100) return 100;
        return soc;
    }
    return 0;
}

static float interpolate_percentage(float voltage) {
    if (voltage >= lipo_lut[0].voltage) return 100.0f;
    if (voltage <= lipo_lut[LUT_SIZE - 1].voltage) return 0.0f;
    
    for (int i = 0; i < LUT_SIZE - 1; i++) {
        if (voltage <= lipo_lut[i].voltage && voltage >= lipo_lut[i + 1].voltage) {
            float v0 = lipo_lut[i + 1].voltage;
            float v1 = lipo_lut[i].voltage;
            float p0 = lipo_lut[i + 1].percentage;
            float p1 = lipo_lut[i].percentage;
            
            return p0 + (voltage - v0) * (p1 - p0) / (v1 - v0);
        }
    }
    return 0.0f;
}

void battery_update(battery_tracker_t *tracker, float *out_voltage, float *out_percentage) {
    if (!axp2101_is_battery_present()) {
        *out_voltage = 0.0f;
        *out_percentage = 0.0f;
        return;
    }
    
    float raw_voltage = axp2101_get_voltage();
    if (raw_voltage < 2.0f) { // Sanity check for faulty reading
        *out_voltage = 0.0f;
        *out_percentage = 0.0f;
        return;
    }
    
    // 1. Smooth out voltage spikes & sags using an Exponential Moving Average (EMA)
    if (!tracker->is_initialized) {
        tracker->filtered_voltage = raw_voltage;
        tracker->is_initialized = true;
    } else {
        tracker->filtered_voltage = (tracker->alpha * raw_voltage) + 
                                    ((1.0f - tracker->alpha) * tracker->filtered_voltage);
    }
    
    *out_voltage = tracker->filtered_voltage;
    
    // 2. Dual-mode calculation depending on charging state
    if (axp2101_is_charging()) {
        // Fall back directly to the AXP2101's internal Coulomb Counter register during charge
        *out_percentage = (float)axp2101_get_internal_percentage();
    } else {
        // Map heavily filtered voltage on the LUT while discharging
        *out_percentage = interpolate_percentage(tracker->filtered_voltage);
    }
}