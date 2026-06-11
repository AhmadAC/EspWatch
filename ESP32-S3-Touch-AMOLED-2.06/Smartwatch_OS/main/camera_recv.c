#include "camera_recv.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h> // Fixed missing gettimeofday and struct timeval

static const char *TAG = "CAMERA_RECV";

uint8_t *canvas_buffer = NULL;
volatile bool new_frame_ready = false;
uint8_t *latest_frame_buffer = NULL;
uint32_t latest_frame_len = 0;

static uint8_t *frame_reassembly_buf = NULL;
static uint16_t expected_chunks = 0;
static uint16_t chunks_received = 0;
static bool *received_chunks_map = NULL;

static uint8_t camera_mac[6] = {0};
static bool camera_connected = false;
static uint8_t watch_mac[6] = {0};
static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Fixed strict pointer typing to match JDEC
unsigned int jpg_input_func(JDEC *jd, uint8_t *buf, unsigned int num) {
    jpeg_decode_t *dec = (jpeg_decode_t *)jd->device;
    if (dec->offset + num > dec->len) {
        num = dec->len - dec->offset;
    }
    if (buf) {
        memcpy(buf, dec->data + dec->offset, num);
    }
    dec->offset += num;
    return num;
}

// Fixed strict pointer typing to match JDEC
unsigned int jpg_output_func(JDEC *jd, void *bitmap, JRECT *rect) {
    jpeg_decode_t *dec = (jpeg_decode_t *)jd->device;
    uint16_t *out = dec->out_buf;
    uint16_t *in = (uint16_t *)bitmap;
    int width = rect->right - rect->left + 1;
    int height = rect->bottom - rect->top + 1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int out_x = rect->left + x;
            int out_y = rect->top + y;
            if (out_x < dec->out_width) {
                uint16_t pixel = in[y * width + x];
                pixel = (pixel << 8) | (pixel >> 8);
                out[out_y * dec->out_width + out_x] = pixel;
            }
        }
    }
    return 1;
}

static void handle_incoming_chunk(uint16_t idx, uint16_t total, uint16_t len, uint8_t *data) {
    if (!frame_reassembly_buf) {
        frame_reassembly_buf = heap_caps_malloc(128 * 1024, MALLOC_CAP_SPIRAM);
        received_chunks_map = calloc(100, sizeof(bool));
        latest_frame_buffer = heap_caps_malloc(128 * 1024, MALLOC_CAP_SPIRAM);
        canvas_buffer = heap_caps_malloc(CAM_WIDTH * CAM_HEIGHT * 2, MALLOC_CAP_SPIRAM);
    }

    if (total != expected_chunks || idx >= total) {
        expected_chunks = total;
        chunks_received = 0;
        memset(received_chunks_map, 0, 100 * sizeof(bool));
    }

    if (!received_chunks_map[idx]) {
        received_chunks_map[idx] = true;
        chunks_received++;

        uint32_t offset = idx * 1300;
        if (offset + len <= 128 * 1024) {
            memcpy(frame_reassembly_buf + offset, data, len);
        }

        if (chunks_received == total) {
            uint32_t total_len = (total - 1) * 1300 + len;
            memcpy(latest_frame_buffer, frame_reassembly_buf, total_len);
            latest_frame_len = total_len;
            new_frame_ready = true;
        }
    }
}

static void wifi_promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *payload = pkt->payload;
    uint32_t len = pkt->rx_ctrl.sig_len;

    if (len < 33) return;

    uint16_t fc;
    memcpy(&fc, payload, 2);
    if (fc != 0x0008) return; 

    if (memcmp(payload + 4, watch_mac, 6) != 0 && memcmp(payload + 4, broadcast_mac, 6) != 0) return;

    uint8_t *custom = payload + 24;
    if (custom[0] == 'C' && custom[1] == 'A' && custom[2] == 'M') {
        uint16_t chunk_idx;
        uint16_t total_chunks;
        uint16_t chunk_len;
        memcpy(&chunk_idx, custom + 3, 2);
        memcpy(&total_chunks, custom + 5, 2);
        memcpy(&chunk_len, custom + 7, 2);

        handle_incoming_chunk(chunk_idx, total_chunks, chunk_len, custom + 9);
    }
}

static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len >= 9 && strncmp((const char*)data, "pyCAM_ACK", 9) == 0) {
        ESP_LOGI(TAG, "Seeed Studio Camera acknowledged our connection!");
        memcpy(camera_mac, recv_info->src_addr, 6);
        camera_connected = true;

        esp_now_peer_info_t peer_info = {0};
        memcpy(peer_info.peer_addr, camera_mac, 6);
        peer_info.channel = 1;
        peer_info.encrypt = false;
        if (!esp_now_is_peer_exist(camera_mac)) {
            esp_now_add_peer(&peer_info);
        }

        const char *stream_start_msg = "pyCAM_STR_1";
        esp_now_send(camera_mac, (uint8_t *)stream_start_msg, strlen(stream_start_msg));
    }
}

void start_camera_stream(void) {
    ESP_LOGI(TAG, "Starting camera stream...");
    
    esp_wifi_disconnect();
    esp_wifi_get_mac(WIFI_IF_STA, watch_mac);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_cb);

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = 1;
    peer_info.encrypt = false;
    if (!esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_add_peer(&peer_info);
    }

    const char *discover_msg = "pyCAR_DISCOVER";
    esp_now_send(broadcast_mac, (uint8_t *)discover_msg, strlen(discover_msg));

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_rx_cb);
    esp_wifi_set_promiscuous(true);
}

void stop_camera_stream(void) {
    ESP_LOGI(TAG, "Stopping camera stream...");
    esp_wifi_set_promiscuous(false);
    if (camera_connected) {
        const char *stream_stop_msg = "pyCAM_STR_0";
        esp_now_send(camera_mac, (uint8_t *)stream_stop_msg, strlen(stream_stop_msg));
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
}

void save_photo_to_sd(void) {
    if (latest_frame_buffer == NULL || latest_frame_len == 0) {
        ESP_LOGE(TAG, "No frame to save");
        return;
    }

    char filepath[64];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(filepath, sizeof(filepath), "/sdcard/photo_%ld.jpg", tv.tv_sec);

    FILE *f = fopen(filepath, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        return;
    }
    fwrite(latest_frame_buffer, 1, latest_frame_len, f);
    fclose(f);
    ESP_LOGI(TAG, "Saved photo to: %s", filepath);
}