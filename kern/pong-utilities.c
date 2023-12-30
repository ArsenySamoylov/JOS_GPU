#include "pong-utilities.h"
#include "console.h"

static int64_t delay = 1000 * 1000;
static int default_segment_width = 5;
static int default_segment_height = 35;

extern char __assets_start[];
extern char __assets_end[];

static char digit_bitmap[] = {
        0b11111010, // 0
        0b10100000, // 1
        0b01101110, // 2
        0b10101110, // 3
        0b10110100, // 4
        0b10011110, // 5
        0b11011110, // 6
        0b10100010, // 7
        0b11111110, // 8
        0b10111110, // 9
};

typedef struct asset_header {
    uint64_t magic;
    uint16_t height;
    uint16_t width;
    uint32_t nframes;
} asset_header_t;


static enum Key
get_keyboard_key() {
    int key = cons_getc();

    if (key == 0)
        return KEY_EMPTY;

    if (key == KEY_ESC) {
        key = cons_getc();
        if (key >= KEY_UP && key <= KEY_LEFT) {
            return key;
        }
    } else if (key == KEY_SPACE) {
        return key;
    }
    return KEY_UNKNOWN;
}

enum Key
get_last_keyboard_key(void) {
    enum Key keyboard_key = KEY_UNKNOWN;
    enum Key temp_key = get_keyboard_key();

    while (temp_key != KEY_EMPTY) {
        keyboard_key = temp_key;
        temp_key = get_keyboard_key();
    }
    return keyboard_key;
}

void
game_delay(int32_t game_ticks) {
    game_ticks += 15 * delay;

    int64_t sleep_time = (game_ticks - (int64_t)current_ms()) / delay;

    if (sleep_time >= 0) {
        sleep(sleep_time);
    }
}


void
draw_number(struct surface_t *screen, uint64_t x, uint64_t y, int n) {
    if (sizeof(digit_bitmap) < n) {
        return;
    }
    uint32_t padding = default_segment_width / 2;
    rect_t segment = {.x = x + padding, .y = y, .width = default_segment_height, .height = default_segment_width};
    char nseg = digit_bitmap[n];

    for (int i = 1; i < 4; ++i) {
        if ((nseg >> i) & 1) {
            surface_fill_rect(screen, &segment, TEST_XRGB_WHITE);
        }
        segment.y += default_segment_height;
    }


    segment.height = default_segment_height;
    segment.width = default_segment_width;

    for (int i = 4; i < 8; ++i) {
        segment.x = x + (i % 2) * default_segment_height;
        segment.y = y + (i / 6) * default_segment_height;
        segment.y += padding;
        if ((nseg >> i) & 1) {
            surface_fill_rect(screen, &segment, TEST_XRGB_WHITE);
        }
    }
}


static uint32_t *
get_splash_animation(uint16_t *width, uint16_t *height) {
    asset_header_t *splash_hdr = (asset_header_t *)__assets_start;
    *width = splash_hdr->width;
    *height = splash_hdr->height;
    return (uint32_t *)(__assets_start + sizeof(asset_header_t));
}

uint32_t
get_splash_animation_frames() {
    asset_header_t *splash_hdr = (asset_header_t *)__assets_start;
    return splash_hdr->nframes;
}


void
draw_splash_frame(struct surface_t *screen, uint32_t x, uint32_t y, uint32_t nframe, int mirrored) {
    uint16_t height = 0, width = 0;
    uint32_t *texture = get_splash_animation(&width, &height);
    rect_t texture_rect = {.x = x, .y = y, .height = height, .width = width};
    surface_fill_texture(screen, &texture_rect, texture + height * width * nframe, mirrored);
}
