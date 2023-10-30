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

#include <coremap.h>
#include <vmc1.h>

static struct coremap_entry *coremap = NULL; // coremap pointer

static int nRamFrames = 0; // coremap current entries, updated at runtime by calling ram_getsize()

static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static int coremapActive = 0; //flag for checking if coremap functionalities are available

static int isTableActive () {
  int active;
  spinlock_acquire(&freemem_lock);
  active = coremapActive;
  spinlock_release(&freemem_lock);
  return active;
}

/**
 * Allocates the empty coremap and enables it by setting coremapActive
*/
void coremap_init() {
    int i;
    int coremap_size = 0;
    nRamFrames = ((int)ram_getsize())/PAGE_SIZE;  
    KASSERT(nRamFrames > 0);

    coremap_size = sizeof(coremap_entry) * nRamFrames;
    coremap = kmalloc(coremap_size);
    KASSERT(coremap != NULL);

    for(i = 0; i < nRamFrames; i++) {
        coremap[i].status = clean;
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
        coremap[i].vaddr_t = 0;
    }

    spinlock_acquire(&freemem_lock);
    coremapActive = 1;
    spinlock_release(&freemem_lock);
}

/**
 * Releases the coremap's memory and disables it
*/
void coremap_shutdown() {
    int i, res;

    spinlock_acquire(&freemem_lock);
    coremapActive = 0;
    for(i = 0; i < nRamFrames; i++) {
        page_free(i*PAGE_SIZE);
    }
    kfree(coremap);
    spinlock_release(&freemem_lock);
}

/**
 * User side, wrapper of getppage_user
*/
paddr_t page_alloc(vaddr_t vaddr) {
    paddr_t pa;
    struct addrspace *as_cur;

    if(!isTableActive()) return 0;
    vm_can_sleep();

    as_curr = proc_getas();
    KASSERT(as_curr != NULL)

    pa = getppage_user(vaddr, as);
    return pa;
}

/**
 * Looks for a freed page if available otherwise a new frame is stolen by ram_stealmem. 
 * System is going to crash if there is no memory
*/
static getppage_user(vaddr_t va, struct addrspace *as) {
    int found = 0, pos;
    int i;
    paddr_t pa;

    // looks for a previously freed page
    spinlock_acquire(&freemem_lock);
    for(i = 0; i < nRamFrames && !found; i++) {
        if(coremap[i].status == free) {
            found = 1;
        }
    }
    spinlock_release(&freemem_lock);

    if(found) {
        pos = i;
        pa = i*PAGE_SIZE;
    }
    else {
        // asks for a clean one
        spinlock_acquire(&stealmem_lock);
        pa = ram_stealmem(1);
        spinlock_release(&stealmem_lock);

        // here the kernel will crash when there is no more memory
        KASSERT(pa != 0)

        pos = pa / PAGE_SIZE;

    }

    spinlock_acquire(&freemem_lock);
    coremap[pos].as = as;
    coremap[pos].status = dirty;
    coremap[pos].vaddr = va;
    coremap[pos].alloc_size = 1;
    spinlock_release(&freemem_lock);

    return pa;
}

/**
 * User side, makes a page as free state
*/
void page_free(paddr_t addr) {
    int pos;
    
    pos = addr / PAGE_SIZE;

    KASSERT(coremap[pos].status != fixed);
    KASSERT(coremap[pos].status != clean);

    spinlock_acquire(&freemem_lock);
    coremap[pos].status = free;
    coremap[pos].as = NULL;
    coremap[pos].alloc_size = 0;
    coremap[pos].vaddr_t = 0;
    spinlock_release(&freemem_lock);
}

/**
 * Kernel side, wrapper of getppages
*/
vaddr_t alloc_kpages(unsigned long npages) {
	paddr_t pa;

	vm_can_sleep();
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

/**
 * Kernel side, wrapper of freeppages
*/
void free_kpages(vaddr_t addr) {
  if (isTableActive()) {
    paddr_t paddr = addr - MIPS_KSEG0;
    long first = paddr/PAGE_SIZE;	
    KASSERT(nRamFrames>first);
    freeppages(paddr, coremap[first].alloc_size);	
  }
}

/**
 * Same behavior of dumbvm's getppages adapted to the coremap structure
*/
static paddr_t getppages(unsigned long npages) {
    paddr_t addr;

    /* try freed pages first */
    addr = getfreeppages(npages);
    if (addr == 0) {
        /* call stealmem for a clean one */
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
        KASSERT(addr != 0);
    }
    if (addr!=0 && isTableActive()) {
        spinlock_acquire(&freemem_lock);
        coremap[addr/PAGE_SIZE].alloc_size = npages;
        coremap[addr/PAGE_SIZE].status = fixed;

        for(i = 1; i < npages; i++) {
            KASSERT( coremap[(addr/PAGE_SIZE)+i].alloc_size == 0 );
            coremap[(addr/PAGE_SIZE)+i].status = fixed;
        }
        spinlock_release(&freemem_lock);
    } 

    return addr;
}

/**
 * Same behavior of dumbvm's getfreeppages adapted to the coremap structure
*/
static paddr_t getfreeppages(unsigned long npages) {
    paddr_t addr;	
    long i, first, found;

    if (!isTableActive()) return 0; 
    spinlock_acquire(&freemem_lock);
    for (i=0,first=found=-1; i<nRamFrames; i++) {
        if (coremap[i].status == free) {
        if (i==0 || !coremap[i-1].status == free) 
            first = i;
        if (i-first+1 >= npages) {
            found = first;
            break;
        }
        }
    }
        
    if (found>=0) {
        for (i=found; i<found+npages; i++) {
            coremap[i].status = fixed;
            KASSERT(coremap[i].alloc_size == 0);
        }
        coremap[found].alloc_size = npages;
        addr = (paddr_t) found*PAGE_SIZE;
    }
    else {
        addr = 0;
    }

    spinlock_release(&freemem_lock);

    return addr;
}

/**
 * Same behavior of dumbvm's freeppages adapted to the coremap structure
*/
static int 
freeppages(paddr_t addr, unsigned long npages) {
  long i, first;	

  if (!isTableActive()) return 0; 
  first = addr/PAGE_SIZE;
  KASSERT(nRamFrames>first);

  spinlock_acquire(&freemem_lock);
  for (i=first; i<first+npages; i++) {
    coremap[i].status = free;
    coremap[i].as = NULL;
    coremap[i].alloc_size = 0;
  }
  spinlock_release(&freemem_lock);

  return 1;
}