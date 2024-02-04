#include <kern/sem.h>
#include <stdbool.h>
#include <kern/sched.h>
#include <kern/env.h>
#include <inc/stdio.h>

void
sem_post(struct sem *sem) {
    asm volatile("cli");
    sem->val += 1;
    asm volatile("sti");
}

void
sem_wait(struct sem *sem) {
    asm volatile("cli");
    if (sem->val > 0) {
        sem->val -= 1;
        asm volatile("sti");
        return;
    }
    if (!curenv) {
        cprintf("tf\n");
    }
    curenv->sem = sem;
    asm volatile("int $0x30");
    asm volatile("sti");
}
