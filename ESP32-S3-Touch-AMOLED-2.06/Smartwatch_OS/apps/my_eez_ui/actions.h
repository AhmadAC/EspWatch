#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_custom_action(lv_event_t * e);
extern void action_open_clock(lv_event_t * e);
extern void action_open_settings(lv_event_t * e);
extern void action_tools(lv_event_t * e);
extern void action_camera(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/