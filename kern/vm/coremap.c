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

/**
 * Lower layer of the whole system, here we manage all the physical pages keeping track of which as they refer to
 * as well as the virtual address of linked page.
 * 
 * LINK /home/os161user/os161/src/kern/vm/kmalloc.c
 * alloc_kpages and free_kpages are the only functions which can handle multiple frames allocation (so alloc_size is going to be
 * different from value 1 ($) ) and they're just called by kernel using kmalloc() and kfree().
 * These two calls, respectively, alloc_kpages and free_kpages which perform frame reservation and removing.
 * 
 * page_alloc() and page_free() work at frame granularity as a normal system managed by TLB should be.
 * 
 * ($) alloc size values:
 *  initial state:              0 0 0 0 0 0 0 0 0 0 0 0
 *  kmalloc -> 3 frames:        3 0 0 0 0 0 0 0 0 0 0 0
 *  page_alloc -> 1 frame:      3 0 0 1 0 0 0 0 0 0 0 0
 * 
 * TODO:
 * 1. swapping
 * 2. check locks
*/

static struct coremap_entry *coremap = NULL; // coremap pointer

static int nRamFrames = 0; // coremap current entries, updated at runtime by calling ram_getsize()

static struct spinlock freemem_lock = SPINLOCK_INITIALIZER;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static int coremapActive = 0; //flag for checking if coremap functionalities are available


static unsigned int current_victim; //chosen victim in case corememory is full


static int isMapActive () {
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

    coremap_size = sizeof(struct coremap_entry) * nRamFrames;

    // coremap is going to be initialized into the kernel space so that we need to `fix` these pages, automatically done by kmalloc
    coremap = kmalloc(coremap_size);
    KASSERT(coremap != NULL);


    // set starting state for the structure
    for(i = 0; i < nRamFrames; i++) {
        coremap[i].status = clean; 
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
        coremap[i].vaddr = 0;
    }

    // let it be usable
    spinlock_acquire(&freemem_lock);
    coremapActive = 1;
    spinlock_release(&freemem_lock);
}

/**
 * Releases the coremap's memory and disables it
*/
void coremap_shutdown() {
    int i;

    spinlock_acquire(&freemem_lock);
    coremapActive = 0;
    // release each page
    for(i = 0; i < nRamFrames; i++) {
        page_free(i*PAGE_SIZE);
    }
    // release the handler
    kfree(coremap);
    spinlock_release(&freemem_lock);
}


/**
 * Same behavior of dumbvm's getfreeppages adapted to the coremap structure
*/
static paddr_t getfreeppages(unsigned long npages) {
    paddr_t addr;	
    long i, first, found;

    if (!isMapActive()) return 0; 
    spinlock_acquire(&freemem_lock);
    for (i=0,first=found=-1; i<nRamFrames; i++) {
        if (coremap[i].status == free) {
        if (i==0 || !coremap[i-1].status == free) 
            first = i;
        if (i-first+1 >= (long)npages) {
            found = first;
            break;
        }
        }
    }
        
    if (found>=0) {
        for (i=found; i<found+(long)npages; i++) {
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

  if (!isMapActive()) return 0; 
  first = addr/PAGE_SIZE;
  KASSERT(nRamFrames>first);

  spinlock_acquire(&freemem_lock);
  for (i=first; i<first+(long)npages; i++) {
    coremap[i].status = free;
    coremap[i].as = NULL;
    coremap[i].alloc_size = 0;
  }
  spinlock_release(&freemem_lock);

  return 1;
}
/**
 * Same behavior of dumbvm's getppages adapted to the coremap structure
*/
static paddr_t getppages(unsigned long npages) {
    unsigned long i;
    paddr_t addr;

    /* try freed pages first */
    addr = getfreeppages(npages);

    // zero is returned if no freed page are available so we steal memory
    if (addr == 0) {
        /* call stealmem for a clean one */
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);
        KASSERT(addr != 0);
    }
    // after stealing memory we MUST have p_addr different from zero (due to ASSERT otherwise system crashes)
    if (addr!=0 && isMapActive()) {
        // update the coremap removing `clean` pages  
        spinlock_acquire(&freemem_lock);
        coremap[addr/PAGE_SIZE].alloc_size = npages;
        coremap[addr/PAGE_SIZE].status = fixed;

        for(i = 1; i < npages; i++) {
            KASSERT( coremap[(addr/PAGE_SIZE)+i].alloc_size == 0 );
            coremap[(addr/PAGE_SIZE)+i].status = fixed;
            // alloc_size is still 0
        }
        spinlock_release(&freemem_lock);
    } 

    return addr;
}
/**
 * Looks for a freed page if available otherwise a new frame is stolen by ram_stealmem. 
 * System is going to crash if there is no memory
*/
static paddr_t getppage_user(vaddr_t va, struct addrspace *as) {
    int found = 0, pos;
    int i;
    unsigned int victim;
    paddr_t pa;

    // looks for a previously freed page using a linear search
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
        // asks for a `clean` one
        spinlock_acquire(&stealmem_lock);
        pa = ram_stealmem(1);
        spinlock_release(&stealmem_lock);

        
       
        //if no physical memory is found we need to choose a victim entry by round robin
        if(pa == 0)
        {
            victim = current_victim;
            current_victim = (current_victim + 1) %  nRamFrames;
            pos = victim 
        }
        else
        { 
            pos = pa / PAGE_SIZE;
        }


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
 * User side, wrapper of getppage_user
*/
paddr_t page_alloc(vaddr_t vaddr) {
    paddr_t pa;
    struct addrspace *as_curr;
    
    if(!isMapActive()) return 0;
    vm_can_sleep();

    as_curr = proc_getas();
    KASSERT(as_curr != NULL);

    //getppage_user we need to check for victim in case no physical address is available
    pa = getppage_user(vaddr, as_curr);
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
    coremap[pos].vaddr = 0;
    spinlock_release(&freemem_lock);

    ///check if dirty swap_out()
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
  if (isMapActive()) {
    paddr_t paddr = addr - MIPS_KSEG0;
    long first = paddr/PAGE_SIZE;	
    KASSERT(nRamFrames>first);
    freeppages(paddr, coremap[first].alloc_size);	


  }
}

