/*
 * Smartwatch OS Audio Implementation
 * Ported from Arduino to pure ESP-IDF v5.4
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "es8311.h"

#define EXAMPLE_SAMPLE_RATE     16000
#define EXAMPLE_VOICE_VOLUME    100 // Set to 100 for maximum volume boost (writes 0xFF)
#define EXAMPLE_MIC_GAIN        ES8311_MIC_GAIN_18DB

#define I2C_PORT_NUM            I2C_NUM_0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "smartwatch_audio";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t es8311_dev_handle = NULL;

#define PA_ENABLE_GPIO 46

void init_pa_power(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PA_ENABLE_GPIO, 1);
}

esp_err_t i2c_master_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM,
        .scl_io_num = 14,
        .sda_io_num = 15,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, &i2c_bus_handle);
}

/* 
 * Explicitly configure the AXP2101 PMU over I2C to enable ALDO3 at 3.3V
 * to supply power to the ES8311 Audio Codec
 */
esp_err_t enable_axp2101_audio_power(void)
{
    i2c_device_config_t pmu_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34, // AXP2101 default I2C address
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t pmu_dev;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &pmu_cfg, &pmu_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 may already be registered or not found: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set ALDO3 to 3.3V (0x1C corresponds to 3.3V, range 0.5V-3.5V, 100mV/step)
    uint8_t write_vol[2] = {0x94, 0x1C};
    ret = i2c_master_transmit(pmu_dev, write_vol, sizeof(write_vol), 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ALDO3 voltage");
    }

    // Read current LDOS ON/OFF control (Register 0x90)
    uint8_t reg_90_addr = 0x90;
    uint8_t reg_90_val = 0;
    ret = i2c_master_transmit_receive(pmu_dev, &reg_90_addr, 1, &reg_90_val, 1, 1000);
    if (ret == ESP_OK) {
        // Enable ALDO3 (Bit 2)
        reg_90_val |= (1 << 2); 
        uint8_t write_en[2] = {0x90, reg_90_val};
        ret = i2c_master_transmit(pmu_dev, write_en, sizeof(write_en), 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "AXP2101 ALDO3 (Audio Codec Power) enabled successfully");
        } else {
            ESP_LOGE(TAG, "Failed to write Reg 0x90 to enable ALDO3");
        }
    } else {
        ESP_LOGE(TAG, "Failed to read Reg 0x90");
    }

    i2c_master_bus_remove_device(pmu_dev);
    return ret;
}

esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Allocate TX and RX channels in full-duplex mode
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_16,
            .bclk = GPIO_NUM_41,
            .ws = GPIO_NUM_45,
            .dout = GPIO_NUM_42, // Corrected: Sends audio data to ES8311 DAC
            .din = GPIO_NUM_40,  // Corrected: Receives audio data from Microphone
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));

    // Enable channels
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    return ESP_OK;
}

esp_err_t es8311_codec_init(void) {
    es8311_handle_t es_handle = es8311_create(es8311_dev_handle);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");
    
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
        .sample_frequency = EXAMPLE_SAMPLE_RATE
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency), TAG, "set es8311 sample frequency failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG, "set es8311 microphone failed");
    
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL), TAG, "set es8311 volume failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, EXAMPLE_MIC_GAIN), TAG, "set es8311 microphone gain failed");
    return ESP_OK;
}

static void play_sine_tone(int frequency_hz, int duration_ms)
{
    int samples_count = (EXAMPLE_SAMPLE_RATE * duration_ms) / 1000;
    int16_t *sine_buffer = malloc(samples_count * 2 * sizeof(int16_t));
    if (!sine_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for chime tone");
        return;
    }

    for (int i = 0; i < samples_count; i++) {
        double t = (double)i / EXAMPLE_SAMPLE_RATE;
        double val = sin(2.0 * M_PI * frequency_hz * t);
        int16_t sample = (int16_t)(val * 30000.0); // Boosted signal amplitude close to 16-bit maximum (32767)

        sine_buffer[i * 2] = sample;     // Left Channel
        sine_buffer[i * 2 + 1] = sample; // Right Channel
    }

    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, sine_buffer, samples_count * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(sine_buffer);
}

void play_startup_chime(void)
{
    ESP_LOGI(TAG, "Playing watch startup chime...");
    play_sine_tone(523, 200); // C5
    vTaskDelay(pdMS_TO_TICKS(50));
    play_sine_tone(659, 200); // E5
    vTaskDelay(pdMS_TO_TICKS(50));
    play_sine_tone(784, 400); // G5
}

#define EXAMPLE_RECV_BUF_SIZE (4096)

static void audio_echo_task(void *pvParameters)
{
    uint8_t *mic_data = malloc(EXAMPLE_RECV_BUF_SIZE);
    if (!mic_data) {
        ESP_LOGE(TAG, "Failed to allocate mic data buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting audio echo loopback task...");

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, mic_data, EXAMPLE_RECV_BUF_SIZE, &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG, "I2S read failed or read 0 bytes");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytes_written = 0;
        ret = i2s_channel_write(tx_handle, mic_data, bytes_read, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK || bytes_written == 0) {
            ESP_LOGW(TAG, "I2S write failed");
        }
    }

    free(mic_data);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing Smartwatch Audio OS...");

    // 1. Initialize power management/enable pins
    init_pa_power();

    // 2. Initialize I2C Bus using Modern Master Driver
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C initialized successfully (SDA=15, SCL=14)");

    // 3. Configure the PMU to turn on the codec and amplifier voltage rail
    ret = enable_axp2101_audio_power();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AXP2101 PMU audio power rail");
    }

    // 4. Initialize I2S Bus (Full-Duplex Standard Mode)
    ret = i2s_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2S initialized successfully (BCLK=41, WS=45, DOUT=42, DIN=40, MCLK=16)");

    // 5. Initialize ES8311 Audio Codec
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDRRES_0,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &es8311_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ES8311 device on I2C bus");
        return;
    }

    ret = es8311_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 codec initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "ES8311 codec initialized successfully");

    // 6. Play startup chime sound to confirm speaker output works flawlessly
    play_startup_chime();

    // 7. Create background FreeRTOS task for real-time mic-to-speaker echo loopback
    xTaskCreate(audio_echo_task, "audio_echo_task", 4096, NULL, 5, NULL);
}