#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

#include <coremap.h>
#include <vmc1.h>

/**
 * page_alloc replaces getppages in the kmalloc
*/
void
vm_bootstrap(void) {
    coremap_init();
}

void vm_shutdown(void) {
    coremap_shutdown();
}

void vm_can_sleep(void)
{
    if (CURCPU_EXISTS())
    {
        /* must not hold spinlocks */
        KASSERT(curcpu->c_spinlocks == 0);

        /* must not be in an interrupt handler */
        KASSERT(curthread->t_in_interrupt == 0);
    }
}