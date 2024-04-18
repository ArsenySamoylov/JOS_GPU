#include <inc/vsyscall.h>
#include <inc/lib.h>

static inline uint64_t
vsyscall(int num) {
    // LAB 12: Your code here
    // cprintf("HEH: %p\n", vsys + num);
    return vsys[num];
}

int
vsys_gettime(void) {
    int res =0;
    while((res = vsyscall(VSYS_gettime)) == 0)
        ;
    
    return res;
}
