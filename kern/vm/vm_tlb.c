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


int tlb_check_victim_pa(paddr_t pa_victim, vaddr_t new_va, int state) {
    int i;
	uint32_t ehi, elo;
    int spl;
    int updated = 0;
    spl = splhigh();

    kprintf("NEW: (%x -- %x)\n", new_va, pa_victim);
    for(i = 0; i < NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        kprintf("READ %d: (%x -- %x)\n", i, (ehi & TLBHI_VPAGE)  >> 12, (elo & TLBLO_PPAGE));
        if (elo & TLBLO_VALID) {
            continue;
        }
        if((elo & TLBLO_PPAGE) == pa_victim) {

            ehi = new_va;
            elo = pa_victim | state | TLBLO_VALID;

            tlb_write(ehi, elo, i);
            updated = 1;
        }
    }

    splx(spl);
    return updated;
}

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
