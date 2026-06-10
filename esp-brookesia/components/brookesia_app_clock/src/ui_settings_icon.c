#include "lvgl.h"

// Define colors (B, G, R, A order for standard little-endian systems)
#define T 0x00, 0x00, 0x00, 0x00  // Transparent
#define G 0xD0, 0xD0, 0xD0, 0xFF  // Metallic Grey

const uint8_t ui_settings_icon_map[] = {
    /* Row 0  */ T, T, T, T, T, T, T, G, G, T, T, T, T, T, T, T,
    /* Row 1  */ T, T, T, T, T, T, T, G, G, T, T, T, T, T, T, T,
    /* Row 2  */ T, T, G, T, T, T, T, G, G, T, T, T, T, G, T, T,
    /* Row 3  */ T, T, T, G, T, T, G, G, G, G, T, T, G, T, T, T,
    /* Row 4  */ T, T, T, T, G, G, G, G, G, G, G, G, T, T, T, T,
    /* Row 5  */ T, T, T, T, G, G, G, G, G, G, G, G, T, T, T, T,
    /* Row 6  */ T, T, T, G, G, G, T, T, T, T, G, G, G, T, T, T,
    /* Row 7  */ G, G, G, G, G, G, T, T, T, T, G, G, G, G, G, G,
    /* Row 8  */ G, G, G, G, G, G, T, T, T, T, G, G, G, G, G, G,
    /* Row 9  */ T, T, T, G, G, G, T, T, T, T, G, G, G, T, T, T,
    /* Row 10 */ T, T, T, T, G, G, G, G, G, G, G, G, T, T, T, T,
    /* Row 11 */ T, T, T, T, G, G, G, G, G, G, G, G, T, T, T, T,
    /* Row 12 */ T, T, T, G, T, T, G, G, G, G, T, T, G, T, T, T,
    /* Row 13 */ T, T, G, T, T, T, T, G, G, T, T, T, T, G, T, T,
    /* Row 14 */ T, T, T, T, T, T, T, G, G, T, T, T, T, T, T, T,
    /* Row 15 */ T, T, T, T, T, T, T, G, G, T, T, T, T, T, T, T
};

#undef T
#undef G

const lv_image_dsc_t ui_settings_icon = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = 16,
        .h = 16,
        .stride = 16 * 4, // 16 pixels * 4 bytes per pixel
    },
    .data_size = sizeof(ui_settings_icon_map),
    .data = ui_settings_icon_map
};