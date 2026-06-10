# File: Smartwatch_OS/apps/my_eez_ui/CMakeLists.txt
idf_component_register(
    SRCS "images.c"
         "screens.c"
         "styles.c"
         "ui.c"
         "ui_font_test.c"
         "ui_image_icon_clock.c"
         "ui_image_icon_gear.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)