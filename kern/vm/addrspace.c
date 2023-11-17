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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>

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
 * -as_activate
 * -as_deactivate
 * -as_prepare_load
 * -as_complete_load
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

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// new `fixed` pages will be created for each of them
	as->code = seg_create();
	as->data = seg_create();
	as->stack = seg_create();

	return as;
}

/**
 * Copy an address space into another, no idea why it should be used
*/
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	newas->code = old->code;
	newas->data = old->data;
	newas->stack = newas->stack;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	KASSERT(as != NULL);
	seg_destroy(as->code);
	seg_destroy(as->data);
	seg_destroy(as->stack);

	kfree(as);
}

/**
 * This function activates a given address space as the currently in use one
 * so we deactivate all the TLB's entries when a context switch is performed
*/
void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
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
		 uint32_t filesz, int readable, int writeable, int executable, int segNo)
{
	int res = 1;
	int perm = 0x0;

	if(readable)
		perm = perm | PF_R;
	if(writeable)
		perm = perm | PF_W;
	if(executable)
		perm = perm | PF_X;

	if(res == 0)
		res = seg_define(as->code, type, offset, vaddr, filesz, memsize, perm);
	else if(res == 1)
		res = seg_define(as->data, type, offset, vaddr, filesz, memsize, perm);
	
	KASSERT(res == 0);	// segment defined correctly
	return res;
}

/**
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
 * No idea, called after each segment load in memory but 
 * this should not be perfomed in demand paging
*/
int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

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

	KASSERT(res == 0) 
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

