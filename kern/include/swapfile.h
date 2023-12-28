#ifndef _SWAPFILE_H
#define _SWAPFILE_H

#include <types.h>

#define FILE_SIZE 9*1024*1024 //9MB
#define NUM_PAGES FILE_SIZE / PAGE_SIZE

struct swap_page
{
    //First initialization of the swap page
    paddr_t ppadd;
    vaddr_t pvadd; //This is used for the swap in
    off_t swap_offset;
    int free; //1: free 0:taken


};

void swapfile_init(void);
int swap_out(paddr_t ppaddr);
int swap_in(paddr_t ppadd, vaddr_t pvadd);
void swap_shutdown(void);
#endif