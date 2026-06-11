// File: Smartwatch_OS/main/ui_app.h
#ifndef UI_APP_H
#define UI_APP_H

#include "lvgl.h"

extern lv_obj_t * tile_tools;
extern lv_obj_t * canvas;

void build_ui(void);
void trigger_return_home(void);

#endif // UI_APP_H