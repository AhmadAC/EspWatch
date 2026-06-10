# File: esp-brookesia/components/brookesia_app_clock/CMakeLists.txt

# Automatically fetch all .c and .cpp files in the src folder and the component root
file(GLOB SRCS "src/*.cpp" "src/*.c" "*.c")

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "include" "."
    PRIV_REQUIRES brookesia_core lvgl esp_wifi esp_http_server driver nvs_flash esp_driver_tsens
)
