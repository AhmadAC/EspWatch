#ifndef CAMERA_RECV_H
#define CAMERA_RECV_H

#include <stdbool.h>
#include <stdint.h>
#include "rom/tjpgd.h"

#define CAM_WIDTH  320
#define CAM_HEIGHT 240

extern uint8_t *canvas_buffer;
extern volatile bool new_frame_ready;
extern uint8_t *latest_frame_buffer;
extern uint32_t latest_frame_len;

void start_camera_stream(void);
void stop_camera_stream(void);
void save_photo_to_sd(void);
bool is_camera_connected(void);

typedef struct {
    const uint8_t *data;
    uint32_t len;
    uint32_t offset;
    uint16_t *out_buf;
    uint32_t out_width;
} jpeg_decode_t;

unsigned int jpg_input_func(JDEC *jd, uint8_t *buf, unsigned int num);
unsigned int jpg_output_func(JDEC *jd, void *bitmap, JRECT *rect);

#endif // CAMERA_RECV_H