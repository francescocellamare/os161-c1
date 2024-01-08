#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>

#include <elf.h>
#include <coremap.h>
#include <vmc1.h>
#include <mips/tlb.h>
#include <vm_tlb.h>


int tlb_remove_by_va(vaddr_t va) {
    int spl, index;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return -1;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

    index = tlb_probe(va, 0);
    if(index >= 0)
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
	
	splx(spl);
    return 0;
}
