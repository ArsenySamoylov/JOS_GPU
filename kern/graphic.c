#include "graphic.h"
#include <inc/x86.h>
#include <inc/stdio.h>
#include "timer.h"

extern char __bin_start[];
extern char __bin_end[];

static uint64_t cpu_freq_ms = 0;

struct surface_t *
get_main_surface() {
    static struct surface_t main_surface = {};
    static bool is_init = false;

    if (!is_init) {
        surface_init(&main_surface, gpu.screen_w, gpu.screen_h);
        // is_init = true; // WTF? why is not working
    }

    return &main_surface;
}
struct font_t *
get_main_font() {
    static struct font_t font;
    static int is_loaded = 0;

    if (!is_loaded) {
        load_font(&font);
        // is_loaded = 1; // WTF? why is not working
    }

    return &font;
}
void
surface_draw_circle(struct surface_t *resource, uint64_t x_center, uint64_t y_center, uint64_t r, uint32_t color) {
    for (uint64_t y = y_center - r; y <= y_center + r; y++) {
        for (uint64_t x = x_center - r; x <= x_center + r; x++) {
            if ((x - x_center) * (x - x_center) + (y - y_center) * (y - y_center) <= r * r) {
                resource->backbuf[y * resource->width + x] = color;
            }
        }
    }
}

// SDL_FillRect
void
surface_fill_rect(struct surface_t *surface, const rect_t *rect, uint32_t color) {
    for (int y = rect->y; y < rect->y + rect->height; ++y) {
        for (int x = rect->x; x < rect->x + rect->width; ++x) {
            surface->backbuf[y * surface->width + x] = color;
        }
    }
}

void
surface_fill_texture(struct surface_t *surface, const rect_t *rect, uint32_t *texture, int y_mirror) {
    if (y_mirror) {
        for (int y = rect->y; y < rect->y + rect->height; ++y) {
            for (int x = rect->x; x < rect->x + rect->width; ++x) {
                surface->backbuf[y * surface->width + x] = texture[(y - rect->y) * rect->width + rect->x + rect->width - x - 1];
            }
        }
    } else {
        for (int y = rect->y; y < rect->y + rect->height; ++y) {
            for (int x = rect->x; x < rect->x + rect->width; ++x) {
                surface->backbuf[y * surface->width + x] = texture[(y - rect->y) * rect->width + x - rect->x];
            }
        }
    }
}

void
load_font(struct font_t *font) {
    struct font_header_t *header = (struct font_header_t *)__bin_start;
    assert(header->magic == FONT_MAGIC_NUM);

    font->char_height = header->char_height;
    font->char_width = header->char_width;

    font->bitmaps = (struct xrgb_pixel *)(((char *)header) + sizeof(struct font_header_t));
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

uint32_t
surface_draw_text(struct surface_t *surface, struct font_t *font, const char *str, uint32_t x, uint32_t y) {
    while (*str) {
        surface_draw_character(surface, font, *str, x, y);
        x += font->char_width;
        str++;
    }

    return x;
}

void
surface_clear(struct surface_t *surface, uint32_t color) {
    rect_t whole_rect = {0, 0, surface->width, surface->height};

    surface_fill_rect(surface, &whole_rect, color);
}

void
sleep(uint32_t ms) {
    if (!cpu_freq_ms) {
        cpu_freq_ms = timer_for_schedule->get_cpu_freq() / 1000;
    }

    uint64_t target = read_tsc() + cpu_freq_ms * ms;

    while (read_tsc() < target) {
        asm volatile("pause");
    }
}

uint64_t
current_ms() {
    if (!cpu_freq_ms) {
        cpu_freq_ms = timer_for_schedule->get_cpu_freq() / 1000;
    }

    return read_tsc() / cpu_freq_ms;
}