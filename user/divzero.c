/* Buggy program - causes a divide by zero exception */

#include <inc/lib.h>

volatile int zero;

void
umain(int argc, char **argv) {
    zero = 0;
    cprintf("1337/0 is %08x!\n", 1337 / zero);
    cprintf("%d\n", zero);
}
