#ifndef _PT_H_
#define _PT_H_

/**
 * fields:
 * - n_page: number of pages in our page table
 * - pages: array sized with n_pages in tlb_init() having as i-th item a physical address (if it exists)
*/
struct pagetable {
    unsigned int size;
    paddr_t* pages;
};


struct pagetable *pt = NULL;


void pt_init(void);
void pt_shutdown(void);

#endif