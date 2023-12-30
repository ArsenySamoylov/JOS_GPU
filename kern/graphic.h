#pragma once

#include <stdint.h>
#include <inc/assert.h>

#include "virtio-gpu.h"
#include "virtio-queue.h"
#include "font.h"

#define MAX_WINDOW_WIDTH  640
#define MAX_WINDOW_HEIGHT 480

#define TEST_XRGB_WHITE     0xffffffff
#define TEST_XRGB_BLUE      0xff2200FF
#define TEST_XRGB_RED       0x0000ff00
#define TEST_XRGB_BLACK     0x00000000
#define TEST_XRGB_GREY      0x60606000
#define TEST_XRGB_ORANGERED 0x4763FF00

#define XRGB_DEFAULT_COLOR TEST_XRGB_BLACK

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
void surface_update_rect(struct surface_t *surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void surface_destroy(struct surface_t *surface);

typedef struct virtio_gpu_rect rect_t;

struct font_t {
    uint32_t char_width;
    uint32_t char_height;

    struct xrgb_pixel *bitmaps;
};

struct vector {
    uint64_t x;
    uint64_t y;
};


struct surface_t *get_main_surface();
struct font_t *get_main_font();

void
surface_draw_circle(struct surface_t *resource, uint64_t x, uint64_t y, uint64_t r, uint32_t color);

// SDL_FillRect
void
surface_fill_rect(struct surface_t *surface, const rect_t *rect, uint32_t color);

void
surface_fill_texture(struct surface_t *surface, const rect_t *rect, uint32_t *texture, int y_mirror, uint32_t extra_color);

void
load_font(struct font_t *font);

uint32_t
surface_draw_text(struct surface_t *surface, struct font_t *font, const char *str, uint32_t x, uint32_t y);

void
surface_clear(struct surface_t *surface, uint32_t color);

void sleep(uint32_t ms);
uint64_t current_ms();