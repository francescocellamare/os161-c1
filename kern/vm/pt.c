#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>

#include <pt.h>
#include <vmc1.h>

/**
 * TLB structure is define into:
 * LINK /home/os161user/os161/src/kern/arch/mips/include/tlb.h
 * 
 * Right away tlb is defined as a global handler to the structure.
 * It has 64 entries to be deactivated at each context switch [as_activate()]
 * 
*/
static int get_p1(vaddr_t va) {
    return va & P1_MASK;
}

static int get_p2(vaddr_t va) {
    return va & P2_MASK;
}

static int get_d(vaddr_t va) {
    return va & D_MASK;
}

/**
 * really, why did I write this?
*/
static unsigned int get_npages(uint32_t memsize, vaddr_t va) {
    unsigned int npages;

    // segment's bytes + offset due to the va 
    npages = memsize + (va & ~PAGE_FRAME);
    // round the number of pages to the nearest integer (ie: 2 pages + x ==> 3, having x < PAGE_SIZE)
    npages = ( (npages + PAGE_SIZE - 1) & PAGE_FRAME ) / PAGE_SIZE;

    return npages;
}

struct pt_directory* pt_create(void) {
    int i;

    struct pt_directory *pt;

    pt = kmalloc(sizeof(struct pt_directory));
    KASSERT(pt != NULL);

    pt->size = SIZE_PT_OUTER;
    pt->pages = kmalloc(sizeof(struct pt_outer_entry)*SIZE_PT_OUTER);
    KASSERT(pt->pages != NULL);

    for(i = 0; i < pt->size; i++) {
        pt->pages[i] = NULL;
        pt->pages[i].valid = 0;
    }

    return pt;
}


static pt_destroy_inner(struct pt_outer_entry pt_inner) {
    
    KASSERT(pt_inner != NULL);
    kfree(pt_inner.pages);
}

void pt_destroy(struct pt_directory* pt) {
    int i;

    KASSERT(pt != NULL);
    for(i = 0; i < pt->size; i++) {
        if(pt->pages[i] != NULL && pt->pages[i].valid) 
            pt_destroy_inner(pt->pages[i]); 
        
    }
    kfree(pt->pages);
    kfree(pt);

}

static void pt_define_inner(struct pt_directory* pt, vaddr_t va) {
    int index, i;

    index = get_p1(va);

    KASSERT(pt->pages[index].valid == 0);

    pt->pages[index].size = SIZE_PT_INNER;
    pt->pages[index].pages = kmalloc(sizeof(struct pt_inner_entry)*SIZE_PT_INNER);
    KASSERT(pt->pages[index] != NULL);

    for(i = 0; i < pt->pages[index].size; i++) {
        pt->pages[index].pages[i].valid = 0;
        pt->pages[index].pages[i].pfn = PFN_NOT_USED;
    }
}

/**
 * Having a virtual address as input, firstly, we look for p1 
 * and p2 for indexing the two-level pagetable used by the 
 * system and then we check for the entry. The following cases
 * may happen:
 * 1. found a page so its PAGE FRAME NUMBER is returned
 * 2. found an invalid page so PFN_NOT_USED constant is returned
 * 3. the outer page table (indexed by p1) doesn't contain a
 * valid entry, this should not occur in standard behavior
*/
paddr_t pt_get_pa(struct pt_directory* pt, vaddr_t va) {
    unsigned int p1, p2, d;

    paddr_t pa;

    p1 = get_p1(va);
    KASSERT(p1 >= 0 && p1 < SIZE_PT_OUTER);

    p2 = get_p2(va);
    KASSERT(p2 >= 0 && p2 < SIZE_PT_INNER);

    d = get_d(va);
    KASSERT(d >= 0 && d < PAGE_SIZE);

    if(pt->pages[p1].valid) {
        if(pt->pages[p1].pages[p2].valid) {
            pa = pt->pages[p1].pages[p2].pfn;
        }
        else {
            return PFN_NOT_USED;
        }
    } else {
        return PFN_NOT_USED;
    }

    return pa;
}

/**
 * This function is going to set a physical address (PFN) into 
 * the pagetable using the given virtual address as the one above
 * defining p1 and p2. If the system wants an entry of a still 
 * `undefined` inner pagetable, it is managed by initializing it  
*/
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa) {
    unsigned int p1, p2, d;

    p1 = get_p1(va);
    KASSERT(p1 >= 0 && p1 < SIZE_PT_OUTER);

    p2 = get_p2(va);
    KASSERT(p2 >= 0 && p2 < SIZE_PT_INNER);

    d = get_d(va);
    KASSERT(d >= 0 && d < PAGE_SIZE);

    if(!pt->pages[p1].valid) {
        pt_define_inner(pt, va);
    }

    // should be valid even after creation
    KASSERT(pt->pages[p1].valid);
    pt->pages[p1].pages[p2].valid = 1;
    pt->pages[p1].pages[p2].pfn = pa;
}