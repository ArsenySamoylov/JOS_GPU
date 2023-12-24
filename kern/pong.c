
#include "pong.h"
#include <inc/stdio.h>
#include "console.h"

static int max_score = 9;
static int paddle_width = 10;
static int paddle_height = 50;
static int ball_size = 10;
static int player_paddle_speed = 10;
static int64_t delay = 1000 * 1000;
static int default_segment_width = 7;
static int default_segment_height = 40;
// first three is horizontal
static char digit2seg[] = {
        0b1111101, // 0
        0b1010000, // 1
        0b0110111, // 2
        0b1010111, // 3
        0b1011010, // 4
        0b1001111, // 5
        0b1101111, // 6
        0b1010100, // 7
        0b1111111, // 8
        0b1011111, // 9
};


typedef struct ball_s {
    int x, y;     /* position on the screen */
    int w, h;     // ball width and height
    int v_x, v_y; /* movement vector */
} ball_t;

typedef struct paddle {
    int x, y;
    int w, h;
} paddle_t;


static struct game_data_t {
    ball_t ball;
    paddle_t paddle[2];
    int score[2];
    struct surface_t screen;

} game_info;

enum Key {
    KEY_EMPTY,
    KEY_UNKNOWN,
    KEY_ESC = '[',
    KEY_UP = 'A',
    KEY_DOWN,
    KEY_RIGHT,
    KEY_LEFT,
    KEY_SPACE = 32,
};


static void
init_game() {
    surface_init(&game_info.screen, MAX_WINDOW_WIDTH, MAX_WINDOW_HEIGHT);

    game_info.ball.x = game_info.screen.width / 2;
    game_info.ball.y = game_info.screen.height / 2;
    game_info.ball.w = ball_size;
    game_info.ball.h = ball_size;
    game_info.ball.v_y = 1;
    game_info.ball.v_x = 1;

    game_info.paddle[0].x = 20;
    game_info.paddle[0].y = game_info.screen.height / 2 - paddle_height;
    game_info.paddle[0].w = paddle_width;
    game_info.paddle[0].h = paddle_height;

    game_info.paddle[1].x = game_info.screen.width - 20 - paddle_width;
    game_info.paddle[1].y = game_info.screen.height / 2 - paddle_height;
    game_info.paddle[1].w = paddle_width;
    game_info.paddle[1].h = paddle_height;
}

void
draw_number(struct surface_t *screen, uint64_t x, uint64_t y, int n) {
    rect_t segment;

    if (sizeof(digit2seg) < n) {
        return;
    }
    char nseg = digit2seg[n];
    uint32_t padding = default_segment_width / 2;

    segment.x = x + padding;
    segment.y = y;
    segment.width = default_segment_height;
    segment.height = default_segment_width;
    for (int i = 0; i < 3; ++i) {
        if ((nseg >> i) & 1) {
            surface_fill_rect(screen, &segment, TEST_XRGB_WHITE);
        }
        segment.y += default_segment_height;
    }


    segment.height = default_segment_height;
    segment.width = default_segment_width;

    for (int i = 3; i < 7; ++i) {
        if (i == 3) {
            segment.x = x;
            segment.y = y;
        } else if (i == 4) {
            segment.x = x + default_segment_height;
            segment.y = y;
        } else if (i == 5) {
            segment.x = x;
            segment.y = y + default_segment_height;
        } else if (i == 6) {
            segment.x = x + default_segment_height;
            segment.y = y + default_segment_height;
        }
        segment.y += padding;
        if ((nseg >> i) & 1) {
            surface_fill_rect(screen, &segment, TEST_XRGB_WHITE);
        }
    }
}

int
check_score(int *score) {
    for (int i = 0; i < 2; i++) {
        if (score[i] == max_score) {
            score[0] = 0;
            score[1] = 0;

            return i + 1;
        }
    }
    return 0;
}

// if return value is 1 collision occured. if return is 0, no collision.
int
check_collision(ball_t *ball, paddle_t paddle) {
    if (ball->x > paddle.x + paddle.w) {
        return 0;
    }
    if (ball->x + ball->w < paddle.x) {
        return 0;
    }
    if (ball->y > paddle.y + paddle.h) {
        return 0;
    }
    if (ball->y + ball->h < paddle.y) {
        return 0;
    }
    return 1;
}

static void
move_ball(ball_t *ball, int *score, struct surface_t *screen, paddle_t *paddle) {
    ball->x += ball->v_x;
    ball->y += ball->v_y;

    /* Turn the ball around if it hits the edge of the screen. */
    if (ball->x < 0) {
        score[1] += 1;
        init_game();
    }

    if (ball->x > screen->width - 10) {
        score[0] += 1;
        init_game();
    }

    if (ball->y < 0 || ball->y > screen->height - 10) {
        ball->v_y = -ball->v_y;
    }
    for (int i = 0; i < 2; i++) {
        int collision = check_collision(ball, paddle[i]);
        if (collision) {
            // ball moving left
            if (ball->v_x < 0) {

                ball->v_x -= 1;
            } else {

                ball->v_x += 1;
            }
            // change ball direction
            ball->v_x = -ball->v_x;

            int hit_pos = (paddle[i].y + paddle[i].h) - ball->y;

            ball->v_y = 4 - hit_pos / 7;
            // ball moving right
            if (ball->v_x > 0) {

                // teleport ball to avoid mutli collision glitch
                if (ball->x < ball->w) {
                    ball->x = ball->w;
                }
                // ball moving left
            } else {
                // teleport ball to avoid mutli collision glitch
                if (ball->x > MAX_WINDOW_WIDTH - ball->w) {
                    ball->x = MAX_WINDOW_WIDTH - ball->w;
                }
            }
        }
    }
}

static void
move_paddle_ai(ball_t *ball, struct surface_t *screen, paddle_t *paddle) {
    int center = paddle[0].y + paddle_height / 2;
    int screen_center = screen->height / 2 - paddle_height / 2;
    int ball_speed = ball->v_y;

    if (ball_speed < 0) {
        ball_speed = -ball_speed;
    }

    if (ball->v_x > 0) {
        // return to center position
        if (center < screen_center) {
            paddle[0].y += ball_speed;
        } else {
            paddle[0].y -= ball_speed;
        }
    } else {
        // ball moving down
        if (ball->v_y > 0) {
            if (ball->y > center) {
                paddle[0].y += ball_speed;
            } else {
                paddle[0].y -= ball_speed;
            }
        }
        // ball moving up
        if (ball->v_y < 0) {
            if (ball->y < center) {
                paddle[0].y -= ball_speed;
            } else {
                paddle[0].y += ball_speed;
            }
        }
        // ball moving stright across
        if (ball->v_y == 0) {
            if (ball->y < center) {
                paddle[0].y -= 5;
            } else {
                paddle[0].y += 5;
            }
        }
    }
    if (paddle[0].y <= 0) {
        paddle[0].y = 0;
    } else if (paddle[1].y >= screen->height - paddle[0].h) {
        paddle[0].y = screen->height - paddle[0].h;
    }
}

static void
move_paddle(struct surface_t *screen, paddle_t *paddle, enum Key direction) {
    if (direction == KEY_DOWN) {
        if (paddle[1].y >= screen->height - paddle[1].h) {
            paddle[1].y = screen->height - paddle[1].h;
        } else {
            paddle[1].y += player_paddle_speed;
        }
    }
    if (direction == KEY_UP) {
        if (paddle[1].y <= 0) {
            paddle[1].y = 0;
        } else {
            paddle[1].y -= player_paddle_speed;
        }
    }
}


static void
draw_ball(struct surface_t *screen, ball_t *ball) {
    struct virtio_gpu_rect ball_rect;

    ball_rect.height = ball->h;
    ball_rect.width = ball->w;
    ball_rect.x = ball->x;
    ball_rect.y = ball->y;

    surface_fill_rect(screen, &ball_rect, TEST_XRGB_WHITE);
}

static void
draw_paddle(struct surface_t *screen, paddle_t *paddle) {

    struct virtio_gpu_rect paddle_rect;

    for (int i = 0; i < 2; i++) {
        paddle_rect.x = paddle[i].x;
        paddle_rect.y = paddle[i].y;
        paddle_rect.width = paddle[i].w;
        paddle_rect.height = paddle[i].h;

        surface_fill_rect(screen, &paddle_rect, TEST_XRGB_WHITE);
    }
}

static void
draw_net(struct surface_t *screen) {
    rect_t net;
    net.x = screen->width / 2;
    net.y = 20;
    net.width = 5;
    net.height = 15;

    for (int i = 0; i < 15; i++) {
        surface_fill_rect(screen, &net, TEST_XRGB_WHITE);
        net.y += 30;
    }
}

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

static enum Key get_last_keyboard_key()
    {
    enum Key keyboard_key = KEY_UNKNOWN;
    enum Key temp_key = get_keyboard_key();

    while(temp_key != KEY_EMPTY) {
        keyboard_key = temp_key;
        temp_key = get_keyboard_key();
        }   

    return keyboard_key;
    }
    
int
pong() {
    int quit = 0;
    int result = 0;

    // Initialize the ball position data.
    init_game();

    // render loop
    while (quit == 0) {
        int64_t next_game_tick = current_ms();
        // draw background

        rect_t screen_rect = {.x = 0, .y = 0, .height = game_info.screen.height, .width = game_info.screen.width};
        surface_fill_rect(&game_info.screen, &screen_rect, 0x00000000);
        draw_number(&game_info.screen, game_info.screen.height / 2 - default_segment_height / 2, 10, game_info.score[0]);
        draw_number(&game_info.screen, game_info.screen.height / 2 + 7 * default_segment_height /2, 10, game_info.score[1]);

        result = check_score(game_info.score);
        // if either player wins, change to game over state

        enum Key keyboard_key = get_last_keyboard_key();

        if (keyboard_key == KEY_SPACE) {
            quit = 1;
        }
        if (keyboard_key != KEY_UNKNOWN) {
            move_paddle(&game_info.screen, game_info.paddle, keyboard_key);
        }

        move_paddle_ai(&game_info.ball, &game_info.screen, game_info.paddle);
        move_ball(&game_info.ball, game_info.score, &game_info.screen, game_info.paddle);

        draw_net(&game_info.screen);
        draw_paddle(&game_info.screen, game_info.paddle);
        draw_ball(&game_info.screen, &game_info.ball);

        surface_display(&game_info.screen);

        int64_t temp = next_game_tick;
        (void) temp;
        next_game_tick += 15 * delay;

        int64_t sleep_time = (next_game_tick - (int64_t)current_ms()) / delay;

        if (sleep_time >= 0) {
            sleep(sleep_time);
        }
        // cprintf("fps %ld\n", 1000 / ((int64_t)current_ms() - temp));

    }
    surface_destroy(&game_info.screen);
    return 0;
}
