#pragma once

#include <stdint.h>

#define MAX_WINDOW_WIDTH  640
#define MAX_WINDOW_HEIGHT 480

struct surface_t {
    uint32_t resource_id;
    uint32_t pos_x;
    uint32_t pos_y;

    /* buffer size */
    uint32_t width;
    uint32_t height;

    // because we don't have malloc :(
    uint32_t backbuf[MAX_WINDOW_WIDTH * MAX_WINDOW_HEIGHT];
};

void surface_init(struct surface_t *surface, uint32_t buf_w, uint32_t buf_h);
void surface_display(struct surface_t *surface);
void surface_destroy(struct surface_t *surface);