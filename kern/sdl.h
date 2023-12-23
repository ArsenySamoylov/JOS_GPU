#pragma once
#include "graphic.h"

#define TEST_XRGB_WHITE 0xffffffff
#define TEST_XRGB_BLUE  0xff2200FF
#define TEST_XRGB_RED   0x0000ff00

struct vector {
    uint64_t x;
    uint64_t y;
};

int draw_circle(struct surface_t *resource, struct vector pos, uint64_t r, uint32_t color);
