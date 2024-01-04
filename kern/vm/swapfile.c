#include <types.h>
#include <kern/fcntl.h>
#include <vnode.h>
#include <vfs.h>
#include <uio.h>
#include <vm.h>
#include <bitmap.h>
#include <swapfile.h>



// we need a bitmap to keep track of which chunks are full and which are empty in the swapfile
//static struct bitmap *map;
//instead we created a list of entries
static struct swap_page swap_list[NUM_PAGES];




//the swapfile should be accessed by one process at a time so we will need a spinlock
static struct spinlock filelock = SPINLOCK_INITIALIZER;

//initialize the swapfile
//everything is set to 0 at the begining



static struct vnode *v = NULL; // The vnode for the swapfile
static int timesOut = 0;
static int timesIn = 0;

void swapfile_init(void)
{
    int result;
    int i;
    for(i=0; i<NUM_PAGES; i++)
    {
        swap_list[i].ppadd = 0;
        swap_list[i].pvadd = 0;
        swap_list[i].swap_offset = 0;
        swap_list[i].free = 1;
    }
    //if does not exist it will be created
    //The swap file is where all the pages will be written
    //when at run time more than 9MB is needed => panic is called
    kprintf("SWAPFILE INIT\n");
    result = vfs_open((char *)"emu0:/SWAPFILE", O_RDWR | O_CREAT, 0,  &v);
    KASSERT(result == 0);
    return;

   
}

//first we need to perform the swap out
//SWAP OUT: swap out of the Physical address the page and put it in the swap file
int swap_out(paddr_t ppaddr, vaddr_t pvaddr){
//given the physical address of the page to be swapped out
//we return the offset where we save it in the swap file 
    int free_index = -1;
    
    struct iovec iov;
    struct uio u;
    int i;
    struct swap_page *entry;
    off_t page_offset;



    spinlock_acquire(&filelock);
    for(i=0; i< NUM_PAGES; i++)
    {
        entry = &swap_list[i];
        if(entry->free)
        {
            free_index = i;
            break;
        }
    }
    spinlock_release(&filelock);

    if(free_index == -1)
    {
        kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", timesOut, timesIn);
        panic("swapfile.c : Out of swap space \n");
        return -1;
    }

    // kprintf("SWAPOUT %d at pa:0x%x va:0x%x in position %d\n", timesOut, ppaddr, pvaddr, free_index);
    timesOut++;
    page_offset = free_index * PAGE_SIZE;
    KASSERT(page_offset < FILE_SIZE);
    KASSERT((ppaddr & PAGE_FRAME) == ppaddr);

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppaddr), PAGE_SIZE, page_offset, UIO_WRITE);
    VOP_WRITE(v, &u);
    if(u.uio_resid != 0)
    {
        panic("swapfile.c: Cannot write to swap file");
        return -1;
    }
    else{
        // kprintf("Putting pa: 0x%x va: 0x%x at position %d\n", ppaddr, pvaddr, free_index);
        spinlock_acquire(&filelock);
        swap_list[free_index].free = 0;
        swap_list[free_index].ppadd = ppaddr;
        swap_list[free_index].pvadd = pvaddr;
        swap_list[free_index].swap_offset = page_offset;
        spinlock_release(&filelock);

        return swap_list[free_index].swap_offset;
    }



}

//SWAP IN: Swapping from the swap file to the physical memory
//we need to reset the page to free in the swapfile

//the vadd given is the vadd of the 

int swap_in(paddr_t ppadd, vaddr_t pvadd, off_t offset){

    struct iovec iov;
    struct uio u;
    // int i;
    int page_index;
    // off_t new_offset;
    int result;

    // kprintf("SWAPIN %d at pa:0x%x va:0x%x in position %lld\n", timesIn, ppadd, pvadd, offset/PAGE_SIZE);
    timesIn++;

    // page_index = -1;
    // for (i=0; i< NUM_PAGES; i++)
    // {
    //     if(swap_list[i].pvadd == pvadd)
    //     {
    //         page_index = i;
    //         kprintf("FOUND AT POS %d -- looking for va: 0x%x and found va 0x%x (in swapfile)\n", page_index, pvadd, swap_list[i].pvadd);
    //         break;
    //     }
    // } 

    // KASSERT(page_index != -1);
    // new_offset =(off_t)(page_index * PAGE_SIZE);
    KASSERT(offset >= 0);
    KASSERT(pvadd == pvadd);
    page_index = offset/PAGE_SIZE;
    spinlock_acquire(&filelock);
    //fix the swap file descriptor
    swap_list[page_index].ppadd =  0;
    swap_list[page_index].pvadd  = 0;
    swap_list[page_index].free  = 1;
    swap_list[page_index].swap_offset = 0;
    //now copy in its new ppadd
    spinlock_release(&filelock);

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppadd), PAGE_SIZE, offset, UIO_READ);
    result = VOP_READ(v, &u);
    KASSERT(result==0);

    if(u.uio_resid != 0)
    {
        kprintf("Total SWAPOUT: %d -- Total SWAPIN: %d\n", timesOut, timesIn);
        panic("swapfile.c: Cannot read from swap file");
        return -1;
    }
    
    return swap_list[page_index].swap_offset;
    
}


void swap_shutdown(void)
{
   vfs_close(v);

}


int getIn(void) {
    return timesIn;
}
int getOut(void) {
    return timesOut;
}