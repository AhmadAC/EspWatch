#include "lvgl.h"

#define T 0x00, 0x00, 0x00, 0x00  // Transparent background
#define G 0xD5, 0xD5, 0xD5, 0xFF  // Clean Silver White

const uint8_t ui_clock_icon_map[] = {
    /* Row 0  */ T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,
    /* Row 1  */ T, T, T, T, T, G, G, G, G, G, G, T, T, T, T, T,
    /* Row 2  */ T, T, T, G, G, T, T, T, T, T, T, G, G, T, T, T,
    /* Row 3  */ T, T, G, T, T, T, T, G, T, T, T, T, T, G, T, T,
    /* Row 4  */ T, T, G, T, T, T, T, G, T, T, T, T, T, G, T, T,
    /* Row 5  */ T, G, T, T, T, T, T, G, T, T, T, T, T, T, G, T,
    /* Row 6  */ T, G, T, T, T, T, T, G, T, T, T, T, T, T, G, T,
    /* Row 7  */ T, G, T, T, T, T, T, G, G, G, G, G, T, T, G, T, // Hour hand right, Minute hand up
    /* Row 8  */ T, G, T, T, T, T, T, T, T, T, T, T, T, T, G, T,
    /* Row 9  */ T, G, T, T, T, T, T, T, T, T, T, T, T, T, G, T,
    /* Row 10 */ T, G, T, T, T, T, T, T, T, T, T, T, T, T, G, T,
    /* Row 11 */ T, T, G, T, T, T, T, T, T, T, T, T, T, G, T, T,
    /* Row 12 */ T, T, G, T, T, T, T, T, T, T, T, T, T, G, T, T,
    /* Row 13 */ T, T, T, G, G, T, T, T, T, T, T, G, G, T, T, T,
    /* Row 14 */ T, T, T, T, T, G, G, G, G, G, G, T, T, T, T, T,
    /* Row 15 */ T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T
};

#undef T
#undef G

const lv_image_dsc_t ui_clock_icon = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = 16,
        .h = 16,
        .stride = 16 * 4,
    },
    .data_size = sizeof(ui_clock_icon_map),
    .data = ui_clock_icon_map
};