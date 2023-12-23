#include "graphic.h"
#include <inc/x86.h>
#include <inc/stdio.h>
#include "timer.h"

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