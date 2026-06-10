// File: Smartwatch_OS/main/wifi_app.c
#include "wifi_app.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_eap_client.h"

static const char *TAG = "WiFi_App";

static bool s_is_ap_mode = false;
static bool s_is_connected = false;
static bool s_wifi_enabled = true; // Tracks driver level state
static int s_reboot_timer = -1;
static httpd_handle_t server = NULL;

#define NVS_NAMESPACE "smartwatch"
#define NVS_KEY_IDENTITY "wifi_ent_id"
#define NVS_KEY_TZ "wifi_tz"

// Getters for UI
bool get_wifi_connected_status(void) { return s_is_connected; }
int get_reboot_timer(void) { return s_reboot_timer; }
void decrement_reboot_timer(void) { if (s_reboot_timer > 0) s_reboot_timer--; }

/* ==========================================
 * NVS STORAGE
 * ========================================== */
static void save_tz_env(const char *tz_env) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, NVS_KEY_TZ, tz_env);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static void save_ent_identity(const char *identity) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_str(my_handle, NVS_KEY_IDENTITY, identity);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

static void load_ent_identity(char *identity, size_t max_len) {
    nvs_handle_t my_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle) == ESP_OK) {
        size_t required_size = max_len;
        nvs_get_str(my_handle, "wifi_ent_id", identity, &required_size);
        nvs_close(my_handle);
    } else {
        identity[0] = '\0';
    }
}

/* ==========================================
 * URL DECODER UTILITY
 * ========================================== */
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a'-'A'; else if (a >= 'A') a -= ('A' - 10); else a -= '0';
            if (b >= 'a') b -= 'a'-'A'; else if (b >= 'A') b -= ('A' - 10); else b -= '0';
            *dst++ = 16*a+b; src+=3;
        } else if (*src == '+') {
            *dst++ = ' '; src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ==========================================
 * HTTP WEB SERVER (CAPTIVE PORTAL)
 * ========================================== */
static void scan_networks(void) {
    ESP_LOGI(TAG, "Initiating network scan...");
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_scan_config_t scan_config = { .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_wifi_scan_start(&scan_config, true);
}

static esp_err_t http_get_handler(httpd_req_t *req) {
    scan_networks(); // Trigger fresh scan on page load

    httpd_req_t * r = req; // Silent unused warning
    (void)r;

    httpd_resp_send_chunk(req, "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>", -1);
    httpd_resp_send_chunk(req, "<style>body{background:#111;color:#fff;font-family:sans-serif;padding:20px;text-align:center;} select,input[type=text],input[type=password]{box-sizing:border-box;width:100%;padding:10px;margin:10px 0;border-radius:5px;background:#222;color:#fff;border:1px solid #444;} label{font-weight:bold;font-size:14px;}</style></head><body>", -1);
    httpd_resp_send_chunk(req, "<h2>Smartwatch Setup</h2><form action='/save' method='POST' onsubmit='saveToLocal(); document.getElementById(\"time\").value=Math.floor(Date.now()/1000); document.getElementById(\"tz_offset\").value=new Date().getTimezoneOffset();' style='display:inline-block;text-align:left;width:100%;max-width:300px;'>", -1);
    httpd_resp_send_chunk(req, "<label>Select Network:</label><br><select name='ssid' id='ssid' style='width:100%;padding:10px;margin:10px 0;border-radius:5px;background:#222;color:#fff;border:1px solid #444;'>", -1);
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 0) {
        wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records != NULL && esp_wifi_scan_get_ap_records(&ap_count, ap_records) == ESP_OK) {
            for(int i = 0; i < ap_count; i++) {
                if (strlen((char*)ap_records[i].ssid) == 0) continue;
                char opt[128];
                snprintf(opt, sizeof(opt), "<option value=\"%s\">%s</option>", (char*)ap_records[i].ssid, (char*)ap_records[i].ssid);
                httpd_resp_send_chunk(req, opt, -1);
            }
        }
        free(ap_records);
    } else {
        httpd_resp_send_chunk(req, "<option value=''>No networks found...</option>", -1);
    }
    
    httpd_resp_send_chunk(req, "</select><br>", -1);
    httpd_resp_send_chunk(req, "<label>Identity / Username (Enterprise Wi-Fi):</label><br><input type='text' name='identity' id='identity' placeholder='Optional (e.g. school username)'><br>", -1);
    httpd_resp_send_chunk(req, "<label>Password:</label><br><input type='password' id='pass' name='pass'><br>", -1);
    httpd_resp_send_chunk(req, "<div style='margin-bottom:15px;'><input type='checkbox' id='show_pass' onclick='togglePass()'> <label for='show_pass' style='font-size:12px;color:#aaa;'>Show Password</label></div>", -1);
    httpd_resp_send_chunk(req, "<input type='hidden' id='time' name='time' value=''>", -1);
    httpd_resp_send_chunk(req, "<input type='hidden' id='tz_offset' name='tz_offset' value=''>", -1);
    httpd_resp_send_chunk(req, "<input type='submit' value='Save & Connect' style='width:100%;padding:15px;margin-top:10px;background:#00bfff;color:#000;border:none;border-radius:5px;font-weight:bold;font-size:16px;cursor:pointer;'>", -1);
    httpd_resp_send_chunk(req, "</form><script>function togglePass(){var x=document.getElementById(\"pass\"); if(x.type===\"password\"){x.type=\"text\";}else{x.type=\"password\";}} function saveToLocal(){localStorage.setItem(\"ssid\", document.getElementById(\"ssid\").value); localStorage.setItem(\"identity\", document.getElementById(\"identity\").value); localStorage.setItem(\"pass\", document.getElementById(\"pass\").value);}</script></body></html>", -1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t http_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if(ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char ssid[64] = {0}, pass[64] = {0}, identity[64] = {0}, time_str[32] = {0}, tz_str[32] = {0};
    char *pair = strtok(buf, "&");
    while(pair != NULL) {
        char *eq = strchr(pair, '=');
        if(eq) {
            *eq = '\0';
            char decoded_val[64] = {0};
            url_decode(decoded_val, eq + 1);
            if(strcmp(pair, "ssid") == 0) strncpy(ssid, decoded_val, sizeof(ssid)-1);
            else if(strcmp(pair, "pass") == 0) strncpy(pass, decoded_val, sizeof(pass)-1);
            else if(strcmp(pair, "identity") == 0) strncpy(identity, decoded_val, sizeof(identity)-1);
            else if(strcmp(pair, "time") == 0) strncpy(time_str, decoded_val, sizeof(time_str)-1);
            else if(strcmp(pair, "tz_offset") == 0) strncpy(tz_str, decoded_val, sizeof(tz_str)-1);
        }
        pair = strtok(NULL, "&");
    }
    
    // Sync Time
    long timestamp = atol(time_str);
    long tz_offset = atol(tz_str);
    if(timestamp > 1000000000) {
        char tz_env_str[32];
        snprintf(tz_env_str, sizeof(tz_env_str), "GMT%d:%02d", (int)(tz_offset / 60), (int)(abs(tz_offset) % 60));
        setenv("TZ", tz_env_str, 1);
        tzset();
        save_tz_env(tz_env_str);

        struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "Time synced from phone: %ld, TZ: %s", timestamp, tz_env_str);
        
        // Immediately update external RTC
        sync_external_rtc_time();
    }
    
    // Save Config
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password)-1);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH); // Secure flash write path
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    save_ent_identity(identity);
    
    httpd_resp_sendstr(req, "<!DOCTYPE html><html><body style='background:#111;color:#fff;text-align:center;padding-top:50px;'><h2 style='color:#00bfff;'>Saved! Rebooting...</h2></body></html>");
    s_reboot_timer = 3;
    return ESP_OK;
}

/* ==========================================
 * WIFI CORE INIT
 * ========================================== */
static void sntp_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP synchronized successfully. Syncing time to hardware PCF85063 RTC.");
    sync_external_rtc_time();
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    void *a = arg; (void)a; // Silent unused warning
    void *d = event_data; (void)d;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        char identity[64] = {0};
        load_ent_identity(identity, sizeof(identity));
        if (strlen(identity) > 0) {
            wifi_config_t sta_config;
            esp_wifi_get_config(WIFI_IF_STA, &sta_config);
            esp_wifi_sta_enterprise_enable();
            esp_eap_client_set_identity((uint8_t *)identity, strlen(identity));
            esp_eap_client_set_username((uint8_t *)identity, strlen(identity));
            esp_eap_client_set_password((uint8_t *)sta_config.sta.password, strlen((char*)sta_config.sta.password));
        } else {
            esp_wifi_sta_enterprise_disable();
        }
        if (!s_is_ap_mode && s_wifi_enabled) {
            wifi_config_t conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && strlen((char*)conf.sta.ssid) > 0) {
                ESP_LOGI(TAG, "Connecting to saved network: %s...", (char*)conf.sta.ssid);
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "No Wi-Fi credentials saved in NVS yet.");
            }
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_is_connected = false;
        if (!s_is_ap_mode && s_wifi_enabled) {
            wifi_config_t conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && strlen((char*)conf.sta.ssid) > 0) {
                esp_wifi_connect();
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_is_connected = true;
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        config.sync_cb = sntp_sync_notification_cb; // Register callback on successful SNTP sync
        esp_netif_sntp_init(&config);
    }
}

void init_wifi(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    // Force Flash mode on start up to keep local storage synchronized across system resets
    esp_wifi_set_storage(WIFI_STORAGE_FLASH); 

    // Diagnostics print
    wifi_config_t stored_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &stored_config) == ESP_OK) {
        ESP_LOGI(TAG, "Stored SSID in NVS on boot: '%s'", (char*)stored_config.sta.ssid);
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

static void ap_task_runner(void *arg) {
    s_is_ap_mode = true;
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_config_t ap_config = {
        .ap = { .ssid = "Smartwatch_AP", .ssid_len = 13, .channel = 1, .password = "12345678", .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK }
    };
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(500));
    scan_networks();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = http_get_handler };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_post = { .uri = "/save", .method = HTTP_POST, .handler = http_post_handler };
        httpd_register_uri_handler(server, &uri_post);
    }
    vTaskDelete(NULL);
}

void start_ap_mode_task(void) {
    xTaskCreate(ap_task_runner, "ap_task", 4096, NULL, 5, NULL);
}

// Wi-Fi enable/disable controls
bool is_wifi_enabled(void) {
    return s_wifi_enabled;
}

void toggle_wifi(void) {
    if (s_wifi_enabled) {
        esp_err_t err = esp_wifi_stop();
        if (err == ESP_OK) {
            s_wifi_enabled = false;
            s_is_connected = false;
            ESP_LOGI(TAG, "Wi-Fi Transceiver Disabled Successfully");
        } else {
            ESP_LOGE(TAG, "Failed to disable Wi-Fi: %d", err);
        }
    } else {
        esp_err_t err = esp_wifi_start();
        if (err == ESP_OK) {
            s_wifi_enabled = true;
            ESP_LOGI(TAG, "Wi-Fi Transceiver Enabled Successfully");
        } else {
            ESP_LOGE(TAG, "Failed to enable Wi-Fi: %d", err);
        }
    }
}