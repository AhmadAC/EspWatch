#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"        
#include "jpeg_decoder.h" // This now safely targets the esp_codec_dev library already in your project
#include <time.h>
#include <sys/time.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "ui_app.h"
#include "wifi_app.h"

// Include your EEZ Studio generated screen headers to access the global "objects" struct
#include "screens.h"

static const char *TAG = "SmartwatchOS";
static const char *NET_TAG = "Watch_Receiver";

#define WIFI_CHANNEL 1
#define PACKET_MAGIC 0xCAFEBABE
#define MAX_JPEG_SIZE (64 * 1024) 
#define CHUNK_SIZE 1000

// ==============================================================================
// Protocol Buffers & Structs
// ==============================================================================
typedef struct {
    uint8_t frame_control[2];
    uint8_t duration[2];
    uint8_t dest_addr[6];
    uint8_t src_addr[6];
    uint8_t bss_id[6];
    uint8_t seq_control[2];
} __attribute__((packed)) wifi_80211_header_t;

typedef struct {
    uint32_t magic;
    uint16_t frame_id;
    uint16_t chunk_idx;
    uint16_t total_chunks;
    uint16_t length;
} __attribute__((packed)) stream_payload_header_t;

typedef struct {
    char message[16];
    uint8_t mac_addr[6];
} __attribute__((packed)) pairing_packet_t;

static uint8_t *jpeg_assembly_buf = NULL;
static uint8_t *decoded_rgb565_buf = NULL; 
static volatile uint32_t assembled_jpeg_len = 0;
static volatile bool new_frame_ready = false;
static uint16_t last_frame_id = 9999;

static uint8_t robot_mac[6] = {0};
static volatile bool is_paired = false;

// Dynamic LVGL Image Descriptor
static lv_img_dsc_t dynamic_camera_dsc = {
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = 320,  
    .header.h = 240,  
    .data_size = 320 * 240 * 2,
    .data = NULL
};

// ==============================================================================
// Wi-Fi Raw Packet Callback
// ==============================================================================
static void wifi_promiscuous_rx_callback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint32_t len = pkt->rx_ctrl.sig_len;
    uint8_t *payload = pkt->payload;

    if (len < (sizeof(wifi_80211_header_t) + sizeof(stream_payload_header_t))) return;

    stream_payload_header_t *stream_hdr = (stream_payload_header_t *)(payload + sizeof(wifi_80211_header_t));

    if (stream_hdr->magic == PACKET_MAGIC) {
        uint8_t *chunk_data = payload + sizeof(wifi_80211_header_t) + sizeof(stream_payload_header_t);
        
        if (stream_hdr->chunk_idx == 0) {
            assembled_jpeg_len = 0;
            last_frame_id = stream_hdr->frame_id;
        }

        if (stream_hdr->frame_id != last_frame_id) return;

        uint32_t offset = stream_hdr->chunk_idx * CHUNK_SIZE;
        if (offset + stream_hdr->length < MAX_JPEG_SIZE) {
            memcpy(jpeg_assembly_buf + offset, chunk_data, stream_hdr->length);
            
            if (stream_hdr->chunk_idx == stream_hdr->total_chunks - 1) {
                assembled_jpeg_len = offset + stream_hdr->length;
                new_frame_ready = true;
            }
        }
    }
}

// ==============================================================================
// ESP-NOW Receiver Callback
// ==============================================================================
static void on_esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len == sizeof(pairing_packet_t)) {
        pairing_packet_t *pkt = (pairing_packet_t *)data;
        if (strcmp(pkt->message, "ROBOT_PAIR_REQ") == 0) {
            memcpy(robot_mac, pkt->mac_addr, 6);
            
            pairing_packet_t reply;
            strcpy(reply.message, "WATCH_PAIR_ACK");
            esp_read_mac(reply.mac_addr, ESP_MAC_WIFI_STA);

            esp_now_peer_info_t peer_info = {0};
            memcpy(peer_info.peer_addr, robot_mac, 6);
            peer_info.channel = WIFI_CHANNEL;
            peer_info.encrypt = false;
            
            esp_now_add_peer(&peer_info);
            esp_now_send(robot_mac, (uint8_t *)&reply, sizeof(reply));
            
            is_paired = true;
            ESP_LOGI(NET_TAG, "ESP-NOW Handshake completed. Registered robot MAC.");
        }
    }
}

// ==============================================================================
// Asynchronous Core 0 Streaming Task (Decodes video in background)
// ==============================================================================
static void video_stream_processing_task(void *pvParameters) {
    jpeg_assembly_buf = (uint8_t *)heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
    decoded_rgb565_buf = (uint8_t *)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);

    if (!jpeg_assembly_buf || !decoded_rgb565_buf) {
        ESP_LOGE(NET_TAG, "Failed to allocate framebuffers in PSRAM");
        vTaskDelete(NULL);
    }

    dynamic_camera_dsc.data = decoded_rgb565_buf;

    ESP_LOGI(NET_TAG, "Listening for ESP-NOW Pairing Handshake...");
    while (!is_paired) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_now_unregister_recv_cb();
    esp_now_deinit();

    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous_rx_callback);
    esp_wifi_set_promiscuous(true);
    ESP_LOGI(NET_TAG, "Promiscuous mode enabled. Collecting frame chunks...");

    while (1) {
        if (new_frame_ready) {
            
            // Explicitly define struct parameters for esp_codec_dev API
            jpeg_dec_config_t config = {
                .output_type = JPEG_RAW_TYPE_RGB565_LE,
                .rotate = JPEG_ROTATE_0
            };
            
            jpeg_dec_handle_t dec_handle = jpeg_dec_open(&config);
            if (dec_handle != NULL) {
                
                // Exact struct definition inside esp_codec_dev/include/jpeg_decoder.h
                jpeg_dec_io_t io = {
                    .inbuf = jpeg_assembly_buf,
                    .inbuf_len = assembled_jpeg_len,
                    .outbuf = decoded_rgb565_buf
                };
                
                jpeg_dec_header_info_t header_info;
                if (jpeg_dec_parse_header(dec_handle, &io, &header_info) == ESP_OK) {
                    jpeg_dec_process(dec_handle, &io);
                }
                jpeg_dec_close(dec_handle);

                // Lock BSP display mutex before updating LVGL
                if (bsp_display_lock(portMAX_DELAY)) {
                    if (objects.app_cam_icon != NULL) {
                        lv_img_set_src(objects.app_cam_icon, &dynamic_camera_dsc);
                        lv_obj_invalidate(objects.app_cam_icon);
                    }
                    bsp_display_unlock();
                }
            }
            new_frame_ready = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==============================================================================
// Streaming Wi-Fi Initialization
// ==============================================================================
static void init_streaming_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_esp_now_recv_cb));
}

// ==============================================================================
// Watch OS Entry Point
// ==============================================================================
void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    time_t now;
    time(&now);
    if (now < 1767225600) {
        struct timeval tv_time = { .tv_sec = 1775688660 };
        settimeofday(&tv_time, NULL);
        ESP_LOGI(TAG, "Initializing to fallback: Jun 8, 2026 15:51:00 PDT");
    }

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

    ESP_LOGI(TAG, "Initializing Board Support Package...");
    if (bsp_display_start() != NULL) {
        bsp_display_backlight_on();
    }

    bsp_display_lock(1000);
    build_ui();
    bsp_display_unlock();

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    init_streaming_wifi();
    
    xTaskCreatePinnedToCore(video_stream_processing_task, "stream_task", 6144, NULL, 5, NULL, 0);
}
