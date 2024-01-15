# os161-c1

## Authors
- Francesco Pio Cellamare - s309530
- Serigne Cheikh Tidiane Sy Fall - s317390
- Inaam ElHelwe - s306979

## Project objective
The project aims at expanding the memory management (dumbvm) module, by fully replacing
it with a more powerful virtual memory manager based on process page tables.

With the current implementation of DUMBVM, the kernel crashes when the TLB is full, our goal is to implement a new virtual-memory system that has:
1. A replacement policy for the TLB
2. On-demand loading of pages so that not the whole pages corresponding to a program will be loaded into memory when the program starts
3. Page replacement for pages in physical memory when no free pages are available
4. Make an application’s text segment read-only

The new virtual memory should use on-demand paging so instead of loading the entire program inside the physical memory, a page is only loaded when needed, so when demanded by a program in execution.  By loading only the portions of program that are needed, memory is used more efficiently.

## Design choices overview
To implement on-demand paging, we adopted the following architectural frameworks for key components within the virtual memory system:
#### TLB Management
- The replacement policy chosen is a simple `Round-Robin` policy
#### Page Table
- Two level page table to reduce the overall memory overhead compared to a single-level page table

#### Page Replacement
- We used a swap file of size 9MB (can be modified) 
- The swaping policy is also `Round-Robin`
#### Read-Only Text segment
- The process will be ended at any attempt to modify its text section


# Implementation
Our main files used to manage the virtual memory are:
- addrspace.c
- coremap.c
- kmalloc.c
- pt.c
- segments.c
- vm_tlb.c
- vmc1.c

We're going discuss more in details the main tasks of each file in the upcoming sections .

## Overview of TLB miss handling
### [vm/vmc1.c](./kern/vm/vmc1.c) & [vm/vm_tlb.c](./kern/vm/vm_tlb.c)

When a TLB miss occurs, `vm_fault()` is called with the virtual address of the address that caused the fault.
First we apply the mask `PAGE_FRAME` to check the page virtual address that triggered the fault.
We check the fault happened in which segment (for more details about [segment](#address-space-and-segments)) to check the permession of this segment, and we check if the page exist in the physical memory or not.
For this part we have many cases:
1. The `pa == PFN_NOT_USED` so no physical address is associated to the page and the page has never been loaded from the disk before (so also the variable `swap_offset = pt_get_offset(as->pt, faultaddress)` is set to -1) the offset variable defined in the page table entry correspoding to the page gives the offset of the page in the `SWAPFILE`  if this page has been loaded from the disk before and then swapped out of the RAM when the RAM was full and a new page was requested. (more about how this work in [swapfile](#swap-file)):
    - Ask for a new frame from the coremap
      using `page_alloc()` that would return a physical address for the corresponding virtual address more information about this step in [page table](#page-table-1), page alloc will also handle the case where the physical memory is full and we need to swap out pages in order to load the new page from the disk.
    - Then we update the page table with the new physical address corresponding to the virtual address
    - If the page belongs to a stack (`seg->p_permission == PF_S`) we zero-it out since in C uninitialized variables are not guaranteed to be set to any particular value. Therefore, if a new page is not zeroed-out before it is used, it may contain arbitrary data that could cause the program to behave unpredictably. By zeroing-out the new page, we ensure that it is initialized to a known state of all zeroes 1
    - If the page does not belong to a stack (`seg->p_permission == PF_S`) we load it to the RAM from the disk with `seg_load_page()` (more about this function in [segments](#address-space-and-segments))
2. If physical address is found but it is swapped out ( `swap_offset = pt_get_offset(as->pt, faultaddress)` the returned value is the offset were the page exist in the `SWAPFILE` so it will be loaded from there):
    - we call `page_alloc()` to allocate a new physical address this physical address is corresponding to the physical address of the swapped out page.
    - Then call `swap_in()` to load it again into the physical memory we receive from `page_alloc()`, we also pass to the function the offset of the page inside the `SWAPFILE` 
    - Then we update the corresponding entry in page table to set the `swap_offset` variable into -1 since the page is no longer in the `SWAPFILE` and the physical address of the page to the newly obtained physical address
    - We check the page table if the victim page exist in the page tabe we set the entry corresponding to it to invalid
    

#### more about [vm/vmc1.c](./kern/vm/vmc1.c)
in `vm_bootstrap()` we initialize the coremap and the swapfile and in `vm_shutdown()` we clean them

#### Read-Only Text Segment

We check for the segment permission if it is read only the dirty bit = 0, no process can write over a page with having a `TLBLO_DIRTY` flag set to 0

```
 if (seg->p_permission == (PF_R | PF_W) || seg->p_permission == PF_S || seg->p_permission == PF_W)
    {
        elo = elo | TLBLO_DIRTY;
    }
```


## Coremap
Coremap is used to manage physical pages. So, we pack a physical page's information into a structure (called struct `coremap_entry`) and use this struct to represent a physical page. We use an array of struct coremap_entry to keep all physical pages information. This array, aka, coremap, will be one of the most important data structure in this project. One entry of this data strucure corresponds to one frame in the physical address. 
```
struct coremap_entry {
    struct addrspace *as;
    enum status_t status;
    vaddr_t vaddr;
    unsigned int alloc_size;
};
```
The status variable can take the following values:
```
enum status_t {
    fixed, //for kernel pages
    free,  //when page is removed from the page table (swapped out)
    dirty, //when user pages
    clean  //the coremap entries are initialized to clean
};
```
The size of the coremap is obtained by:
dividing the RAM size `ram_getsize()` by the size of the page `PAGE_SIZE` we obtain the number of entries
we multiply this value by the size of the struct `coremap_entry`
so the coremap size is the following:
`coremap_size = sizeof(struct coremap_entry) * nRamFrames;`
We initialize the coremap into the Kernel space using `kmalloc()` ,
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
So at first all frames are set to `clean` 
When a process request pages we have to possibility:
- kernel function requesting a number of pages
- User function requesting one page

#### For kernel functions:
To get free pages from the Physical memory we call `getfreeppages()` it's a kernel function, a kernel function can request more than one page.
`getfreeppages()` takes as parameter the number of pages needed, it loops over the ram frames and checks if there's enough memory for  the number of pages requested it returns the free physical address otherewise it returns 0.
This function is called by `getppages()` that checks the return of `getfreeppages()` when the function returns 0, a victim is chosen in the coremap by calling `get_victim_coremap()` the victims in the coremap are chosen by round-robin with  an extra check that the chosen pages are not fixed or clean.
For the chosen victims, we swap them out of the physical memory by calling `swap_out()` and passing the physical and virtual addresses.
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
we call `getppage_user()` that looks in the coremap for a previously freed page using a linear search
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

When a physical page is first allocated for a user process, its state is set to `dirty` , not `clean`. Since this page do not have a copy in `SWAPFILE` (disk). 
Note: Whenever we access the coremap we acquire the `freemem_lock()` and we release it when we finish reading or writing from it.

`getppage_user()` is called by the function `page_alloc()` that  just returns the physical address obtained by `getppage_user()`

the function `page_free()` is called by the `pt_destroy()` to free all the entries of the coremap at the end of a process. While the function `coremap_shutdown()` is called when we want to shutdown the system.


## Page Table
### [vm/pt.c](./kern/vm/pt.c)
The page table is a memory management data structure that organizes the correspondance between the virtual and the physical addresses. We used a two level page table. So we have an outer table of size `1024` and several inner tables of size `1024` also. The structure of tha page table is defined as follows:

```
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

```
So each entry of the inner page table has three fields:
1. valid: tells if this entry is valid and associated to a page having the corresponding virtual address 
2. pfn: which the physical address corresponding to the virtual address of the entry
3. swap_offset: the offset of the page in the `SWAPFILE` if the page has been swapped out of the physical memory or equal to -1 if it's still in physical memory

### How it works:
Given a virtual address, it is composed of 3 parts: 
- p1 : 10 bits index into the outer page table 
- p2 : 10 bits for displacement within the inner page table.
- d  : 12 bits offset inside the page we don't use it since we load the whole  page anyway

We get p1, p2 and d having the following masks: 
- `P1_MASK 0xFFC00000`
- `P2_MASK 0x003FF000`
- `D_MASK 0x00000FFF`

When a user requests a page, the virtual address of the page is used to lookup the page table, this is done by `pt_get_pa()` which is called in `vm_fault()` to check that the page is associated to a physical address. We first get `p1`, we check if the entry corresponding to the offset `p1` inside of the outer page table is valid, if yes, we use `p2` to check if the entry corresponding to the offset `p2` in the inner table is valid, if yes, we return the physical address which is the variable `pfn` . When a virtual address is not found in the page table, we call `page_alloc` that will return a new physical address, then `pt_set_pa()` is called to set an entry for this virtual address, if a new inner table is needed it will be created in this step by calling `pt_define_inner()`.

`pt_define_inner()` sets the valid bit corresponding to the inner table of the page in the outer table to 1 and allocate a new inner table inside of the memory by:
```
pt->pages[index].pages = kmalloc(sizeof(struct pt_inner_entry)*SIZE_PT_INNER);
``` 
then we set all entries of this table to the default values.

- `pt_get_pa()` takes the virtual address of the requested page and returns the correspoding physical address if found or `PFN_NOT_USED` if the page has never been loaded into the physical memory.
- `pt_get_offset()` returns the offset of the page in the `SWAPFILE` if the page has been swapped out of the physical memory or `-1` if not.
`pt_set_offset()` is called when the page is swapped out or swapped in to change its `swap_offset` variable






## Swap file
### [vm/swapfile.c](./kern/vm/swapfile.c)

We created a file called SWAPFILE of size 9MB (can be modified) which is divided into n number of pages with `n = FILE_SIZE / PAGE_SIZE`.

To manage the swapfile we created a list of struct `swap_page` where each entry defines a page in the `SWAPFILE`
```
struct swap_page
{
    paddr_t ppadd;
    vaddr_t pvadd; 
    off_t swap_offset;
    int free; //1: free 0: taken
};
```
and we initialized the following list:
```
static struct swap_page swap_list[NUM_PAGES];
```
in the `swap_init` function we set all the parameters of the swapfile to 0 except the `free` variable we set it to 1 . 
We have two important functions in the `swapfile.c`:
1. `swap_out(paddr_t ppaddr, vaddr_t pvaddr)`
    This function is called whenever we want to remove a page (the victim page) from the physical memory - RAM - and we copy it to the `SWAPFILE`
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
    5. `swap_out()` returns the offset where the page was before removing it from the SWAPFILE this same offset will be passed to the function `swap_in` , since at this offset we get the previously stored page.
2. `swap_in(paddr_t ppadd, vaddr_t pvadd, off_t offset)` 
    This function is called whenever we want to swap a page from the SWAPFILE into the physical memory, so the page is demanded by a process and in the coremap it was flagged as `swapped_out`.
    1. We get the position of the page in the SWAPFILE from 
  
  

## Address space and Segments
### [vm/addrspace.c](./kern/vm/addrspace.c) & [vm/segments.c](./kern/vm/segments.c)

The address space of each program is split into 3 segments:
- Code segment
- Data segment 
- Stack segment

We define a struct address space that contain the three segments and will be allocated to each program when the program starts by calling `as_create()` and destroyed at the end of the program by calling `as_destroy()` :

```
struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t as_npages2;
        paddr_t as_stackpbase;
#else
        struct segment* code;
        struct segment* data;       
        struct segment* stack;
        struct pt_directory *pt;
#endif
};
```
the definition of the struct for our implementation of the virtual memory differ than the one of DUMBVM.

`pt` is a pointer to the struct `pt_directory` that will be used for this program, more about this structure in `pagetable` section

And the struct segment is defined as follows in `segment.h` :

```
struct segment {
    uint32_t	p_type;      /* Type of segment */
	uint32_t	p_offset;    /* Location of data within file where the segment begins at */
	uint32_t	p_vaddr;     /* Virtual address aka base_addr*/
	uint32_t	p_filesz;    /* Size of data within file */
	uint32_t	p_memsz;     /* Size of data to be loaded into memory*/
	uint32_t	p_permission;  /* describes what operations can be performed on the pages of the segment, could have the values */
    struct vnode *vnode;
};
```

for `p_permission` the possible values are defined in `elf.h` and are:
```
/* values for p_flags */
#define	PF_R		0x4	/* Segment is readable */
#define	PF_W		0x2	/* Segment is writable */
#define	PF_X		0x1	/* Segment is executable */
#define PF_S        0x8 /* Segment is stack */

```
So they define the possible actions on the corresponding segment.



when a program starts `as_create()` is called by `loadelf.c` and it allocates kernel space for each segment, for now all the segments are empty.

In `DUMBVM`, When a program starts, `load_segment()` was called to load the whole virtual address space into the physical memory. In our implementation we adapted a different approach, so the function `load_elf()` is called and it defines the address space segments of the program by gettig the information from the `ELF FILE` that will stay open until `as_destroy()` is called, `as_destroy()` free all the segments and the page table associated to the program and it is called at the end of the process.  In `load_elf()` we also call `as_define_region()` passing the permession flags as parameters and the fille handler, these flags are managed inside  `as_define_region()` with bit-wise operations:
```
if(readable)
		perm = perm | PF_R;
	if(writeable)
		perm = perm | PF_W;
	if(executable)
		perm = perm | PF_X;

```
For stack segment, `as_define_stack()` is called in `runprogram()` to define the user stack in the address space, the stack pointer is passed as parameter and it is assigned to the last address inside the user address space (0x80000000). To make sure no overlap would occur between the stack  segments and the other segments, we assign a fixed number of pages (12 can be modified ) for the stack.

The main function we used in `segments.c`  is `seg_load_page()` which is called in `vm_fault()` when a page is requested for the first time, to read it from the file and load to the disk. 
`seg_load_page()` calculates how many pages are needed, then calculates the index of the page inside the segment and the offset we need to add for the fault page. These parameters will be used to fill up the uio structure as needed for handling the fault.

#### more about `addrspace.c` 
As the design choice was to have a per-process TLB  `as_activate()` was called in `runprogram()` to  activate the given address space as the currently in use one, so all TLB entries are deactivated when a context switch is performed.

## Tests and Statistics

To check the new implementation of the virtual memory we ran the following tests:
- palin
- huge
- ctest
- sort
- matmul

To test the kernel we run the following tests:
- at
- at2
- bt
- tlt
- km1
- km2

The following results were obtained:

||Palin|Huge|Ctest|Sort|Matmult|
| :- | :-: | :-: | :-: | :-: | :-: |
|TLB Faults|13918|7574|249425|8483|4824|
|TLB Faults with Free|13918|7574|249425|8483|4824|
|TLB Faults with Replace|0|0|0|0|0|
|TLB Invalidations|8010|7190|251495|4501|1586|
|TLB Reloads|13913|3908|123658|6198|3967|
|Page Faults (zero filled) |1|512|257|289|380|
|Page Faults (disk)|4|3154|125510|1996|477|
|Page Faults from ELF|4|3|3|4|3|
|Page Faults from Swapfile|0|3151|125507|1992|474|
|Swapfile Writes|0|3631|125724|2242|814|

Further more, to test the read-only text segment functionality we set all the segments to read-only.

## Team workload division

About the workload division, at the beginning of the project we started with brainstorming, thinking together of possible solutions and design choices. For this part, we met and worked on the same machine more specifically for the address space and the segments section since they were the base of the rest of the code. 

When the new member joined the team, we gradually started with work division for efficiency. So we split the work into 3 main tasks, Designing the Coremap, the page table and the swapfile, before starting with each of these tasks, we brainstormed, decided on how we want to implement it then each member was assigned one of these tasks, for debugging we also met and fixed the work together.









