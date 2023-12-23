#include "graphic.h"

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