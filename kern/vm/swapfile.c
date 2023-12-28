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


void swapfile_init(void)
{
    int result;

    //if does not exist it will be created
    //The swap file is where all the pages will be written
    //when at run time more than 9MB is needed => panic is called
    result = vfs_open((char *)"./SWAPFILE", O_RDWR | O_CREAT , 777, &v);
    KASSERT(result == 0);
    return;

   
}

//first we need to perform the swap out
//SWAP OUT: swap out of the Physical address the page and put it in the swap file
int swap_out(paddr_t ppaddr){
//given the physical address of the page to be swapped out
//we return the offset where we save it in the swap file 
    int free_index = -1;
    
    struct iovec iov;
    struct uiov u;
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
    if(free_index == -1)
    {
        panic("swapfile.c : Out of swap space \n");
        return -1;
    }

    page_offset = free_index * PAGE_SIZE;
    KASSERT(page_offset < FILE_SIZE);

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppaddr), PAGE_SIZE, page_offset, UIO_WRITE);
    VOP_WRITE(v, &u);
    if(u.uio_resid != 0)
    {
        panic("swapfile.c: Cannot write to swap file");
        return -1;
    }
    else{
        return 0;
    }



}

//SWAP IN: Swapping from the swap file to the physical memory
//we need to reset the page to free in the swapfile

//the vadd given is the vadd of the 

int swap_in(paddr_t ppadd, vaddr_t pvadd){

    unsigned int swap_index;
    struct iovec iov;
    struct uio u;
    int i;
    int page_index;
    off_t new_offset;


    page_index = -1;
    for (i=0; i< NUM_PAGES; i++)
    {
        if(swap_list[i].pvadd == pvadd)
        {
            page_index = i;
            break;
        }
    } 

    KASSERT(page_index != -1);
    new_offset =(off_t)(page_index * PAGE_SIZE); //how to transform int variable to offset

    spinlock_acquire(&filelock);
    //fix the swap file descriptor
    swap_list[page_index].ppadd =  NULL;
    swap_list[page_index].vadd  = NULL;
    swap_list[page_index].free  = 1;
    //now copy in its new ppadd

    uio_kinit(&iov, &u, (void *) PADDR_TO_KVADDR(ppadd), PAGE_SIZE, new_offset, UIO_READ);
    VOP(v, &u);
    KASSERT(u.uio_resid ! = 0);

    spinlock_release(&filelock);

    return 0;
}


void swap_shutdown(void)
{
   vfs_close(v);

}