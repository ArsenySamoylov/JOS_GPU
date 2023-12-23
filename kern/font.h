#pragma once

#include <stdint.h>

#define FONT_MAGIC_NUM (0xD34DD3D)
#define FONT_SYMBOLS_NUM (96)
#define FONT_INDEX(c) (c - 32) // so we start from printable ' ' and not '\0'

/**
 * File format
 * 
 * <font_header_t>
 * right after header: 
 * 96 x char_width x char_height xrgb_pixel structs
 */

struct font_header_t {
    uint64_t magic;         /* should be FONT_MAGIC_NUM */
    uint32_t char_width;
    uint32_t char_height;
};


struct xrgb_pixel {
    union {
        struct {
            uint8_t B;
            uint8_t G;
            uint8_t R;
            uint8_t is_enabled;
        };

        uint32_t xrgb_val;
    };
};
