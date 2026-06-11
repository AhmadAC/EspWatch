#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *obj0;
    lv_obj_t *obj0__app_settings_1;
    lv_obj_t *obj0__app_settings_icon_1;
    lv_obj_t *obj0__label_settings_1;
    lv_obj_t *app_settings_2;
    lv_obj_t *app_settings_icon_2;
    lv_obj_t *label_settings_2;
    lv_obj_t *app_settings_3;
    lv_obj_t *app_cam_icon;
    lv_obj_t *label_cam;
    lv_obj_t *app_settings_4;
    lv_obj_t *app_tools_icon;
    lv_obj_t *label_spanner;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_user_widget_app_icon(lv_obj_t *parent_obj, int startWidgetIndex);
void tick_user_widget_app_icon(int startWidgetIndex);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/