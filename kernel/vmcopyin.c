#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
p->kernel pagetable and p->user pagetable shoud have the same mapping
k doesn't have PTE_U set, u sets PTE_U
the mapping should below PLIC
the user process has no alloc(), they are grown with sbrk
*/
int 
copyin_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{   
    if (srcva + len < srcva || (uint64)dst + len < (uint64)dst) {
        return -1;
    }
    if (srcva + len >= PLIC) {
        return -1;
    }
    uint64 pa0;
    for (uint64 va0 = PGROUNDDOWN(srcva); va0 < PGROUNDUP(srcva + len); va0 += PGSIZE) {
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
    }
    memmove(dst, (char *)srcva, len);
    return 0;
}


uint64
get_length_to_null(pagetable_t pagetable, uint64 srcva, uint64 max)
{
    int i = 0;
    uint64 pa0, va0;
    char *p;
    for (i = 0; i < max; i++) {
        va0 = srcva + i;
        if (va0 < srcva || va0 >= PLIC) {
            return -1;
        }
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        p = (char *) (va0);
        if (*p == '\0') {
            break;
        }
    }
    if (i == max) {
        return -1;
    }
    return i + 1;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int 
copyinstr_new(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    uint64 len = get_length_to_null(pagetable, srcva, max);
    if (len < 0) {
        return -1;
    }
    if ((uint64)dst + len < (uint64)dst) {
        return -1;
    }
    memmove(dst, (char *)srcva, len);
    return 0;
}