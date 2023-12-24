/* Simple command-line kernel monitor useful for
 * controlling the kernel and exploring the system interactively. */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/env.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kclock.h>
#include <kern/kdebug.h>
#include <kern/tsc.h>
#include <kern/timer.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>

#include <kern/pong.h>
#include <kern/graphic.h>

#define WHITESPACE "\t\r\n "
#define MAXARGS    16

/* Functions implementing monitor commands */
int mon_help(int argc, char **argv, struct Trapframe *tf);
int mon_pong(int argc, char **argv, struct Trapframe *tf);
int mon_font(int argc, char **argv, struct Trapframe *tf);
int mon_example(int argc, char **argv, struct Trapframe *tf);

struct Command {
    const char *name;
    const char *desc;
    /* return -1 to force monitor to exit */
    int (*func)(int argc, char **argv, struct Trapframe *tf);
};

static struct Command commands[] = {
        {"help",    "Display this list of commands", mon_help},
        {"pong",    "Start playing pong",            mon_pong},
        {"font",    "Display string on screen",      mon_font},
        {"example", "Best example",                  mon_example},
};

#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))

/* Implementations of basic kernel monitor commands */

int
mon_help(int argc, char **argv, struct Trapframe *tf) {
    for (size_t i = 0; i < NCOMMANDS; i++)
        cprintf("%s - %s\n", commands[i].name, commands[i].desc);
    return 0;
}

int
mon_pong(int argc, char **argv, struct Trapframe *tf) {
    cprintf("starting pong\n");
    return pong();
}    

int mon_font(int argc, char **argv, struct Trapframe *tf){
    if(argc < 2){
        cprintf("Enter string to display\nExiting\n");
        return 0;
    }

    cprintf("Drawing '%s'\n", argv[1]);
    
    struct font_t* font = get_main_font();

    struct surface_t* main_srf_ptr = get_main_surface(); 
    surface_clear(main_srf_ptr, XRGB_DEFAULT_COLOR);
    surface_draw_text(main_srf_ptr, font, argv[1], 200, 200);

    surface_display(main_srf_ptr);
    return 0;
}
   
struct surface_t surface  = {};
struct surface_t surface2 = {};

int mon_example(int argc, char **argv, struct Trapframe *tf) {
    surface_init(&surface, gpu.screen_w, gpu.screen_h);
    surface_init(&surface2, gpu.screen_w, gpu.screen_h);

    struct vector pos = {.x = 50, .y = 50};
    surface_draw_circle(&surface, pos, 50, TEST_XRGB_RED);

    rect_t rect = {100, 100, 30, 60};
    surface_fill_rect(&surface2, &rect, TEST_XRGB_BLUE);

    struct font_t font;
    load_font(&font);

    surface_draw_text(&surface,  &font, "osdev", 200, 200);
    surface_draw_text(&surface2, &font, "osdev", 200, 200);

    while (1) {
        surface_display(&surface);
        sleep(300);
        surface_display(&surface2);
        sleep(300);

        if (get_last_keyboard_key() == KEY_SPACE) {
            break;
        }
    }

    surface_clear(&surface, XRGB_DEFAULT_COLOR);
    surface_draw_text(&surface, &font, "END OF DEMO", 200, 200);
    surface_display(&surface);

    sleep(500);
    
    surface_destroy(&surface);
    surface_destroy(&surface2);
    return 0;
}

/* Kernel monitor command interpreter */

static int
runcmd(char *buf, struct Trapframe *tf) {
    int argc = 0;
    char *argv[MAXARGS];

    argv[0] = NULL;

    /* Parse the command buffer into whitespace-separated arguments */
    for (;;) {
        /* gobble whitespace */
        while (*buf && strchr(WHITESPACE, *buf)) *buf++ = 0;
        if (!*buf) break;

        /* save and scan past next arg */
        if (argc == MAXARGS - 1) {
            cprintf("Too many arguments (max %d)\n", MAXARGS);
            return 0;
        }
        argv[argc++] = buf;
        while (*buf && !strchr(WHITESPACE, *buf)) buf++;
    }
    argv[argc] = NULL;

    /* Lookup and invoke the command */
    if (!argc) return 0;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0)
            return commands[i].func(argc, argv, tf);
    }

    cprintf("Unknown command '%s'\n", argv[0]);
    return 0;
}

void
monitor(struct Trapframe *tf) {

    cprintf("Welcome to the JOS kernel monitor!\n");
    cprintf("Type 'help' for a list of commands.\n");

    if (tf) print_trapframe(tf);

    char *buf;
    do buf = readline("K> ");
    while (!buf || runcmd(buf, tf) >= 0);
}
