#pragma once

#include <stdint.h>
#include "virtio-queue.h"

#define MAX_WINDOW_WIDTH  640
#define MAX_WINDOW_HEIGHT 480

#define TEST_XRGB_WHITE 0xffffffff
#define TEST_XRGB_BLUE  0xff2200FF
#define TEST_XRGB_RED   0x0000ff00

struct surface_t {
    uint32_t resource_id;

    /* buffer size */
    uint32_t width;
    uint32_t height;

    // because we don't have malloc :(
    uint32_t backbuf[MAX_WINDOW_WIDTH * MAX_WINDOW_HEIGHT];
};

void surface_init(struct surface_t *surface, uint32_t buf_w, uint32_t buf_h);
void surface_display(struct surface_t *surface);
void surface_destroy(struct surface_t *surface);

typedef struct virtio_gpu_rect rect_t; // somehow it doesn't work...

struct vector {
    uint64_t x;
    uint64_t y;
};

void
surface_draw_circle(struct surface_t *resource, struct vector pos, uint64_t r, uint32_t color);

// SDL_FillRect
void
surface_fill_rect(struct surface_t *surface, const rect_t *rect, uint32_t color);