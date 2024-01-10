/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>
#include <vfs.h>
#include <mips/tlb.h>

#include <swapfile.h>
#include <coremap.h>
#include <vm_tlb.h>
#include <vmc1.h>
#include <statistics.h>


/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 * 
 * To run the default DUMBVM just set to 1 opt-dumbvm.h option
 */


/**
 * 
 * TODO: 
 * -manage what it's going to happen when an as is freed (ie set to free the coremap)
 * - //LINK ./addrspace.c#as_activate
*/

/**
 * This function allocates space, in the kernel, for a structure that does the bookkeeping for a single address space. 
 * It does NOT allocate space for the stack, the program binary, etc., just the structure that hold information 
 * about the address space. 
*/
struct addrspace *
as_create(void)
{
	struct addrspace *as;
	// coremap_turn_on();
	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// new `fixed` pages will be created for each of them
	as->code = seg_create();
	as->data = seg_create();
	as->stack = seg_create();
	as->pt = pt_create();
	swapfile_init();

	return as;
}

/**
 * Copy an address space into another
*/
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	int result;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	result = seg_copy(old->code, &newas->code);
	KASSERT(result == 0);
	result = seg_copy(old->data, &newas->data);
	KASSERT(result == 0);
	result = seg_copy(old->stack, &newas->stack);
	KASSERT(result == 0);
	newas->pt = old->pt;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	struct vnode *v;

	KASSERT(as != NULL);

	kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", getOut(), getIn());
	v = as->code->vnode;
	seg_destroy(as->code);
	seg_destroy(as->data);
	seg_destroy(as->stack);
	pt_destroy(as->pt);
	vfs_close(v);
	kfree(as);
}

/**
 * ANCHOR[id=as_activate]
 * This function activates a given address space as the currently in use one
 * so we deactivate all the TLB's entries when a context switch is performed
*/
void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	increment_statistics(STATISTICS_TLB_INVALIDATE);

	splx(spl);
	
}

void
as_deactivate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	increment_statistics(STATISTICS_TLB_INVALIDATE);

	splx(spl);
	
}
/*
 * ANCHOR[id=define_region] 
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 * 
 * All of these parameters can be taken by reading related elf file,
 * calling this we can just defined CODE and DATA sections, stack one
 * must be managed separately (using as_define_stack) because we do not
 * need such parameters for a stack region, all is defined into function
 * seg_define_stack()
 */
int
as_define_region(struct addrspace *as, uint32_t type, uint32_t offset ,vaddr_t vaddr, size_t memsize,
		 uint32_t filesz, int readable, int writeable, int executable, int segNo, struct vnode *v)
{
	int res = 1;
	int perm = 0x0;

	KASSERT(segNo < 2);

	if(readable)
		perm = perm | PF_R;
	if(writeable)
		perm = perm | PF_W;
	if(executable)
		perm = perm | PF_X;

	if(segNo == 0)
		res = seg_define(as->code, type, offset, vaddr, filesz, memsize, perm, v);
	else if(segNo == 1)
		res = seg_define(as->data, type, offset, vaddr, filesz, memsize, perm, v);
		
	KASSERT(res == 0);	// segment defined correctly
	return res;
}
/**
 * ANCHOR[id=prepare_load]
 * No idea, called after as_define_region() in load_elf()
*/
int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

/**
 * ANCHOR[id=complete_load]
 * No idea, called after each segment load in memory but 
 * this should not be perfomed in demand paging
*/
int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

/**
 * Hands back the initial stack pointer for the new process
 * so old data are going to be overwritten/zeroed
 * 
 * stackptr is a pointer to be setted and used by the caller 
*/
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	int res;

	res = seg_define_stack(as->stack);

	KASSERT(res == 0); 
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	(void)as;

	return 0;
}

struct segment* as_get_segment(struct addrspace *as, vaddr_t va) {
	
	KASSERT(as != NULL);

	uint32_t base_seg1, top_seg1;
	uint32_t base_seg2, top_seg2;
	uint32_t base_seg3, top_seg3;

	base_seg1 = as->code->p_vaddr;
	top_seg1 = ( as->code->p_vaddr + as->code->p_memsz);

	base_seg2 = as->data->p_vaddr;
	top_seg2 = ( as->data->p_vaddr + as->data->p_memsz);

	base_seg3 = as->stack->p_vaddr;
	top_seg3 = USERSTACK;

	if(va >= base_seg1 && va <= top_seg1) {
		return as->code;
	}
	else if(va >= base_seg2 && va <= top_seg2) {
		return as->data;
	}
	else if (va >= base_seg3 && va <= top_seg3) {
		return as->stack;
	}
	return NULL;
}
