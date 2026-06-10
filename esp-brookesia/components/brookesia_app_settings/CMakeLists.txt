# File: examples/ESP-IDF-v5.4.2/03_esp-brookesia/components/brookesia_app_settings/CMakeLists.txt

# Automatically fetch all .c files in the component folder (including gear.c)
file(GLOB SRCS "*.c")

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "."
                    REQUIRES espressif__esp-brookesia esp_wifi)
