#pragma once

#include <stdint.h>

#define MAX_WINDOW_WIDTH  640
#define MAX_WINDOW_HEIGHT 480

struct texture_2d {
    uint32_t resource_id;
    uint32_t pos_x;
    uint32_t pos_y;

    /* buffer size */
    uint32_t width;
    uint32_t height;

    // because we don't have malloc :(
    uint32_t backbuf[MAX_WINDOW_WIDTH * MAX_WINDOW_HEIGHT];
};

void texture_init(struct texture_2d *texture, uint32_t buf_w, uint32_t buf_h);
void texture_display(struct texture_2d *texture);
void texture_destroy(struct texture_2d *texture);