# START OF FILE: Smartwatch_OS/main/CMakeLists.txt
idf_component_register(SRCS "main.c"
                            "ui_app.c"
                            "wifi_app.c"
                            "battery.c"
                            "rtc_clock.c"
                            "hardware_button.c"
                            "camera_recv.c"
                            "file_explorer.c"
                            "../fonts/arial_160.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp_lvgl_port esp_timer esp_http_server wpa_supplicant my_eez_ui fatfs)