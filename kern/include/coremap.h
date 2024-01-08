#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <addrspace.h>
#include <types.h>

/**
 * fixed: requested by kernel
 * free: freed and now available
 * dirty: requested by a user program
 * clean: still no required by ram_stealmem
*/
enum status_t {
    fixed,
    free,
    dirty,
    clean
};
/**
 * vaddr in [0x80000000, 0x80000000+ram_size]
*/
struct coremap_entry {
    struct addrspace *as;
    enum status_t status;
    vaddr_t vaddr;
    unsigned int alloc_size;
};

void coremap_init(void);
void coremap_shutdown(void);

// for user
paddr_t page_alloc(vaddr_t vaddr);
void page_free(paddr_t paddr);

// for kernel
vaddr_t alloc_kpages(unsigned long npages);
void free_kpages(vaddr_t addr);

#endif