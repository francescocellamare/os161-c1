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
#include <vnode.h>
#include <uio.h>


/**
 * Module for segment mgmt, a segment is defined as a section of an address space which is going to be loaded
 * by function load_elf() defined into:
 * LINK /home/os161user/os161/src/kern/syscall/loadelf.c
 * For each segment a segment is defined calling at as_define_region() into load_elf().
 * Each segment is loaded into a `fixed` page in kernel side but it SHOULD NOT crash the whole system.
 * 
 * TODO: 
 * 1. change the load_elf function according to as_define_region prototype in //LINK /home/os161user/os161/src/kern/vm/addrspace.c#define_region
*/

void zero(paddr_t paddr, size_t n)
{
    bzero((void *)PADDR_TO_KVADDR(paddr), n);
}

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
    seg->vnode = NULL;

    return seg;
}

/**
 * Fill the empty segment struct given in the first parameter and checks it is still empty
*/
int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission, struct vnode * v) {
    
    KASSERT(seg != NULL);
    KASSERT(seg->p_type == 0);
    KASSERT(seg->p_offset == 0);
    KASSERT(seg->p_vaddr == 0);
    KASSERT(seg->p_filesz == 0);
    KASSERT(seg->p_memsz == 0);
    KASSERT(seg->p_permission == 0);
    KASSERT(seg->vnode == NULL);

    seg->p_type = p_type;
    seg->p_offset = p_offset;
    seg->p_vaddr = p_vaddr;
    seg->p_filesz = p_filesz;
    seg->p_memsz = p_memsz;
    seg->p_permission = p_permission;
    seg->vnode = v;

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
    KASSERT(seg->vnode == NULL);

    seg->p_type = PT_LOAD;          //unused
    seg->p_offset = 0;              //unused
    seg->p_vaddr = USERSTACK - (VMC1_STACKPAGES * PAGE_SIZE);       //base_addr as 0x8000000 (biggest addr of our VM system configuration)
    seg->p_filesz = 0;              //unused
    seg->p_memsz = VMC1_STACKPAGES * PAGE_SIZE;
    seg->p_permission = PF_S;                        //read and write should be garanteed 
    seg->vnode = NULL;

    return 0;
}

int seg_load_page(struct segment* seg, vaddr_t va, paddr_t pa) {
    struct iovec iov;
    struct uio u;
    vaddr_t vbaseoffset, voffset;
    paddr_t dest_paddr;
    size_t read_len, npages;
    off_t file_offset;
    int result;
    unsigned long page_index;

    KASSERT(seg != NULL);
    KASSERT(seg->vnode != NULL);

	npages = seg->p_memsz + (va & ~(vaddr_t)PAGE_FRAME);
	npages = (npages + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = npages / PAGE_SIZE;
    
    if (seg->p_filesz > npages * PAGE_SIZE)
    {
        kprintf("segments.c: warning: segment filesize > segment memsize\n");
        seg->p_filesz = npages * PAGE_SIZE;
    }


    page_index = (va - (seg->p_vaddr & PAGE_FRAME)) / PAGE_SIZE;
    KASSERT(page_index < npages);
    vbaseoffset = seg->p_vaddr & ~(PAGE_FRAME);
    if (page_index == 0)
    {
        dest_paddr = pa + vbaseoffset;
        read_len = (PAGE_SIZE - vbaseoffset > seg->p_filesz) ? seg->p_filesz : PAGE_SIZE - vbaseoffset;
        file_offset = seg->p_offset;
    }
    else if (page_index == (npages) - 1)
    {
        voffset = (npages - 1) * PAGE_SIZE - vbaseoffset;
        dest_paddr = pa;
        file_offset = seg->p_offset + voffset;
        if (seg->p_filesz > voffset)
        {
            read_len = seg->p_filesz - voffset;
        }
        else
        {
            read_len = 0;
            file_offset = seg->p_filesz;
        }
    }
    else
    {
        dest_paddr = pa;
        file_offset = seg->p_offset + (page_index * PAGE_SIZE) - vbaseoffset;
        if (seg->p_filesz > ((page_index + 1) * PAGE_SIZE) - vbaseoffset)
        {
            read_len = PAGE_SIZE;
        }
        else if (seg->p_filesz < (page_index * PAGE_SIZE) - vbaseoffset)
        {
            read_len = 0;
            file_offset = seg->p_filesz;
        }
        else
        {
            read_len = seg->p_filesz - ((page_index * PAGE_SIZE) - vbaseoffset);
        }
    }


    zero(pa, PAGE_SIZE);


    uio_kinit(&iov, &u, (void *)PADDR_TO_KVADDR(dest_paddr), read_len, file_offset, UIO_READ);
    result = VOP_READ(seg->vnode, &u);
    if (result)
    {
        return result;
    }

    if (u.uio_resid != 0)
    {
        /* short read; problem with executable? */
        kprintf("segments.c: short read on segment - file truncated?\n");
        return ENOEXEC;
    }
    return 0;
}

int seg_copy(struct segment *old, struct segment **ret) {
    struct segment *newps;
    int result;
    
    KASSERT(old != NULL);

    newps = seg_create();
    if (newps == NULL) {
        return ENOMEM;
    }

    result = seg_define(newps, old->p_type, old->p_offset, old->p_vaddr, old->p_filesz, old->p_memsz, old->p_permission, old->vnode);
    KASSERT(result == 0);
    
    *ret = newps;
    return 0;
}