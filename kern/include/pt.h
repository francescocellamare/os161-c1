#ifndef _PT_H_
#define _PT_H_

/*
    addr (32 bits): p1 | p2 | d
    p1: 10 bits, indexing inner page table
    p2: 10 bits, indexing entries into the inner page table
    d: 12 bits, offset
*/ 
#define SIZE_PT_OUTER 1024
#define SIZE_PT_INNER 1024

#define P1_MASK 0xFFC00000
#define P2_MASK 0x003FF000
#define D_MASK 0x00000FFF
#define PFN_NOT_USED 0x00000000

struct pt_inner_entry {
    unsigned int valid;
    paddr_t pfn;
    off_t swap_offset; 
};
struct pt_outer_entry {
    unsigned int valid;
    unsigned int size;
    struct pt_inner_entry* pages;
};
struct pt_directory {
    unsigned int size;
    struct pt_outer_entry* pages;
};


/**
 * pt->
 *      pages[p1]->
 *      (if valid) pages[p2]->
 *      (if valid) pfn
*/

/*
    Create the directory pt having each inner table as NULL pointer till it's not used (VALID == 0) 
*/
struct pt_directory* pt_create(void);

/*
    Static function which is called whenever a new inner pt is needed (so it becames valid)
*/
void pt_define_inner(struct pt_directory* pt, vaddr_t va);

/*
    Free the whole structure
*/
void pt_destroy(struct pt_directory* pt);

/*
    Free the given inner table
*/
void pt_destroy_inner(struct pt_outer_entry pt_inner);

/*
    Get the physical address having a virtual address, PFN_NOT_USED if it is not valid
*/
int pt_get_pa(struct pt_directory* pt, vaddr_t va);

/*
    Get the swapped out flag  having a virtual address, 2 if it is not valid
*/

off_t pt_get_offset(struct pt_directory* pt, vaddr_t va);


/*
    Set the offset having a virtual address 
*/

void pt_set_offset(struct pt_directory* pt, vaddr_t va, off_t offset);


/*
    Set the physical address having a virtual address, new inner table allocation is managed
*/
void pt_set_pa(struct pt_directory* pt, vaddr_t va, paddr_t pa);

#endif