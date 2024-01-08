# os161-c1
## Project objective
The project aims at expanding the memory management (dumbvm) module, by fully replacing
it with a more powerful virtual memory manager based on process page tables

With the current implementation of DUMBVM, the kernel crashes when the TLB is full, our goal is to implement a new virtual-memory system that has:
1. A replacement policy for the TLB
2. On-demand loading of pages so that not the whole pages corresponding to a program will be loaded into memory when the program starts
3. Page replacement for pages in physical memory when no free pages are available

## Design choices overview
#### TLB Management
- The replacement policy chosen is a simple `Round-Robin` policy
#### Page Table
- Two level page table to reduce the overall memory overhead compared to a single-level page table

#### Page Replacement
- We used a swap file of size 9MB (can be modified) 
- The swaping policy is also `Round-Robin`

# Implementation
## TLB Fault
### [vm/vmc1.c](./kern/vm/vmc1.c) & [vm_tlb.c](./kern/vm/vm_tlb.c)

When a page fault occurs in the TLB, `vm_fault` is called with the virtual address of the address that cause the fault.
first we apply the mask `PAGE_FRAME` to check the page virtual address that triggered the fault.
We check the fault happened in which segment (for more details about segment /link/) to check the permession of this segment.
For this part we have many possibilities:
1. The `pa == PFN_NOT_USED` so no physical address is associated to the page and the page has never been loaded from the disk before (so also the variable `swapped_out = pt_get_state(as->pt, faultaddress)` is set to -1)
    - Ask for a new frame from the coremap
      using `page_alloc` that would return a physical address for the corresponding virtual address more information about this step in /LINK/, page alloc will also handle the case where the physical memory is full and we need to swap out pages in order to swap in the new page
    - Then we update the page table with the new physical address corresponding to the virtual address
    - If the page belongs to a stack (`seg->p_permission == PF_S`) we zero-it out since in C uninitialized variables are not guaranteed to be set to any particular value. Therefore, if a new page is not zeroed-out before it is used, it may contain arbitrary data that could cause the program to behave unpredictably. By zeroing-out the new page, we ensure that it is initialized to a known state of all zeroes 1
    - If the page does not belong to a stack (`seg->p_permission == PF_S`) we load it to the RAM from the disk with `seg_load_page`
2. If physical address is found but it is swapped out (by checking the corresponding flag: `swapped_out >= 0 `) we call `page_alloc` to allocate a new physical address for the page and then `swap_in` to load it again into the physical memory

### More about [vm/vmc1.c](./kern/vm/vmc1.c)
in `vm_bootstrap` we initialize the coremap and the swapfile and in `vm_shutdown` we clean them

## Coremap
Coremap is used to manage physical pages. So, we pack a physical page's information into a structure (called struct `coremap_entry`) and use this struct to represent a physical page. We use an array of struct coremap_entry to keep all physical pages information. This array, aka, coremap, will be one of the most important data structure in this project.
```
struct coremap_entry {
    struct addrspace *as;
    enum status_t status;
    vaddr_t vaddr;
    unsigned int alloc_size;
};
```
The size of the coremap is obtained by:
dividing the RAM size `ram_getsize()` by the size of the page `PAGE_SIZE` we obtain the number of entries
we multiply this value by the size of the struct `coremap_entry`
so the coremap size is the following:
`coremap_size = sizeof(struct coremap_entry) * nRamFrames;`
We initialize the coremap into the Kernel space using `kmalloc`
`coremap = kmalloc(coremap_size)`

We use this value to allocate 
When initializing the coremap we set all the entries to the default values:
```
for(i = 0; i < nRamFrames; i++) {
        coremap[i].status = clean; 
        coremap[i].as = NULL;
        coremap[i].alloc_size = 0;
        coremap[i].vaddr = 0; 
        //the physical address  = i * PAGE_SIZE
    }
```
When a process request pages we have to possibility:
- kernelfunction requesting a number of pages
- User function requesting one page

#### For kernel functions:
To get free pages from the Physical memory we call `getfreeppages` it's a kernel function 
which takes as parameter the number of pages needed, it loops over the ram frames and checks if there's enough memory for  the number of pages requested it returns the free physical address otherewise it returns 0
This function is called by `getppages` that checks the return of `getfreeppages` when the function returns 0, a victim is chosen in the TLB by calling `get_victim_coremap` the victims in the coremap are chosen by round-robin and  an extra check that the chosen pages are not fixed or clean.
For the chosen victims, we swap them out of the physical memory by calling `swap_out` and passing the physical and virtual addresses.
We then update the coremap structure with the new values.
```
coremap[addr/PAGE_SIZE].alloc_size = npages;
coremap[addr/PAGE_SIZE].status = fixed;

 for(i = 1; i < npages; i++) 
 {
    coremap[(addr/PAGE_SIZE)+i].status = fixed;
    // alloc_size is still 0
}
```
fixed because those pages belong to the kernel.

#### For user functions:
Now for getting a page for the user we follow a similar approch, 
we call `getppage_user` that looks in the coremap for a previously freed page using a linear search
```
 for(i = 0; i < nRamFrames && !found; i++) {
        if(coremap[i].status == free) {
            found = 1;
            break;
        }
    }
```
If a free page is found, the function returns the corresponding physical address. Otherwise, we look in the coremap for a clean page  instead, if also no clean page is found we use round-robin for choosing a victim and we call swap_out to remove the victim from the physical memory.
we take the position of the victim in the coremap by diving the physical address by the `PAGE_SIZE` we update the coremap in this position:
```
 spinlock_acquire(&freemem_lock);
    coremap[pos].as = as;
    coremap[pos].status = dirty;
    coremap[pos].vaddr = va;
    coremap[pos].alloc_size = 1;
    spinlock_release(&freemem_lock);

    return pa;
```

Note: Whenever we access the coremap we acquire the `freemem_lock` and we release it when we finish reading or writing from it.

`getppage_user` is called by the function `page_alloc` that  just returns the physical address obtained by `getppage_user`

the function `page_free` is just called by the `coremap_shutdown` to free all the entries of the coremap at the end of the process

## Page Table
### [vm/pt.c](./kern/vm/pt.c)
The page table is a memory management data structure that organizes the correspondance between the virtual and the physical addresses. We used a two level page table.
```
struct pt_inner_entry {
    unsigned int valid;
    paddr_t pfn;
    unsigned int swapped_out; 
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

```
### How it works:
Given a virtual address, it is composed of 3 parts: 
- p1 : 10 bits indexing the inner page table
-  p2 : 10 bits indexing entries into the inner page table 
- d  : 12 bits offset

We look for p1 and p2 having the following masks: 
- `P1_MASK 0xFFC00000`
- `P2_MASK 0x003FF000`
- `D_MASK 0x00000FFF`

The following cases may happen:
  1. found a page so its PAGE FRAME NUMBER is returned
  2. found an invalid page so PFN_NOT_USED constant is returned
  3. the outer page table (indexed by p1) doesn't contain a  valid entry, this should not occur in standard behavior

Each page has 2 state identifiers:
- valid: to check wether this page has been used before or not
- swapped_out: used later when doing the swapping to check wether this page was swapped out of the physical memory and is now in the swap file

### Victim selection

When no more free frames are available, so the physical memory is full we need to perform the swap out of a page into the swap file.

the main function for the page table is `pt_get_pa` that takes the `va` that resulted in the TLB fault and returns the physical address if found or `PFN_NOT_USED` if it was not used before and should be loaded from the disk

## Swapfile
### [vm/swapfile.c](./kern/vm/swapfile.c)

We created a file called SWAPFILE of size 9MB (can be modified) which is divided into n number of pages with `n = FILE_SIZE / PAGE_SIZE`.

To manage the swapfile we created a list of struct `swap_page` where each entry defines a page in the `SWAPFILE`
```
struct swap_page
{
    paddr_t ppadd;
    vaddr_t pvadd; 
    off_t swap_offset;
    int free; //1: free 0:taken
};
```
and we initialized the following list:
```
static struct swap_page swap_list[NUM_PAGES];
```
in the `swap_init` function we set all the parameters of the swapfile to 0 except the `free` variable we set it to 1. 
We have two important functions in the `swapfile.c`:
1. `swap_out(paddr_t ppaddr, vaddr_t pvaddr)`
    This function is called whenver we want to remove a page (the victim page) from the physical memory - RAM - and we copy it to the SWAPFILE
    1. Look for the first free position in the SWAPFILE
        ```
        for(i=0; i< NUM_PAGES; i++)
    {
        entry = &swap_list[i];
        if(entry->free)
        {
            free_index = i;
            break;
        }
    }
    ```
    2. If no more free pages are available return the following panic:
    ```
    panic("swapfile.c : Out of swap space \n");
    ```
    3. the offset inside of the `SWAPFILE` Where the page will be loaded is given by: `page_offset = free_index * PAGE_SIZE`
    4. update the `swap_list` and set the `free` variable to 0 so the entry is no more available
    ```
    swap_list[free_index].free = 0;
    swap_list[free_index].ppadd = ppaddr;
    swap_list[free_index].pvadd = pvaddr;
    swap_list[free_index].swap_offset = page_offset;
    ```
    5. `swap_out` returns a function that would be later on passed to `swap_in` 
2. `swap_in(paddr_t ppadd, vaddr_t pvadd, off_t offset)` 
    This function is called whenever we want to swap a page from the SWAPFILE into the physical memory, so the page is demanded by a process and in the coremap it was flagged as `swapped_out`.
    1. We get the position of the page in the SWAPFILE from 
    
    





