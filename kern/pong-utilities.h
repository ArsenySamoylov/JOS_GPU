#pragma once

#include <inc/x86.h>
#include "graphic.h"

enum Key {
    KEY_EMPTY,
    KEY_UNKNOWN,
    KEY_ESC = '[',
    KEY_UP = 'A',
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_SPACE = ' ',
};

enum State {
    GAME_OVER,
    GAME_RUN,
    AI_WIN,
    USER_WIN,
};

enum Key get_last_keyboard_key(void);
void game_delay(int32_t game_ticks);
void draw_number(struct surface_t *screen, uint64_t x, uint64_t y, int n);
void draw_splash_frame(struct surface_t *screen, uint32_t x, uint32_t y, uint32_t nframe, int mirrored);
uint32_t get_splash_animation_frames();