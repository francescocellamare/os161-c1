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
#include <swapfile.h>
#include <vm_tlb.h>

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

static int get_victim_coremap(int size) {
    int victim = -1;
    int len = 0;

    KASSERT(size != 0);
    while(len < size) {
        // for having contiguous cells in kernel side
        if(current_victim + (size-len) >= (unsigned int)nRamFrames)
            current_victim = 1;

        victim = current_victim;
        current_victim = (current_victim + 1) % nRamFrames;
        if(coremap[victim].status != fixed && coremap[victim].status != clean) {
            len += 1; 
        }
        else len = 0;
    }
    return victim-(len-1); }

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
        //the physical address  = i * PAGE_SIZE
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
static int getfreeppages(unsigned long npages) {
    int addr;	
    volatile long i, first, found;


    if (!isMapActive()) return 0; 
    spinlock_acquire(&freemem_lock);
    first = -1;
    found = -1;
    for (i=1; i<nRamFrames; i++) {
        if (coremap[i].status == free) {
            if (i==0 || coremap[i-1].status != free) {
                first = i;
            }

            if (i-first+1 >= (long)npages) {
                found = first;
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

    return addr; //Returns a free physical address
}

/**
 * Same behavior of dumbvm's freeppages adapted to the coremap structure
*/
static int freeppages(paddr_t addr, unsigned long npages) {
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
    unsigned long i, pos;
    paddr_t addr;
    unsigned int victim;
    volatile paddr_t victim_pa;
    vaddr_t victim_va;
    int result_swap_out;
    struct addrspace* as;
    int result;

    /* try freed pages first */
    addr = getfreeppages(npages);

    // zero is returned if no freed page are available so we steal memory
    if (addr == 0) {
        /* call stealmem for a clean one */
        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);

        if(addr == 0) {

            victim = get_victim_coremap(npages);

            as = proc_getas();
            if (as == NULL) {
                /*
                * Kernel thread without an address space; leave the
                * prior address space in place.
                */
                return 0;
            }

            for(i = 0; i < npages; i++) {
                pos = victim + i;
                //here we should add the call to swap out
                victim_pa = pos * PAGE_SIZE;


                victim_va = coremap[pos].vaddr;
                result_swap_out = swap_out(victim_pa, victim_va);

                pt_set_offset(as->pt, victim_va, result_swap_out);
                pt_set_pa(as->pt, victim_va, 0);
                result = tlb_remove_by_va(victim_va);
                KASSERT(result != -1);
            }
            addr = victim * PAGE_SIZE;

        }
        // KASSERT(addr != 0);
    }
    // after stealing memory we MUST have p_addr different from zero (due to ASSERT otherwise system crashes)
    if (addr!=0 && isMapActive()) {
        // update the coremap removing `clean` pages  
        spinlock_acquire(&freemem_lock);
        coremap[addr/PAGE_SIZE].alloc_size = npages;
        coremap[addr/PAGE_SIZE].status = fixed;

        for(i = 1; i < npages; i++) {
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
    volatile int found = 0, pos;
    int i;
    unsigned int victim;
    paddr_t pa;
    paddr_t victim_pa;
    vaddr_t victim_va;
    int result_swap_out;
    int result;
    

    // looks for a previously freed page using a linear search
    spinlock_acquire(&freemem_lock);
    for(i = 1; i < nRamFrames && !found; i++) {
        if(coremap[i].status == free) {
            found = 1;
            break;
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
            victim = get_victim_coremap(1);
            pos = victim;
            //here we should add the call to swap out
            victim_pa = pos * PAGE_SIZE;

            victim_va = coremap[pos].vaddr;

            result_swap_out = swap_out(victim_pa, victim_va);

            pt_set_offset(as->pt, victim_va, result_swap_out);
            pt_set_pa(as->pt, victim_va, 0);
            
            pa = victim_pa;

            pos = victim_pa / PAGE_SIZE;
            result = tlb_remove_by_va(victim_va);
            KASSERT(result == 0);
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

