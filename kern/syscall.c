/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/console.h>
#include <kern/env.h>
#include <kern/kclock.h>
#include <kern/pmap.h>
#include <kern/sched.h>
#include <kern/syscall.h>
#include <kern/trap.h>
#include <kern/traceopt.h>

/* Print a string to the system console.
 * The string is exactly 'len' characters long.
 * Destroys the environment on memory errors. */
static int
sys_cputs(const char *s, size_t len) {
    // LAB 8: Your code here

    /* Check that the user has permission to read memory [s, s+len).
     * Destroy the environment if not. */
    #ifdef SANITIZE_SHADOW_BASE
    platform_asan_unpoison((void*)s, len);
    #endif

    user_mem_assert(curenv, s, len, PROT_R); // may not return

    for (int i = 0; i < len; ++i) {
        cputchar(*(s+i));
    }

    #ifdef SANITIZE_SHADOW_BASE
    platform_asan_poison((void*)s, len);
    #endif
    
    return 0;
}

/* Read a character from the system console without blocking.
 * Returns the character, or 0 if there is no input waiting. */
static int
sys_cgetc(void) {
    // LAB 8: Your code here
    return cons_getc();
}

/* Returns the current environment's envid. */
static envid_t
sys_getenvid(void) {
    // LAB 8: Your code here
    return curenv->env_id;
}

/* Destroy a given environment (possibly the currently running environment).
 *
 *  Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid. */
static int
sys_env_destroy(envid_t envid) {
    // LAB 8: Your code here.
    struct Env* prey;
    int res = envid2env(envid, &prey, true);
    if (res) {
        return -E_BAD_ENV;
    }

#if 1 /* TIP: Use this snippet to log required for passing grade tests info */
    if (trace_envs) {
        struct Env* env = prey;
        cprintf(env == curenv ?
                        "[%08x] exiting gracefully\n" :
                        "[%08x] destroying %08x\n",
                curenv->env_id, env->env_id);
    }
#endif

    env_destroy(prey);
    return 0;
}

/* Deschedule current environment and pick a different one to run. */
static void
sys_yield(void) {
    // LAB 9: Your code here
    sched_yield();
}

/* Allocate a new environment.
 * Returns envid of new environment, or < 0 on error.  Errors are:
 *  -E_NO_FREE_ENV if no free environment is available.
 *  -E_NO_MEM on memory exhaustion. */
static envid_t
sys_exofork(void) {
    /* Create the new environment with env_alloc(), from kern/env.c.
     * It should be left as env_alloc created it, except that
     * status is set to ENV_NOT_RUNNABLE, and the register set is copied
     * from the current environment -- but tweaked so sys_exofork
     * will appear to return 0. */

    // LAB 9: Your code here
    struct Env* nenv;
    assert(curenv);
    int status = env_alloc(&nenv, curenv->env_id, ENV_TYPE_USER);
    if (status)
        return status;

    nenv->env_status = ENV_NOT_RUNNABLE;
    nenv->env_tf = curenv->env_tf;
    nenv->env_tf.tf_regs.reg_rax = 0;

    return nenv->env_id;
}

/* Set envid's env_status to status, which must be ENV_RUNNABLE
 * or ENV_NOT_RUNNABLE.
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if status is not a valid status for an environment. */
static int
sys_env_set_status(envid_t envid, int env_status) {
    /* Hint: Use the 'envid2env' function from kern/env.c to translate an
     * envid to a struct Env.
     * You should set envid2env's third argument to 1, which will
     * check whether the current environment has permission to set
     * envid's status. */

    // LAB 9: Your code here
    if (env_status != ENV_RUNNABLE && env_status != ENV_NOT_RUNNABLE)
        return -E_INVAL;

    struct Env* env;
    int status = envid2env(envid, &env, true);
    if (status) 
        return status;

    env->env_status = env_status;
    return 0;
}

/* Set the page fault upcall for 'envid' by modifying the corresponding struct
 * Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
 * kernel will push a fault record onto the exception stack, then branch to
 * 'func'.
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid. */
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func) {
    // LAB 9: Your code here:
    assert(func);

    struct Env* env;
    int status = envid2env(envid, &env, true);
    if (status) 
        return status;

    env->env_pgfault_upcall = func;
    return 0;
}

/* Allocate a region of memory and map it at 'va' with permission
 * 'perm' in the address space of 'envid'.
 * The page's contents are set to 0.
 * If a page is already mapped at 'va', that page is unmapped as a
 * side effect.
 *
 * This call should work with or without ALLOC_ZERO/ALLOC_ONE flags
 * (set them if they are not already set)
 *
 * It allocates memory lazily so you need to use map_region
 * with PROT_LAZY and ALLOC_ONE/ALLOC_ZERO set.
 *
 * Don't forget to set PROT_USER_
 *
 * PROT_ALL is useful for validation.
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if va >= MAX_USER_ADDRESS, or va is not page-aligned.
 *  -E_INVAL if perm is inappropriate (see above).
 *  -E_NO_MEM if there's no memory to allocate the new page,
 *      or to allocate any necessary page tables. */
static int
sys_alloc_region(envid_t envid, uintptr_t addr, size_t size, int perm) {
    // LAB 9: Your code here:
    struct Env* env;
    int status = envid2env(envid, &env, true);
    if (status) 
        return status;
    
    if (addr >= MAX_USER_ADDRESS || addr % PAGE_SIZE != 0)
        return -E_INVAL;

    if ((perm & ~(ALLOC_ONE | ALLOC_ZERO)) != (perm & PROT_ALL)) {
        return -E_INVAL;
    }

    if (!(perm & ALLOC_ONE)) {
        perm |= ALLOC_ZERO;
    }

    perm |= PROT_USER_ | PROT_LAZY;
    status = map_region(&env->address_space, addr, NULL, 0, size, perm);
    return status;
}

/* Map the region of memory at 'srcva' in srcenvid's address space
 * at 'dstva' in dstenvid's address space with permission 'perm'.
 * Perm has the same restrictions as in sys_alloc_region, except
 * that it also does not supprt ALLOC_ONE/ALLOC_ONE flags.
 *
 * You only need to check alignment of addresses, perm flags and
 * that addresses are a part of user space. Everything else is
 * already checked inside map_region().
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
 *      or the caller doesn't have permission to change one of them.
 *  -E_INVAL if srcva >= MAX_USER_ADDRESS or srcva is not page-aligned,
 *      or dstva >= MAX_USER_ADDRESS or dstva is not page-aligned.
 *  -E_INVAL is srcva is not mapped in srcenvid's address space.
 *  -E_INVAL if perm is inappropriate (see sys_page_alloc).
 *  -E_INVAL if (perm & PROT_W), but srcva is read-only in srcenvid's
 *      address space.
 *  -E_NO_MEM if there's no memory to allocate any necessary page tables. */

static int
sys_map_region(envid_t srcenvid, uintptr_t srcva,
               envid_t dstenvid, uintptr_t dstva, size_t size, int perm) {
    // LAB 9: Your code here
    struct Env* src_env;
    int status = envid2env(srcenvid, &src_env, true);
    if (status) 
        return status;
    
    struct Env* dst_env;
    status = envid2env(dstenvid, &dst_env, true);
    if (status) 
        return status;
    
    if (srcva  >= MAX_USER_ADDRESS || srcva % PAGE_SIZE != 0)
        return -E_INVAL;
    if (dstva >= MAX_USER_ADDRESS || dstva % PAGE_SIZE != 0)
        return -E_INVAL;
    if (perm & (~PROT_ALL))
        return -E_INVAL;
    if (perm & (ALLOC_ZERO | ALLOC_ONE))
        return -E_INVAL;

    // status = user_mem_check(src_env, (void*) srcva, size, perm);
    if (status)
        return status;

    status = map_region(&dst_env->address_space, dstva, &src_env->address_space, srcva, size, perm | PROT_USER_);
    return status;
}

/* Unmap the region of memory at 'va' in the address space of 'envid'.
 * If no page is mapped, the function silently succeeds.
 *
 * Return 0 on success, < 0 on error.  Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_INVAL if va >= MAX_USER_ADDRESS, or va is not page-aligned. */
static int
sys_unmap_region(envid_t envid, uintptr_t va, size_t size) {
    /* Hint: This function is a wrapper around unmap_region(). */

    // LAB 9: Your code here
    struct Env* env;
    int status = envid2env(envid, &env, true);
    if (status) 
        return status;

     if (va >= MAX_USER_ADDRESS || va % PAGE_SIZE != 0)
        return -E_INVAL;

    unmap_region(&env->address_space, va, size);
    return 0;
}

/* Map region of physical memory to the userspace address.
 * This is meant to be used by the userspace drivers, of which
 * the only one currently is the filesystem server.
 *
 * Return 0 on succeeds, < 0 on error. Erros are:
 *  -E_BAD_ENV if environment envid doesn't currently exist,
 *      or the caller doesn't have permission to change envid.
 *  -E_BAD_ENV if is not a filesystem driver (ENV_TYPE_FS).
 *  -E_INVAL if va >= MAX_USER_ADDRESS, or va is not page-aligned.
 *  -E_INVAL if pa is not page-aligned.
 *  -E_INVAL if size is not page-aligned.
 *  -E_INVAL if prem contains invalid flags
 *     (including PROT_SHARE, PROT_COMBINE or PROT_LAZY).
 *  -E_NO_MEM if address does not exist.
 *  -E_NO_ENT if address is already used. */
static int
sys_map_physical_region(uintptr_t pa, envid_t envid, uintptr_t va, size_t size, int perm) {
    // LAB 10: Your code here
    // TIP: Use map_physical_region() with (perm | PROT_USER_ | MAP_USER_MMIO)
    //      And don't forget to validate arguments as always.
    struct Env* env;
    int status = envid2env(envid, &env, true);
    if (status) 
        return status;

    if (env->env_type != ENV_TYPE_FS) 
        return -E_INVAL;

    if (pa >= MAX_USER_ADDRESS || pa % PAGE_SIZE != 0)
        return -E_INVAL;
    if (size % PAGE_SIZE != 0)
        return -E_INVAL;
    if (va >= MAX_USER_ADDRESS || va % PAGE_SIZE != 0)
        return -E_INVAL;
    if (perm & ~PROT_ALL || perm & (PROT_SHARE | PROT_COMBINE | PROT_LAZY))
        return -E_INVAL;
    
    status = map_physical_region(&env->address_space, va, pa, size, perm | PROT_USER_ | MAP_USER_MMIO);
    return 0;
}

/* Try to send 'value' to the target env 'envid'.
 * If srcva < MAX_USER_ADDRESS, then also send region currently mapped at 'srcva',
 * so that receiver gets mapping.
 *
 * The send fails with a return value of -E_IPC_NOT_RECV if the
 * target is not blocked, waiting for an IPC.
 *
 * The send also can fail for the other reasons listed below.
 *
 * Otherwise, the send succeeds, and the target's ipc fields are
 * updated as follows:
 *    env_ipc_recving is set to 0 to block future sends;
 *    env_ipc_maxsz is set to min of size and it's current vlaue;
 *    env_ipc_from is set to the sending envid;
 *    env_ipc_value is set to the 'value' parameter;
 *    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
 * The target environment is marked runnable again, returning 0
 * from the paused sys_ipc_recv system call.  (Hint: does the
 * sys_ipc_recv function ever actually return?)
 *
 * If the sender wants to send a page but the receiver isn't asking for one,
 * then no page mapping is transferred, but no error occurs.
 * The ipc only happens when no errors occur.
 * Send region size is the minimum of sized specified in sys_ipc_try_send() and sys_ipc_recv()
 *
 * Returns 0 on success, < 0 on error.
 * Errors are:
 *  -E_BAD_ENV if environment envid doesn't currently exist.
 *      (No need to check permissions.)
 *  -E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
 *      or another environment managed to send first.
 *  -E_INVAL if srcva < MAX_USER_ADDRESS but srcva is not page-aligned.
 *  -E_INVAL if srcva < MAX_USER_ADDRESS and perm is inappropriate
 *      (see sys_page_alloc).
 *  -E_INVAL if srcva < MAX_USER_ADDRESS but srcva is not mapped in the caller's
 *      address space.
 *  -E_INVAL if (perm & PTE_W), but srcva is read-only in the
 *      current environment's address space.
 *  -E_NO_MEM if there's not enough memory to map srcva in envid's
 *      address space. */
static int
sys_ipc_try_send(envid_t envid, uint32_t value, uintptr_t srcva, size_t size, int perm) {
    // LAB 9: Your code here
    struct Env* target;
    int status = envid2env(envid, &target, false);
    if (status)
        return status;

    if (!target->env_ipc_recving)
        return -E_IPC_NOT_RECV;
    
    if (srcva < MAX_USER_ADDRESS) {
        if(srcva & CLASS_MASK(0)) 
            return -E_INVAL;
        
        // TODO check perm
        // TODO check that srcva mapped
        target->env_ipc_maxsz = MIN(target->env_ipc_maxsz, size);
        
        // TODO unmap dstva if it already mapped
        status = map_region(&target->address_space, target->env_ipc_dstva, &curenv->address_space, srcva, target->env_ipc_maxsz, perm | PROT_USER_);
        if (status)
            return status;
    } else {
        if (perm) {
            cprintf("Warning: maping, but perm not 0\n"); 
            perm = 0;
            }
    }

    target->env_ipc_recving = false; 
    target->env_ipc_value   = value;
    target->env_ipc_from    = curenv->env_id;
    target->env_ipc_perm    = perm;

    target->env_status = ENV_RUNNABLE;
    return 0;
}

/* Block until a value is ready.  Record that you want to receive
 * using the env_ipc_recving, env_ipc_maxsz and env_ipc_dstva fields of struct Env,
 * mark yourself not runnable, and then give up the CPU.
 *
 * If 'dstva' is < MAX_USER_ADDRESS, then you are willing to receive a page of data.
 * 'dstva' is the virtual address at which the sent page should be mapped.
 *
 * This function only returns on error, but the system call will eventually
 * return 0 on success.
 * Return < 0 on error.  Errors are:
 *  -E_INVAL if dstva < MAX_USER_ADDRESS but dstva is not page-aligned;
 *  -E_INVAL if dstva is valid and maxsize is 0,
 *  -E_INVAL if maxsize is not page aligned. */
static int
sys_ipc_recv(uintptr_t dstva, uintptr_t maxsize) {
    // LAB 9: Your code here
    if (dstva < MAX_USER_ADDRESS) {
     if (dstva & CLASS_MASK(0))
        return -E_INVAL;
     if (maxsize == 0 || maxsize & CLASS_MASK(0))
        return -E_INVAL;
    }

    curenv->env_ipc_maxsz = maxsize;
    curenv->env_ipc_dstva = dstva;
    
    curenv->env_status = ENV_NOT_RUNNABLE;
    
    curenv->env_ipc_recving = true; 

    // yield
    curenv->env_tf.tf_regs.reg_rax = 0; 
    sched_yield(); // no return !
}

static int
sys_region_refs(uintptr_t addr, size_t size, uintptr_t addr2, uintptr_t size2) {
    // LAB 10: Your code here

    return 0;
}

/* Dispatches to the correct kernel function, passing the arguments. */
uintptr_t
syscall(uintptr_t syscallno, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    /* Call the function corresponding to the 'syscallno' parameter.
     * Return any appropriate return value. */

    // LAB 8: Your code here
    switch (syscallno) {
        case SYS_cputs:
            return sys_cputs((char*) a1, (size_t) a2);
        case SYS_cgetc:
            return sys_cgetc();
        case SYS_getenvid:
            return sys_getenvid();
        case SYS_env_destroy:
            return sys_env_destroy(a1);
        // LAB 9: Your code here
        case SYS_alloc_region:
            return sys_alloc_region(a1, a2, a3, a4);
        case SYS_map_region:
            return sys_map_region(a1, a2, a3, a4, a5, a6);
        case SYS_map_physical_region:
            return sys_map_physical_region(a1, a2, a3, a4, a5);
        case SYS_unmap_region:
            return sys_unmap_region(a1, a2, a3);
        case SYS_region_refs:
            return sys_region_refs(a1, a2, a3, a4);
        case SYS_exofork:
            return sys_exofork();
        case SYS_env_set_status:
            return sys_env_set_status(a1, a2);
        case SYS_env_set_pgfault_upcall:
            return sys_env_set_pgfault_upcall(a1, (void*)a2);
        case SYS_yield:
            sys_yield();
            panic("No return!");
            break;
        case SYS_ipc_try_send:
            return sys_ipc_try_send(a1, a2, a3, a4, a5);
        case SYS_ipc_recv:
            return sys_ipc_recv(a1, a2);
    }
    // LAB 9: Your code here
    // LAB 10: Your code here

    return -E_NO_SYS;
}
