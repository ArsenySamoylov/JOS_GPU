
#include <inc/stdio.h>
#include "pong.h"


typedef struct ball_s {
    int x, y;     /* position on the screen */
    int w, h;     // ball width and height
    int v_x, v_y; /* movement vector */
} ball_t;

typedef struct paddle {
    int x, y;
    int w, h;
} paddle_t;

static ball_t ball;
static paddle_t paddle[2];
int score[] = {0, 0};
struct surface_t screen;

static void
init_game() {
    surface_init(&screen, MAX_WINDOW_WIDTH, MAX_WINDOW_HEIGHT);

    ball.x = screen.width / 2;
    ball.y = screen.height / 2;
    ball.w = 10;
    ball.h = 10;
    ball.v_y = 1;
    ball.v_x = 1;

    paddle[0].x = 20;
    paddle[0].y = screen.height / 2 - 50;
    paddle[0].w = 10;
    paddle[0].h = 50;

    paddle[1].x = screen.width - 20 - 10;
    paddle[1].y = screen.height / 2 - 50;
    paddle[1].w = 10;
    paddle[1].h = 50;
}

int
check_score() {
    // loop through player scores
    for (int i = 0; i < 2; i++) {
        if (score[i] == 10) {
            score[0] = 0;
            score[1] = 0;

            if (i == 0) {
                return 1;
            } else {
                return 2;
            }
        }
    }
    return 0;
}

// if return value is 1 collision occured. if return is 0, no collision.
int
check_collision(ball_t ball, paddle_t paddle) {
    if (ball.x > paddle.x + paddle.w) {
        return 0;
    }

    if (ball.x + ball.w < paddle.x) {
        return 0;
    }

    if (ball.y > paddle.y + paddle.h) {
        return 0;
    }

    if (ball.y + ball.h < paddle.y) {
        return 0;
    }

    return 1;
}

static void
move_ball() {

    /* Move the ball by its motion vector. */
    ball.x += ball.v_x;
    ball.y += ball.v_y;

    /* Turn the ball around if it hits the edge of the screen. */
    if (ball.x < 0) {
        score[1] += 1;
        init_game();
    }

    if (ball.x > screen.width - 10) {
        score[0] += 1;
        init_game();
    }

    if (ball.y < 0 || ball.y > screen.height - 10) {
        ball.v_y = -ball.v_y;
    }
    for (int i = 0; i < 2; i++) {

        int c = check_collision(ball, paddle[i]);

        // collision detected
        if (c == 1) {

            // ball moving left
            if (ball.v_x < 0) {

                ball.v_x -= 1;

                // ball moving right
            } else {

                ball.v_x += 1;
            }

            // change ball direction
            ball.v_x = -ball.v_x;

            // change ball angle based on where on the paddle it hit
            int hit_pos = (paddle[i].y + paddle[i].h) - ball.y;

            if (hit_pos >= 0 && hit_pos < 7) {
                ball.v_y = 4;
            }

            if (hit_pos >= 7 && hit_pos < 14) {
                ball.v_y = 3;
            }

            if (hit_pos >= 14 && hit_pos < 21) {
                ball.v_y = 2;
            }

            if (hit_pos >= 21 && hit_pos < 28) {
                ball.v_y = 1;
            }

            if (hit_pos >= 28 && hit_pos < 32) {
                ball.v_y = 0;
            }

            if (hit_pos >= 32 && hit_pos < 39) {
                ball.v_y = -1;
            }

            if (hit_pos >= 39 && hit_pos < 46) {
                ball.v_y = -2;
            }

            if (hit_pos >= 46 && hit_pos < 53) {
                ball.v_y = -3;
            }

            if (hit_pos >= 53 && hit_pos <= 60) {
                ball.v_y = -4;
            }

            // ball moving right
            if (ball.v_x > 0) {

                // teleport ball to avoid mutli collision glitch
                if (ball.x < 30) {

                    ball.x = 30;
                }

                // ball moving left
            } else {

                // teleport ball to avoid mutli collision glitch
                if (ball.x > 600) {
                    ball.x = 600;
                }
            }
        }
    }
}

static void
move_paddle_ai() {

    int center = paddle[0].y + 25;
    int screen_center = screen.height / 2 - 25;
    int ball_speed = ball.v_y;

    if (ball_speed < 0) {

        ball_speed = -ball_speed;
    }

    // ball moving right
    if (ball.v_x > 0) {

        // return to center position
        if (center < screen_center) {

            paddle[0].y += ball_speed;

        } else {

            paddle[0].y -= ball_speed;
        }

        // ball moving left
    } else {

        // ball moving down
        if (ball.v_y > 0) {

            if (ball.y > center) {

                paddle[0].y += ball_speed;

            } else {

                paddle[0].y -= ball_speed;
            }
        }

        // ball moving up
        if (ball.v_y < 0) {

            if (ball.y < center) {

                paddle[0].y -= ball_speed;

            } else {

                paddle[0].y += ball_speed;
            }
        }

        // ball moving stright across
        if (ball.v_y == 0) {

            if (ball.y < center) {

                paddle[0].y -= 5;

            } else {

                paddle[0].y += 5;
            }
        }
    }
}

static void
move_paddle(int d) {

    // if the down arrow is pressed move paddle down
    if (d == 0) {
        if (paddle[1].y >= screen.height - paddle[1].h) {
            paddle[1].y = screen.height - paddle[1].h;

        } else {
            paddle[1].y += 5;
        }
    }
    // if the up arrow is pressed move paddle up
    if (d == 1) {
        if (paddle[1].y <= 0) {
            paddle[1].y = 0;
        } else {
            paddle[1].y -= 5;
        }
    }
}


static void
draw_ball() {
    struct virtio_gpu_rect ball_rect;

    ball_rect.height = ball.h;
    ball_rect.width = ball.w;
    ball_rect.x = ball.x;
    ball_rect.y = ball.y;

    surface_fill_rect(&screen, &ball_rect, TEST_XRGB_WHITE);
}

static void
draw_paddle() {

    struct virtio_gpu_rect paddle_rect;

    for (int i = 0; i < 2; i++) {

        paddle_rect.x = paddle[i].x;
        paddle_rect.y = paddle[i].y;
        paddle_rect.width = paddle[i].w;
        paddle_rect.height = paddle[i].h;

        surface_fill_rect(&screen, &paddle_rect, TEST_XRGB_WHITE);
    }
}

int pong() {
    int quit = 0;
    int state = 0;
    int result = 0;

    // Initialize the ball position data.
    init_game();

    // render loop
    while (quit == 0) {
        // draw background
        rect_t screen_rect = {.x = 0, .y = 0, .height = screen.height, .width = screen.width};
        surface_fill_rect(&screen, &screen_rect, 0x00000000);

        result = check_score();
        // if either player wins, change to game over state
        if (result == 1) {
            state = 2;
        } else if (result == 2) {
            state = 2;
        }

        // paddle ai movement
        // move_paddle_ai();

        //* Move the balls for the next frame.
        move_ball();

        // // draw net
        // draw_net();

        // draw paddles
        draw_paddle();

        //* Put the ball on the screen.
        draw_ball();

        surface_display(&screen);
    }
    surface_destroy(&screen);
    return 0;
}
