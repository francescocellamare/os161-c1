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

#include <elf.h>
#include <coremap.h>
#include <vmc1.h>

static unsigned int current_victim;

static unsigned int tlb_get_rr_victim(void) {
    unsigned int victim;
    victim = current_victim;

    
    current_victim = (current_victim + 1) % NUM_TLB;
    return victim;
}

/**
 * page_alloc replaces getppages in the kmalloc
 */
void vm_bootstrap(void)
{
    coremap_init();
    current_victim = 0;
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

int vm_fault(int faulttype, vaddr_t faultaddress)
{
    int i, spl, found, new_page, result;
    unsigned int victim;
	uint32_t ehi, elo;
	struct addrspace *as;
    paddr_t pa; 
    struct segment * seg;
    vaddr_t pageallign_va;

    // todo
   	pageallign_va = faultaddress & PAGE_FRAME;


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

    new_page = -1;
    seg = as_get_segment(as, faultaddress);
    if (seg == NULL)
    {
        return EFAULT;
    }
    // segment found

    // look into the pagetable
    pa = pt_get_pa(as->pt, faultaddress);

    // if not exists then allocate a new frame
    if(pa == PFN_NOT_USED) {
        // asks for a new frame from the coremap
        pa = page_alloc(pageallign_va);
        // update the pagetable with the new PFN 
        KASSERT((pa & PAGE_FRAME) == pa);
        pt_set_pa(as->pt, faultaddress, pa);

        if (seg->p_permission == PF_S)
        {
            zero(PADDR_TO_KVADDR(pa), PAGE_SIZE);
        }

        new_page = 1;
    }

    if(new_page == 1 && seg->p_permission != PF_S) {
        result = seg_load_page(seg, faultaddress, pa); 
        if (result)
            return EFAULT;
    }    


    // otherwise update the TLB
    spl = splhigh();


    found = tlb_probe(faultaddress, 0);
    if(found < 0) {
        // not found
        for (i=0; i<NUM_TLB; i++) {
            tlb_read(&ehi, &elo, i);
            if (elo & TLBLO_VALID) {
                continue;
            }
            ehi = pageallign_va;
            elo = pa | TLBLO_DIRTY | TLBLO_VALID;
            DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, pa);
            tlb_write(ehi, elo, i);
            splx(spl);
            return 0;
        }
        // choose a victim

        ehi = faultaddress;
        elo = pa | TLBLO_DIRTY | TLBLO_VALID;
        victim = tlb_get_rr_victim();
        tlb_write(ehi, elo, victim);
        return 0;
    }
    // else found ==> duplication so not needed


    kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);

	return EFAULT;
}

//TODO - Just a copy of the dumbvm one till now
void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}