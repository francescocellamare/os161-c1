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

#include <segments.h>
#include <vmc1.h>


/**
 * Module for segment mgmt, a segment is defined as a section of an address space which is going to be loaded
 * by function load_elf() defined into:
 * LINK /home/os161user/os161/src/kern/syscall/loadelf.c
 * For each segment a segment is defined calling at as_define_region() into load_elf().
 * Each segment is loaded into a `fixed` page in kernel side but it SHOULD NOT crash the whole system.
 * 
 * TODO: 
 * 1. change the load_elf function according to as_define_region prototype in //LINK /home/os161user/os161/src/kern/vm/addrspace.c#define_region
 * 2. 
*/


/**
 * Create a new empty segment struct and set to zero every field (TOBE checked)
*/
struct segment* seg_create(void) {
    struct segment* seg;

    seg = kmalloc(sizeof(struct segment));
    KASSERT(seg != NULL);

    seg->p_type = 0;
    seg->p_offset = 0;
    seg->p_vaddr = 0;
    seg->p_filesz = 0;
    seg->p_memsz = 0;
    seg->p_permission = 0;

    return seg;
}

/**
 * Fill the empty segment struct given in the first parameter and checks it is still empty
*/
int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission) {
    
    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);

    seg->p_type = p_type;
    seg->p_offset = p_offset;
    seg->p_vaddr = p_vaddr;
    seg->p_filesz = p_filesz;
    seg->p_memsz = p_memsz;
    seg->p_permission = p_permission;


    return 0;
}

/**
 * Free the memory of the given segment
*/
void seg_destroy(struct segment* seg) {

    KASSERT(seg != NULL);
    kfree(seg);
}

/**
 * Define a stack segment having as base address the top one of our system due to
 * its way of growing downwards.
 * The size is a function of VMC1_STACKPAGES defined as constant in vmc1.h.
 * Read and write operations are allowed.
*/
int seg_define_stack(struct segment* seg) {

    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);

    seg->p_type = PT_LOAD;          //unused
    seg->p_offset = 0;              //unused
    seg->p_vaddr = USERSTACK;       //base_addr as 0x8000000 (biggest addr of our VM system configuration)
    seg->p_filesz = 0;              //unused
    seg->p_memsz = USERSTACK - VMC1_STACKPAGES * PAGE_SIZE; //stack size is function of VMC1_STACKPAGES 
    seg->p_permission = PF_R | PF_W;                        //read and write should be garanteed 

    return 0;
}

