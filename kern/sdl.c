#include "sdl.h"

int
draw_circle(struct surface_t *resource, struct vector pos, uint64_t r, uint32_t color) {
    for (int i = pos.x - r; i <= pos.x + r; i++) {
        for (int j = pos.y - r; j <= pos.y + r; j++) {
            if ((i - r) * (i - r) + (j - r) * (j - r) <= r * r) {
                resource->backbuf[i * resource->width + j] = color;
            }
        }
    }
    return 0;
}