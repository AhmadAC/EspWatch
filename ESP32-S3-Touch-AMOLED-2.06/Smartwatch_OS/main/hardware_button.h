// File: Smartwatch_OS/main/hardware_button.h
#ifndef HARDWARE_BUTTON_H
#define HARDWARE_BUTTON_H

#include <stdbool.h>

extern volatile bool is_screen_on;

void init_hardware_button(void);

#endif // HARDWARE_BUTTON_H