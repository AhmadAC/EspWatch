#ifndef FILE_EXPLORER_H
#define FILE_EXPLORER_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

void init_es8311_codec(void);
void init_i2s_audio(uint32_t sample_rate);
bool mount_sd_card(void);
void play_wav_file(const char *filepath);
void start_file_explorer(void);
void close_file_explorer(void);

#endif // FILE_EXPLORER_H