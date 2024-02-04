/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>
#include <kern/macro.h>

#include <kern/picirq.h>

#ifdef CONFIG_KSPACE

.globl thdlr_syscall
.type thdlr_syscall, @function
thdlr_syscall:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(T_SYSCALL), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq0
.type thdlr_irq0, @function
thdlr_irq0:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 0), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq1
.type thdlr_irq1, @function
thdlr_irq1:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 1), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq2
.type thdlr_irq2, @function
thdlr_irq2:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 2), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq3
.type thdlr_irq3, @function
thdlr_irq3:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 3), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq4
.type thdlr_irq4, @function
thdlr_irq4:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 4), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq5
.type thdlr_irq5, @function
thdlr_irq5:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 5), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq6
.type thdlr_irq6, @function
thdlr_irq6:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 6), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq7
.type thdlr_irq7, @function
thdlr_irq7:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 7), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq8
.type thdlr_irq8, @function
thdlr_irq8:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 8), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq9
.type thdlr_irq9, @function
thdlr_irq9:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 9), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq10
.type thdlr_irq10, @function
thdlr_irq10:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 10), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq11
.type thdlr_irq11, @function
thdlr_irq11:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 11), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq12
.type thdlr_irq12, @function
thdlr_irq12:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 12), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq13
.type thdlr_irq13, @function
thdlr_irq13:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 13), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq14
.type thdlr_irq14, @function
thdlr_irq14:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 14), 136(%rsp)
    call trap
    jmp .

.globl thdlr_irq15
.type thdlr_irq15, @function
thdlr_irq15:
    call save_trapframe_trap
    # Set trap code for trapframe
    movl $(IRQ_OFFSET + 15), 136(%rsp)
    call trap
    jmp .

#endif
