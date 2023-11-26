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
void vm_bootstrap(void)
{
    coremap_init();
}

void vm_shutdown(void)
{
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

void vm_fault(int faulttype, vaddr_t faultaddress)
{
    int i, spl;
	uint32_t ehi, elo;
	struct addrspace *as;
    paddr_t pa; 

    if(faulttype == NULL)
        return EFAULT;

    switch (faulttype) {
        case VM_FAULT_READONLY:
            // panic("dumbvm: got VM_FAULT_READONLY\n");
            return EACCES;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }

    if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

    as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

    // look into the pagetable
    pa = pt_get_pa(as->pt, faultaddress);

    // if not exists then allocate a new frame
    if(pa == PFN_NOT_USED) {
        // asks for a new frame from the coremap
        pa = page_alloc(faultaddress);
        // update the pagetable with the new PFN 
        KASSERT((pa & PAGE_FRAME) == pa);
        pt_set_pa(as->pt, faultaddress, pa);
    }
    // otherwise update the TLB
    
/*
    spl = splhigh();

    // this for should be replaced with tlb_probe() and tlb_write()
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = pa | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, pa);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

    kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
*/
	return EFAULT;
}