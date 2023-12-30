
#include "pong.h"
#include "pong-utilities.h"
#include "graphic.h"
#include <inc/stdio.h>

static int max_score = 9;
static int paddle_width = 11;
static int paddle_height = 50;
static int paddle_padding = 30;
static int net_offset = 30;
static int ball_size = 10;
static int player_paddle_speed = 10;
static int ai_paddle_speed = 5;
static int frame_width = 2;

struct game_data;

typedef void (*draw_func)(void *, struct surface_t *);
typedef void (*move_func)(struct game_data *);


enum EffectType {
    SPLASH,
};

typedef struct rectangle {
    int x, y;
    int w, h;
    uint32_t color;
    draw_func draw;
    move_func move;
} rectangle_t;

typedef struct paddle {
    rectangle_t rect;
    int v_y;
} paddle_t;

typedef struct ball {
    rectangle_t rect;
    int v_x, v_y;
} ball_t;


typedef struct effect {
    rectangle_t *traced_rect;
    enum EffectType type;
    int x_offset;
    int y_offset;
    int y_mirror;
    int frame;
    int enable;
    uint32_t extra_color;
} effect_t;


static struct game_data {
    ball_t ball;
    paddle_t paddle[2];
    rectangle_t net;
    rectangle_t gates[2];
    effect_t splash;
    rectangle_t *objects[8];
    int ndrawable;
    int nmovable;
    int ai_score;
    int user_score;
    struct surface_t screen;
} game_info;


#define RECT2GPU_RECT(game_rect_name, gpu_rect_name)   \
    rectangle_t *rect = (rectangle_t *)game_rect_name; \
    struct virtio_gpu_rect gpu_rect_name = {.height = rect->h, .width = rect->w, .x = rect->x, .y = rect->y};

static void
draw_ball(void *ball_rect, struct surface_t *screen) {
    RECT2GPU_RECT(ball_rect, ball);
    surface_fill_rect(screen, &ball, rect->color);
}

static void
draw_paddle(void *paddle_rect, struct surface_t *screen) {
    RECT2GPU_RECT(paddle_rect, paddle);
    surface_fill_rect(screen, &paddle, rect->color);
    surface_draw_circle(screen, paddle.x + (paddle.width -1) / 2, paddle.y, paddle.width / 2, rect->color);
    surface_draw_circle(screen, paddle.x + (paddle.width -1) / 2, paddle.y + paddle.height, paddle.width / 2, rect->color);

}

static void
draw_net(void *net_rect, struct surface_t *screen) {
    RECT2GPU_RECT(net_rect, net);

    for (int i = 0; i < screen->height / (net.height + net_offset) * 1.5; i++) {
        surface_fill_rect(screen, &net, rect->color);
        net.y += net_offset;
    }
}

static void
draw_gate(void *gate_rect, struct surface_t *screen) {
    RECT2GPU_RECT(gate_rect, gate);
    surface_fill_rect(screen, &gate, rect->color);
}

static void
draw_effect(effect_t *e, struct surface_t *screen) {
    if (!e->enable) {
        return;
    }
    int x = e->traced_rect->x;
    if (!e->y_mirror) {
        x += e->traced_rect->w;
    }
    int y = e->traced_rect->y - e->y_offset;

    switch (e->type) {
    case SPLASH:
        draw_splash_frame(screen, x, y, e->frame, e->y_mirror, e->extra_color);
        e->frame += 1;
        if (e->frame == get_splash_animation_frames()) {
            e->frame = 0;
            e->enable = 0;
        }
        break;

    default:
        break;
    }
}

static void
effect_attach(effect_t *e, rectangle_t *rect, int mirrored, int x_offset, int y_offset, uint32_t color) {
    e->enable = 1;
    e->traced_rect = rect;
    e->y_mirror = mirrored;
    e->x_offset = x_offset;
    e->y_offset = y_offset;
    e->extra_color = color;
}


static void
move_dummy(paddle_t *paddle_array, ball_t *ball, struct surface_t *screen) {
}


static void
move_paddle(struct game_data *data) {
    paddle_t *paddle = data->paddle + 1;
    paddle->rect.y += paddle->v_y;
    if (paddle->rect.y >= data->screen.height - paddle->rect.h) {
        paddle->rect.y = data->screen.height - paddle->rect.h;
    }
    if (paddle->rect.y <= 0) {
        paddle->rect.y = 0;
    }
}

static void
move_paddle_ai(struct game_data *data) {
    paddle_t *paddle = data->paddle;
    struct surface_t *screen = &data->screen;
    ball_t *ball = &data->ball;
    int center = paddle->rect.y + paddle_height / 2;
    int screen_center = screen->height / 2 - paddle_height / 2;
    int ball_speed = ball->v_y > 0 ? ball->v_y : -ball->v_y;

    if (ball->v_x > 0) {
        // return to center position
        if (center < screen_center - paddle_height / 2) {
            paddle->rect.y += ball_speed;
        } else if (center > screen_center + paddle_height / 2) {
            paddle->rect.y -= ball_speed;
        }
    } else {
        // ball moving down
        if (ball->v_y > 0) {
            if (ball->rect.y > center) {
                paddle->rect.y += ball_speed;
            } else {
                paddle->rect.y -= ball_speed;
            }
        }
        // ball moving up
        if (ball->v_y < 0) {
            if (ball->rect.y < center) {
                paddle->rect.y -= ball_speed;
            } else {
                paddle->rect.y += ball_speed;
            }
        }
        // ball moving stright across
        if (ball->v_y == 0) {
            if (ball->rect.y < center - paddle_height / 2) {
                paddle->rect.y -= paddle->v_y;
            } else if (ball->rect.y > center + paddle_height / 2) {
                paddle->rect.y += paddle->v_y;
            }
        }
    }
    if (paddle->rect.y <= 0) {
        paddle->rect.y = 0;
    } else if (paddle->rect.y >= screen->height - paddle->rect.h) {
        paddle->rect.y = screen->height - paddle->rect.h;
    }
}

// if return value is 1 collision occured. if return is 0, no collision.
static int
check_collision(ball_t *ball, paddle_t *paddle) {
    if (ball->rect.x > paddle->rect.x + paddle->rect.w) {
        return 0;
    }
    if (ball->rect.x + ball->rect.w < paddle->rect.x) {
        return 0;
    }
    if (ball->rect.y > paddle->rect.y + paddle->rect.h) {
        return 0;
    }
    if (ball->rect.y + ball->rect.h < paddle->rect.y) {
        return 0;
    }
    return 1;
}


static void
move_ball(struct game_data *data) {
    ball_t *ball = &data->ball;
    paddle_t *paddle_array = data->paddle;
    struct surface_t *screen = &data->screen;
    ball->rect.x += ball->v_x;
    ball->rect.y += ball->v_y;

    if (ball->rect.y < 0 || ball->rect.y > screen->height - 10) {
        ball->v_y = -ball->v_y;
    }
    for (int i = 0; i < 2; i++) {
        int collision = check_collision(ball, paddle_array + i);
        if (collision) {
            int y_offset = data->paddle[i].rect.y - ball->rect.y;
            uint32_t extra_color = (i == 0) ? TEST_XRGB_ORANGERED : TEST_XRGB_WHITE;
            effect_attach(&data->splash, (rectangle_t *)(paddle_array + i), i, 0, y_offset, extra_color);
            if (ball->v_x < 0) {
                ball->v_x -= 1;
            } else {
                ball->v_x += 1;
            }

            ball->v_x = -ball->v_x;

            int hit_pos = (paddle_array[i].rect.y + paddle_array[i].rect.h) - ball->rect.y;

            ball->v_y = 4 - hit_pos / 7;

            if (ball->rect.x < ball->rect.w) {
                ball->rect.x = ball->rect.w;
            }
            // ball moving left
            else if (ball->rect.x > MAX_WINDOW_WIDTH - ball->rect.w) {
                ball->rect.x = MAX_WINDOW_WIDTH - ball->rect.w;
            }
        }
    }
}


static void
ball_init(ball_t *ball, int x, int y) {
    ball->rect.x = x;
    ball->rect.y = y;
    ball->rect.w = ball_size;
    ball->rect.h = ball_size;
    ball->v_y = 1;
    ball->v_x = 1;
    ball->rect.draw = draw_ball;
    ball->rect.move = move_ball;
    ball->rect.color = TEST_XRGB_WHITE;
}

static void
effect_init(effect_t *e, enum EffectType type) {
    e->type = type;
    switch (type) {
    case SPLASH:
        e->frame = 0;
        e->y_mirror = 0;
        e->traced_rect = 0;
        e->enable = 0;
        break;
    default:
        break;
    }
}

static inline void
paddle_init(paddle_t *paddle, int x, int y, uint32_t color, move_func move) {
    paddle->rect.x = x;
    paddle->rect.y = y;
    paddle->rect.w = paddle_width;
    paddle->rect.h = paddle_height;
    paddle->rect.draw = draw_paddle;
    paddle->rect.move = move;
    paddle->rect.color = color;
}

static inline void
net_init(rectangle_t *net, int width) {
    net->x = width / 2;
    net->y = 20;
    net->w = 5;
    net->h = 15;
    net->draw = draw_net;
    net->color = TEST_XRGB_GREY;
}


static inline void
gates_init(rectangle_t *gate, struct surface_t *screen) {

    for (int i = 0; i < 2; ++i) {
        gate[i].color = TEST_XRGB_WHITE;
        gate[i].draw = draw_gate;
        gate[i].y = 0;
        gate[i].h = screen->height;
    }
    gate->x = 0;
    gate->w = frame_width;

    gate[1].x = screen->width - frame_width;
    gate[1].w = frame_width;
}

static void
game_init() {
    surface_init(&game_info.screen, MAX_WINDOW_WIDTH, MAX_WINDOW_HEIGHT);

    ball_init(&game_info.ball, game_info.screen.width / 2, game_info.screen.height / 2);

    paddle_init(&game_info.paddle[0], paddle_padding, game_info.screen.height / 2 - paddle_height, TEST_XRGB_ORANGERED, move_paddle_ai);
    game_info.paddle[0].v_y = player_paddle_speed;
    paddle_init(&game_info.paddle[1], game_info.screen.width - paddle_padding - paddle_width, game_info.screen.height / 2 - paddle_height, TEST_XRGB_WHITE, move_paddle);
    game_info.paddle[1].v_y = ai_paddle_speed;

    net_init(&game_info.net, game_info.screen.width);
    gates_init(game_info.gates, &game_info.screen);

    effect_init(&game_info.splash, SPLASH);

    game_info.objects[0] = (rectangle_t *)&game_info.paddle[0];
    game_info.objects[1] = (rectangle_t *)&game_info.paddle[1];
    game_info.objects[2] = (rectangle_t *)&game_info.ball;
    game_info.objects[3] = (rectangle_t *)&game_info.net;
    game_info.objects[4] = (rectangle_t *)&game_info.gates[0];
    game_info.objects[5] = (rectangle_t *)&game_info.gates[1];

    game_info.ndrawable = 6;
    game_info.nmovable = 3;
}


static enum State
check_game_over(struct game_data *info) {
    struct font_t *font = get_main_font();
    // check over all games
    if (info->ai_score == max_score) {
        surface_draw_text(&game_info.screen, font, "You lose!", game_info.screen.height / 2, game_info.screen.width / 2);
    } else if (info->user_score == max_score) {
        surface_draw_text(&game_info.screen, font, "You win!", game_info.screen.height / 2, game_info.screen.width / 2);
    }
    if (info->user_score == max_score || info->ai_score == max_score) {
        surface_display(&game_info.screen);
        sleep(500);
        return GAME_OVER;
    }
    // check over one batch
    if (info->ball.rect.x < 0) {
        game_info.user_score += 1;
        game_init();
    }
    if (info->ball.rect.x > info->screen.width - info->ball.rect.w) {
        game_info.ai_score += 1;
        game_init();
    }
    return GAME_RUN;
}

int
pong(void) {
    enum State state = GAME_RUN;

    // Initialize the ball position data.
    game_init();

    while (state != GAME_OVER) {
        int64_t next_game_tick = current_ms();
        // draw background

        surface_clear(&game_info.screen, TEST_XRGB_BLACK);
        draw_effect(&game_info.splash, &game_info.screen);

        draw_number(&game_info.screen, game_info.screen.height / 2, 10, game_info.ai_score);
        draw_number(&game_info.screen, game_info.screen.height / 2 + 110, 10, game_info.user_score);

        state = check_game_over(&game_info);
        enum Key keyboard_key = get_last_keyboard_key();

        switch (keyboard_key) {
        case KEY_SPACE:
            state = GAME_OVER;
            break;
        case KEY_UP:
            game_info.paddle[1].v_y = -player_paddle_speed;
            break;
        case KEY_DOWN:
            game_info.paddle[1].v_y = player_paddle_speed;
            break;
        case KEY_UNKNOWN:
            game_info.paddle[1].v_y = 0;
        default:
            break;
        }

        for (int i = 0; i < game_info.nmovable; ++i) {
            game_info.objects[i]->move(&game_info);
        }
        for (int i = 0; i < game_info.ndrawable; ++i) {
            game_info.objects[i]->draw(game_info.objects[i], &game_info.screen);
        }
        surface_display(&game_info.screen);
        game_delay(next_game_tick);
    }
    surface_destroy(&game_info.screen);
    return 0;
}
