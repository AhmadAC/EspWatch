// File: Smartwatch_OS/main/file_explorer.c
#include "file_explorer.h"
#include "camera_recv.h"
#include "ui_app.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "FILE_EXPLORER";

static sdmmc_card_t *card = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static lv_obj_t * explorer_container = NULL;
static lv_obj_t * file_list = NULL;
static lv_obj_t * text_viewer = NULL;
static lv_obj_t * img_viewer = NULL;

static char current_dir[256] = "/sdcard";

static void refresh_directory_list(const char *path);
static void file_click_cb(lv_event_t *e);

static i2c_master_dev_handle_t es8311_handle = NULL;

static volatile bool wav_playing = false;
static TaskHandle_t wav_task_handle = NULL;

static volatile bool mjpeg_playing = false;
static TaskHandle_t mjpeg_task_handle = NULL;
static lv_obj_t *canvas_img = NULL;
static uint8_t *mjpeg_canvas_buf = NULL;

/*
 * Persistent I2C device acquisition for ES8311
 */
static bool init_es8311_device(void) {
    if (es8311_handle != NULL) return true;
    
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        bsp_i2c_init();
        bus = bsp_i2c_get_handle();
    }
    if (bus == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C master bus handle");
        return false;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18, // ES8311 I2C address
        .scl_speed_hz = 100000,
    };

    if (i2c_master_bus_add_device(bus, &dev_cfg, &es8311_handle) == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 I2C device added successfully");
        return true;
    }
    ESP_LOGE(TAG, "Failed to add ES8311 I2C device");
    return false;
}

static bool es8311_write_reg(uint8_t reg, uint8_t val) {
    if (!init_es8311_device()) return false;
    uint8_t data[2] = { reg, val };
    return i2c_master_transmit(es8311_handle, data, 2, -1) == ESP_OK;
}

static bool es8311_read_reg(uint8_t reg, uint8_t *val) {
    if (!init_es8311_device()) return false;
    return i2c_master_transmit_receive(es8311_handle, &reg, 1, val, 1, -1) == ESP_OK;
}

/*
 * Official ES8311 Clock Coefficient Structure and Table
 */
struct _coeff_div {
    uint32_t mclk;        /* mclk frequency */
    uint32_t rate;        /* sample rate */
    uint8_t pre_div;      /* the pre divider with range from 1 to 8 */
    uint8_t pre_multi;    /* the pre multiplier with 0: 1x, 2x, 4x, 8x selection */
    uint8_t adc_div;      /* adcclk divider */
    uint8_t dac_div;      /* dacclk divider */
    uint8_t fs_mode;      /* double speed or single speed, =0, ss, =1, ds */
    uint8_t lrck_h;       /* adclrck divider and daclrck divider */
    uint8_t lrck_l;
    uint8_t bclk_div;     /* sclk divider */
    uint8_t adc_osr;      /* adc osr */
    uint8_t dac_osr;      /* dac osr */
};

static const struct _coeff_div coeff_div[] = {
    {12288000, 8000, 0x06, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 11025, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 12000, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 16000, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 22050, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 24000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 32000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10}
};

static int get_coeff(uint32_t mclk, uint32_t rate) {
    int size = sizeof(coeff_div) / sizeof(coeff_div[0]);
    for (int i = 0; i < size; i++) {
        if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk) {
            return i;
        }
    }
    return -1;
}

static esp_err_t es8311_sample_frequency_config(uint32_t mclk_frequency, uint32_t sample_frequency) {
    uint8_t regv;
    int coeff = get_coeff(mclk_frequency, sample_frequency);
    if (coeff < 0) {
        ESP_LOGE(TAG, "Unable to configure sample rate %luHz with %luHz MCLK", sample_frequency, mclk_frequency);
        return ESP_ERR_INVALID_ARG;
    }

    const struct _coeff_div *selected_coeff = &coeff_div[coeff];

    es8311_read_reg(0x02, &regv);
    regv &= 0x07;
    regv |= (selected_coeff->pre_div - 1) << 5;
    regv |= selected_coeff->pre_multi << 3;
    es8311_write_reg(0x02, regv);

    uint8_t reg03 = (selected_coeff->fs_mode << 6) | selected_coeff->adc_osr;
    es8311_write_reg(0x03, reg03);
    es8311_write_reg(0x04, selected_coeff->dac_osr);

    uint8_t reg05 = ((selected_coeff->adc_div - 1) << 4) | (selected_coeff->dac_div - 1);
    es8311_write_reg(0x05, reg05);

    es8311_read_reg(0x06, &regv);
    regv &= 0xE0;
    if (selected_coeff->bclk_div < 19) {
        regv |= (selected_coeff->bclk_div - 1) << 0;
    } else {
        regv |= (selected_coeff->bclk_div) << 0;
    }
    es8311_write_reg(0x06, regv);

    es8311_read_reg(0x07, &regv);
    regv &= 0xC0;
    regv |= selected_coeff->lrck_h << 0;
    es8311_write_reg(0x07, regv);
    es8311_write_reg(0x08, selected_coeff->lrck_l);

    return ESP_OK;
}

void init_es8311_codec(uint32_t sample_rate, uint16_t bits_per_sample) {
    es8311_write_reg(0x00, 0x1F); // Reset digital, CSM, clock manager
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write_reg(0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    es8311_write_reg(0x00, 0x80); // Power-on command

    uint32_t mclk_frequency = sample_rate * 256; 
    es8311_write_reg(0x01, 0x3F); // Enable all internal clocks, select MCLK pin input

    uint8_t reg06;
    es8311_read_reg(0x06, &reg06);
    reg06 &= ~0x20; // SCLK not inverted
    es8311_write_reg(0x06, reg06);

    es8311_sample_frequency_config(mclk_frequency, sample_rate);

    uint8_t reg00;
    es8311_read_reg(0x00, &reg00);
    reg00 &= 0xBF; // Set to slave mode
    es8311_write_reg(0x00, reg00);

    uint8_t res_val = 0x0C; // default 16-bit
    if (bits_per_sample == 24) res_val = 0x00;
    else if (bits_per_sample == 32) res_val = 0x10;

    es8311_write_reg(0x09, res_val); // SDP In
    es8311_write_reg(0x0A, res_val); // SDP Out

    es8311_write_reg(0x0D, 0x01); // Power up analog circuitry
    es8311_write_reg(0x0E, 0x02); // Enable analog PGA, enable ADC modulator
    es8311_write_reg(0x12, 0x00); // Power up DAC
    es8311_write_reg(0x13, 0x10); // Enable output to HP drive
    es8311_write_reg(0x1C, 0x6A); // ADC Equalizer bypass, cancel DC offset
    es8311_write_reg(0x37, 0x08); // Bypass DAC equalizer
    es8311_write_reg(0x32, 0xBF); // Set default output volume (~90%)
}

void init_i2s_audio(uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample) {
    if (tx_chan != NULL) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_slot_mode_t slot_mode = (num_channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
    i2s_data_bit_width_t bits_cfg = I2S_DATA_BIT_WIDTH_16BIT;
    if (bits_per_sample == 24) bits_cfg = I2S_DATA_BIT_WIDTH_24BIT;
    else if (bits_per_sample == 32) bits_cfg = I2S_DATA_BIT_WIDTH_32BIT;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_cfg, slot_mode),
        .gpio_cfg = {
            .mclk = GPIO_NUM_16,
            .bclk = GPIO_NUM_41,
            .ws = GPIO_NUM_45,
            .dout = GPIO_NUM_42,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_46),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_46, 1); // Enable PA amplifier
}

bool mount_sd_card(void) {
    if (card != NULL) return true;

    ESP_LOGI(TAG, "Mounting SD Card...");

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = GPIO_NUM_1,
        .miso_io_num = GPIO_NUM_3,
        .sclk_io_num = GPIO_NUM_2,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus.");
        return false;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = GPIO_NUM_17;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem.");
        spi_bus_free(host.slot);
        card = NULL;
        return false;
    }

    ESP_LOGI(TAG, "SD Card mounted successfully.");
    return true;
}

static void wav_play_task(void *arg) {
    char *filepath = (char *)arg;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open audio file: %s", filepath);
        free(filepath);
        wav_playing = false;
        wav_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        fclose(f);
        free(filepath);
        wav_playing = false;
        wav_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        ESP_LOGE(TAG, "Not a valid WAV file");
        free(filepath);
        wav_playing = false;
        wav_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint32_t sample_rate = *(uint32_t*)(header + 24);
    uint16_t num_channels = *(uint16_t*)(header + 22);
    uint16_t bits_per_sample = *(uint16_t*)(header + 34);

    ESP_LOGI(TAG, "WAV info: rate=%lu, channels=%u, bits=%u", sample_rate, num_channels, bits_per_sample);

    init_es8311_codec(sample_rate, bits_per_sample);
    init_i2s_audio(sample_rate, num_channels, bits_per_sample);

    size_t chunk_size = 4096;
    uint8_t *buf = malloc(chunk_size);
    if (!buf) {
        fclose(f);
        free(filepath);
        wav_playing = false;
        wav_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read;
    while (wav_playing && (bytes_read = fread(buf, 1, chunk_size, f)) > 0) {
        size_t bytes_written = 0;
        i2s_channel_write(tx_chan, buf, bytes_read, &bytes_written, portMAX_DELAY);
    }

    free(buf);
    fclose(f);
    free(filepath);

    gpio_set_level(GPIO_NUM_46, 0); // Disable PA
    ESP_LOGI(TAG, "Audio playback completed");
    wav_playing = false;
    wav_task_handle = NULL;
    vTaskDelete(NULL);
}

void play_wav_file(const char *filepath) {
    if (wav_playing) {
        wav_playing = false;
        vTaskDelay(pdMS_TO_TICKS(150)); // wait for old task to cleanly exit
    }
    
    char *path_copy = strdup(filepath);
    wav_playing = true;
    xTaskCreate(wav_play_task, "wav_play", 4096, path_copy, 5, &wav_task_handle);
}

static void btn_delete_cb(lv_event_t *e) {
    void *user_data = lv_event_get_user_data(e);
    if (user_data && strcmp((char*)user_data, "..") != 0) {
        free(user_data);
    }
}

static void close_text_viewer_cb(lv_event_t *e) {
    if (text_viewer) {
        lv_obj_delete(text_viewer);
        text_viewer = NULL;
    }
}

static void view_text_file(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char *buf = malloc(4096);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t bytes_read = fread(buf, 1, 4095, f);
    buf[bytes_read] = '\0';
    fclose(f);

    text_viewer = lv_obj_create(explorer_container);
    lv_obj_set_size(text_viewer, 410, 502);
    lv_obj_set_style_bg_color(text_viewer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(text_viewer, LV_OPA_COVER, 0);
    lv_obj_center(text_viewer);
    lv_obj_move_foreground(text_viewer);

    lv_obj_t *ta = lv_textarea_create(text_viewer);
    lv_obj_set_size(ta, 390, 420);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 10);
    lv_textarea_set_text(ta, buf);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta, lv_color_black(), 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);

    lv_obj_t *btn_close = lv_button_create(text_viewer);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, close_text_viewer_cb, LV_EVENT_CLICKED, NULL);

    free(buf);
}

static void close_img_viewer_cb(lv_event_t *e) {
    mjpeg_playing = false;
    canvas_img = NULL;
    if (img_viewer) {
        lv_obj_delete(img_viewer);
        img_viewer = NULL;
    }
}

static void mjpeg_play_task(void *arg) {
    char *filepath = (char *)arg;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        free(filepath);
        mjpeg_playing = false;
        mjpeg_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t temp_buf_size = 64 * 1024;
    uint8_t *frame_buf = malloc(temp_buf_size);
    uint8_t *work_buf = malloc(3100);

    if (frame_buf && work_buf) {
        while (mjpeg_playing) {
            int c;
            // Find SOI marker (0xFFD8)
            while ((c = fgetc(f)) != EOF) {
                if (c == 0xFF) {
                    int next = fgetc(f);
                    if (next == 0xD8) {
                        break;
                    }
                }
            }
            if (feof(f)) {
                // End of file, loop video
                fseek(f, 0, SEEK_SET);
                continue;
            }

            uint32_t frame_len = 2;
            frame_buf[0] = 0xFF;
            frame_buf[1] = 0xD8;

            bool found_eoi = false;
            while (frame_len < temp_buf_size - 1 && mjpeg_playing) {
                int byte = fgetc(f);
                if (byte == EOF) break;
                frame_buf[frame_len++] = byte;
                if (byte == 0xD9 && frame_buf[frame_len - 2] == 0xFF) {
                    found_eoi = true;
                    break;
                }
            }

            if (found_eoi && mjpeg_playing && canvas_img != NULL) {
                JDEC jd;
                jpeg_decode_t dec = {
                    .data = frame_buf,
                    .len = frame_len,
                    .offset = 0,
                    .out_buf = (uint16_t *)mjpeg_canvas_buf,
                    .out_width = CAM_WIDTH,
                    .out_height = CAM_HEIGHT
                };

                if (jd_prepare(&jd, jpg_input_func, work_buf, 3100, &dec) == JDR_OK) {
                    jd_decomp(&jd, jpg_output_func, 0);
                    // Notify LVGL from background task using non-blocking lock to prevent stalls
                    if (mjpeg_playing && canvas_img != NULL) {
                        if (bsp_display_lock(0)) {
                            if (canvas_img != NULL) lv_obj_invalidate(canvas_img);
                            bsp_display_unlock();
                        }
                    }
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
    }

    free(frame_buf);
    free(work_buf);
    fclose(f);
    free(filepath);
    
    // Safety delay to guarantee UI thread completes the delete event
    vTaskDelay(pdMS_TO_TICKS(150));
    if (mjpeg_canvas_buf) {
        free(mjpeg_canvas_buf);
        mjpeg_canvas_buf = NULL;
    }
    
    mjpeg_playing = false;
    mjpeg_task_handle = NULL;
    vTaskDelete(NULL);
}

static void view_mjpeg_file(const char *filepath) {
    if (mjpeg_playing || wav_playing) {
        stop_file_explorer_media();
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    img_viewer = lv_obj_create(explorer_container);
    lv_obj_set_size(img_viewer, 410, 502);
    lv_obj_set_style_bg_color(img_viewer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(img_viewer, LV_OPA_COVER, 0);
    lv_obj_center(img_viewer);
    lv_obj_move_foreground(img_viewer);

    canvas_img = lv_canvas_create(img_viewer);
    mjpeg_canvas_buf = heap_caps_malloc(CAM_WIDTH * CAM_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    if (!mjpeg_canvas_buf) {
        lv_obj_delete(img_viewer);
        img_viewer = NULL;
        canvas_img = NULL;
        return;
    }
    memset(mjpeg_canvas_buf, 0, CAM_WIDTH * CAM_HEIGHT * 2);
    lv_canvas_set_buffer(canvas_img, mjpeg_canvas_buf, CAM_WIDTH, CAM_HEIGHT, LV_COLOR_FORMAT_RGB565);
    
    // Visually scale the 320x240 frame by 2.09x (536/256) to fill the screen bounds (height 502)
    lv_image_set_scale(canvas_img, 536); 
    lv_obj_center(canvas_img);

    lv_obj_t *btn_close = lv_button_create(img_viewer);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, close_img_viewer_cb, LV_EVENT_CLICKED, NULL);

    char *path_copy = strdup(filepath);
    mjpeg_playing = true;
    xTaskCreate(mjpeg_play_task, "mjpeg_play", 8192, path_copy, 5, &mjpeg_task_handle);
}

static void view_image_file(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *img_data = malloc(size);
    if (!img_data) {
        fclose(f);
        return;
    }
    fread(img_data, 1, size, f);
    fclose(f);

    img_viewer = lv_obj_create(explorer_container);
    lv_obj_set_size(img_viewer, 410, 502);
    lv_obj_set_style_bg_color(img_viewer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(img_viewer, LV_OPA_COVER, 0);
    lv_obj_center(img_viewer);
    lv_obj_move_foreground(img_viewer);

    // Automatically clean up dynamically allocated image source buffer when the viewer is closed
    lv_obj_add_event_cb(img_viewer, btn_delete_cb, LV_EVENT_DELETE, (void*)img_data);

    const char *ext = strrchr(filepath, '.');
    bool is_png = (ext && strcasecmp(ext, ".png") == 0);
    bool is_jpg = (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0));

    if (is_png || is_jpg) {
        // Safe allocation using calloc to prevent unitialized memory garbage/crashes in LVGL 9
        lv_image_dsc_t *img_dsc = calloc(1, sizeof(lv_image_dsc_t));
        if (img_dsc) {
            img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            img_dsc->header.cf = LV_COLOR_FORMAT_RAW;
            img_dsc->header.flags = 0;
            img_dsc->header.w = 0;
            img_dsc->header.h = 0;
            img_dsc->header.stride = 0;
            img_dsc->header.reserved_2 = 0;
            img_dsc->data_size = size;
            img_dsc->data = img_data;
            img_dsc->reserved = NULL;

            lv_obj_t *img = lv_image_create(img_viewer);
            lv_image_set_src(img, img_dsc);
            // Assuming this is a pycam photo (320x240), scale proportionally to fill entire watch face height
            lv_image_set_scale(img, 536);
            lv_obj_center(img);
            
            // Clean up the image descriptor structure when the image widget is deleted
            lv_obj_add_event_cb(img, btn_delete_cb, LV_EVENT_DELETE, (void*)img_dsc);
        }
    }

    lv_obj_t *btn_close = lv_button_create(img_viewer);
    lv_obj_set_size(btn_close, 100, 40);
    lv_obj_align(btn_close, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "Close");
    lv_obj_center(lbl_close);
    lv_obj_add_event_cb(btn_close, close_img_viewer_cb, LV_EVENT_CLICKED, NULL);
}

static void file_click_cb(lv_event_t *e) {
    char *path = (char*)lv_event_get_user_data(e);
    if (!path) return;

    if (strcmp(path, "..") == 0) {
        char *last_slash = strrchr(current_dir, '/');
        if (last_slash && last_slash != current_dir) {
            *last_slash = '\0';
        } else {
            strcpy(current_dir, "/sdcard");
        }
        refresh_directory_list(current_dir);
        return;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            strcpy(current_dir, path);
            refresh_directory_list(current_dir);
        } else {
            char *ext = strrchr(path, '.');
            if (ext) {
                if (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".txt") == 0) {
                    view_text_file(path);
                } else if (strcasecmp(ext, ".wav") == 0) {
                    play_wav_file(path);
                } else if (strcasecmp(ext, ".mjp") == 0 || strcasecmp(ext, ".mjpeg") == 0) {
                    view_mjpeg_file(path);
                } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
                           strcasecmp(ext, ".png") == 0) {
                    view_image_file(path);
                }
            }
        }
    }
}

static void refresh_directory_list(const char *path) {
    if (!file_list) return;
    lv_obj_clean(file_list);

    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return;
    }

    if (strcmp(path, "/sdcard") != 0) {
        lv_obj_t *btn = lv_list_add_button(file_list, LV_SYMBOL_DIRECTORY, ".. [Parent]");
        lv_obj_add_event_cb(btn, file_click_cb, LV_EVENT_CLICKED, (void*)"..");
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char full_path[300];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        const char *symbol = LV_SYMBOL_FILE;
        if (entry->d_type == DT_DIR) {
            symbol = LV_SYMBOL_DIRECTORY; 
        } else {
            char *ext = strrchr(entry->d_name, '.');
            if (ext) {
                if (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0) {
                    symbol = LV_SYMBOL_AUDIO;
                } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 || 
                           strcasecmp(ext, ".mjp") == 0 || strcasecmp(ext, ".mjpeg") == 0 ||
                           strcasecmp(ext, ".png") == 0) {
                    symbol = LV_SYMBOL_IMAGE;
                }
            }
        }

        lv_obj_t *btn = lv_list_add_button(file_list, symbol, entry->d_name);
        char *allocated_path = strdup(full_path);
        lv_obj_add_event_cb(btn, file_click_cb, LV_EVENT_CLICKED, (void*)allocated_path);
        lv_obj_add_event_cb(btn, btn_delete_cb, LV_EVENT_DELETE, (void*)allocated_path);
    }
    closedir(dir);
}

void start_file_explorer(void) {
    if (!mount_sd_card()) {
        ESP_LOGE(TAG, "Could not launch File Explorer - No SD Card found.");
        return;
    }

    if (explorer_container == NULL) {
        explorer_container = lv_obj_create(tile_tools);
        lv_obj_remove_style_all(explorer_container);
        lv_obj_set_size(explorer_container, 410, 502);
        lv_obj_set_style_bg_color(explorer_container, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(explorer_container, LV_OPA_COVER, 0);

        file_list = lv_list_create(explorer_container);
        lv_obj_set_size(file_list, 390, 440);
        lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(file_list, lv_color_black(), 0);
        lv_obj_set_style_text_color(file_list, lv_color_white(), 0);

        refresh_directory_list(current_dir);
    } else {
        lv_obj_remove_flag(explorer_container, LV_OBJ_FLAG_HIDDEN);
        refresh_directory_list(current_dir);
    }
}

void close_file_explorer(void) {
    if (explorer_container != NULL) {
        lv_obj_add_flag(explorer_container, LV_OBJ_FLAG_HIDDEN);
    }
}

void stop_file_explorer_media(void) {
    if (wav_playing) {
        wav_playing = false;
    }
    if (mjpeg_playing) {
        mjpeg_playing = false;
    }
}