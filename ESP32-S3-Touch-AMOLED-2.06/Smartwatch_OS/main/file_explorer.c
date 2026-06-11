#include "file_explorer.h"
#include "camera_recv.h"
#include "ui_app.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
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

static bool es8311_write_reg(uint8_t reg, uint8_t val) {
    i2c_master_dev_handle_t codec_handle = NULL;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x18, // ES8311
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev_cfg, &codec_handle) == ESP_OK) {
        uint8_t data[2] = { reg, val };
        esp_err_t err = i2c_master_transmit(codec_handle, data, 2, -1);
        i2c_master_bus_rm_device(codec_handle);
        return err == ESP_OK;
    }
    return false;
}

void init_es8311_codec(void) {
    es8311_write_reg(0x00, 0x1F); // Reset
    vTaskDelay(pdMS_TO_TICKS(10));
    es8311_write_reg(0x00, 0x00);
    es8311_write_reg(0x00, 0x80); // Power on
    es8311_write_reg(0x0D, 0x01); // Power up analog
    es8311_write_reg(0x0E, 0x02);
    es8311_write_reg(0x12, 0x00); // DAC power up
    es8311_write_reg(0x13, 0x10); // HP drive
    es8311_write_reg(0x32, 0xD9); // Set volume (85%)
}

void init_i2s_audio(uint32_t sample_rate) {
    if (tx_chan != NULL) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan);
        tx_chan = NULL;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
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
    slot_config.gpio_cs = GPIO_NUM_17; // Corrected board layout CS pin mapping
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

void play_wav_file(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open audio file: %s", filepath);
        return;
    }

    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        fclose(f);
        return;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        ESP_LOGE(TAG, "Not a valid WAV file");
        return;
    }

    uint32_t sample_rate = *(uint32_t*)(header + 24);
    uint16_t num_channels = *(uint16_t*)(header + 22);
    uint16_t bits_per_sample = *(uint16_t*)(header + 34);

    ESP_LOGI(TAG, "WAV info: rate=%lu, channels=%u, bits=%u", sample_rate, num_channels, bits_per_sample);

    init_es8311_codec();
    init_i2s_audio(sample_rate);

    size_t chunk_size = 4096;
    uint8_t *buf = malloc(chunk_size);
    if (!buf) {
        fclose(f);
        return;
    }

    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, chunk_size, f)) > 0) {
        size_t bytes_written = 0;
        i2s_channel_write(tx_chan, buf, bytes_read, &bytes_written, portMAX_DELAY);
    }

    free(buf);
    fclose(f);

    gpio_set_level(GPIO_NUM_46, 0);
    ESP_LOGI(TAG, "Audio playback completed");
}

static void btn_delete_cb(lv_event_t *e) {
    char *path = (char*)lv_event_get_user_data(e);
    if (path && strcmp(path, "..") != 0) {
        free(path);
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
    lv_obj_move_to_foreground(text_viewer);

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
    if (img_viewer) {
        lv_obj_delete(img_viewer);
        img_viewer = NULL;
    }
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
    lv_obj_move_to_foreground(img_viewer);

    // Automatically clean up dynamically allocated image source buffer when the viewer is closed
    lv_obj_add_event_cb(img_viewer, btn_delete_cb, LV_EVENT_DELETE, (void*)img_data);

    const char *ext = strrchr(filepath, '.');
    bool is_png = (ext && strcasecmp(ext, ".png") == 0);

    if (is_png) {
        // PNG Viewing: using LVGL's lodepng decoder from a memory buffer
        lv_image_dsc_t *img_dsc = malloc(sizeof(lv_image_dsc_t));
        if (img_dsc) {
            img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            img_dsc->header.cf = LV_COLOR_FORMAT_RAW;
            img_dsc->header.w = 0;
            img_dsc->header.h = 0;
            img_dsc->header.stride = 0;
            img_dsc->data_size = size;
            img_dsc->data = img_data;
            img_dsc->reserved = NULL;

            lv_obj_t *img = lv_image_create(img_viewer);
            lv_image_set_src(img, img_dsc);
            lv_obj_center(img);
            
            // Clean up the image descriptor structure when the image widget is deleted
            lv_obj_add_event_cb(img, btn_delete_cb, LV_EVENT_DELETE, (void*)img_dsc);
        }
    } else {
        // JPEG/MJPEG Viewing: using ROM-based tjpgd to decode into canvas
        JDEC jd;
        jpeg_decode_t dec = {
            .data = img_data,
            .len = size,
            .offset = 0,
            .out_buf = NULL,
            .out_width = 0
        };

        uint8_t *work_buf = malloc(3100);
        if (work_buf) {
            if (jd_prepare(&jd, jpg_input_func, work_buf, 3100, &dec) == JDR_OK) {
                // Dynamically read actual image dimensions
                uint16_t img_w = jd.width;
                uint16_t img_h = jd.height;
                
                uint8_t *temp_canvas_buf = heap_caps_malloc(img_w * img_h * 2, MALLOC_CAP_SPIRAM);
                if (temp_canvas_buf) {
                    dec.out_buf = (uint16_t *)temp_canvas_buf;
                    dec.out_width = img_w;
                    
                    if (jd_decomp(&jd, jpg_output_func, 0) == JDR_OK) {
                        lv_obj_t *canvas_img = lv_canvas_create(img_viewer);
                        lv_canvas_set_buffer(canvas_img, temp_canvas_buf, img_w, img_h, LV_COLOR_FORMAT_RGB565);
                        lv_obj_center(canvas_img);
                        
                        // Clean up temporary canvas frame buffer when the canvas widget is deleted
                        lv_obj_add_event_cb(canvas_img, btn_delete_cb, LV_EVENT_DELETE, (void*)temp_canvas_buf);
                    } else {
                        free(temp_canvas_buf);
                    }
                }
            }
            free(work_buf);
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
                } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
                           strcasecmp(ext, ".mjp") == 0 || strcasecmp(ext, ".mjpeg") == 0 ||
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
        // Parented directly to tile_tools to prevent shifts and maintain AMOLED black backdrop
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