#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/env.h>
#include <kern/monitor.h>


struct Taskstate cpu_ts;

struct Env *
sched(void) {
    const int num = !curenv ? 0 : (curenv - envs) + 1;
    for (int i = num; i < NENV; i++) {
        if (envs[i].env_status == ENV_RUNNABLE &&
            envs[i].env_type != ENV_TYPE_IDLE &&
            curenv != envs + i) {
            if (envs[i].sem && envs[i].sem->val <= 0) {
                continue;
            }
            return envs + i;
        }
    }

    for (int i = 0; i < num; i++) {
        if (envs[i].env_status == ENV_RUNNABLE &&
            envs[i].env_type != ENV_TYPE_IDLE &&
            curenv != envs + i) {
            if (envs[i].sem && envs[i].sem->val <= 0) {
                continue;
            }
            return envs + i;
        }
    }

    if (curenv && curenv->env_status == ENV_RUNNING &&
        curenv->env_type != ENV_TYPE_IDLE) {
        if ((curenv->sem && curenv->sem->val > 0) || !curenv->sem) {
            return curenv;
        }
    }
    
    for (int i = 0; i < NENV; i++) {
        if ((envs[i].env_status == ENV_RUNNABLE ||
             envs[i].env_status == ENV_RUNNING) &&
            envs[i].env_type == ENV_TYPE_IDLE) {
                return envs + i;
            }
    }
    
    return NULL;
}

/* Choose a user environment to run and run it */
_Noreturn void
sched_yield(void) {
    /* Implement simple round-robin scheduling.
     *
     * Search through 'envs' for an ENV_RUNNABLE environment in
     * circular fashion starting just after the env was
     * last running.  Switch to the first such environment found.
     *
     * If no envs are runnable, but the environment previously
     * running is still ENV_RUNNING, it's okay to
     * choose that environment.
     *
     * If there are no runnable environments,
     * simply drop through to the code
     * below to halt the cpu */

    // LAB 3: Your code here:
    struct Env *e = sched();
    assert(e);
    if (e->sem) {
        e->sem->val -= 1;
        e->sem = NULL;
    }
    env_run(e);
}
