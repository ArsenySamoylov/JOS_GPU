#include "graphic.h"
#include <inc/x86.h>
#include <inc/stdio.h>
#include "timer.h"

extern char __bin_start[];
extern char __bin_end[];

static uint64_t cpu_freq_ms = 0;

void
surface_draw_circle(struct surface_t *resource, struct vector pos, uint64_t r, uint32_t color) {
    for (int i = pos.y - r; i <= pos.y + r; i++) {
        for (int j = pos.x - r; j <= pos.x + r; j++) {
            if ((i - r) * (i - r) + (j - r) * (j - r) <= r * r) {
                resource->backbuf[i * resource->width + j] = color;
            }
        }
    }
}

// SDL_FillRect
void
surface_fill_rect(struct surface_t *surface, const struct virtio_gpu_rect *rect, uint32_t color) {
    for (int y = rect->y; y < rect->y + rect->height; ++y) {
        for (int x = rect->x; x < rect->x + rect->width; ++x) {
            surface->backbuf[y * surface->width + x] = color;
        }
    }
}

void
load_font(struct font_t *font) {
    struct font_header_t * header = (struct font_header_t *) __bin_start;
    assert(header->magic == FONT_MAGIC_NUM);

    font->char_height = header->char_height;
    font->char_width  = header->char_width;

    font->bitmaps = (struct xrgb_pixel *) (((char *)header) + sizeof(struct font_header_t));
}

static void
surface_draw_character(struct surface_t *surface, struct font_t *font, char ch, uint32_t pos_x, uint32_t pos_y) {
    struct xrgb_pixel *bitmap_array = font->bitmaps + font->char_height * font->char_width * FONT_INDEX(ch);

    for (int y = 0; y < font->char_height; ++y) {
        for (int x = 0; x < font->char_width; ++x) {
            struct xrgb_pixel *bitmap_pixel = bitmap_array + y * font->char_width + x;

            if (bitmap_pixel->is_enabled) {
                surface->backbuf[(pos_y + y) * surface->width + pos_x + x] = bitmap_pixel->xrgb_val;
            }
        }
    }
}

void
surface_draw_text(struct surface_t *surface, struct font_t *font, const char *str, uint32_t x, uint32_t y) {
    while (*str) {
        surface_draw_character(surface, font, *str, x, y);
        x += font->char_width;
        str++;
    }
}

void
sleep(uint32_t ms) {
    if (!cpu_freq_ms) {
        cpu_freq_ms = timer_for_schedule->get_cpu_freq() / 1000;
    }

    uint64_t target = read_tsc() + cpu_freq_ms * ms;

    while (read_tsc() < target) {
        asm volatile ("pause");
    }
}

uint64_t current_ms() {
    if (!cpu_freq_ms) {
        cpu_freq_ms = timer_for_schedule->get_cpu_freq() / 1000;
    }

    return read_tsc() / cpu_freq_ms;
}