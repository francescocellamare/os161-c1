# os161-c1
## Project objective
The project aims at expanding the memory management (dumbvm) module, by fully replacing
it with a more powerful virtual memory manager based on process page tables

With the current implementation of DUMBVM, the kernel crashes when the TLB is full, our goal is to implement a new virtual-memory system that has: <br>
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
1. The `pa == PFN_NOT_USED` so no physical address is associated to the page and the page has never been loaded from the disk before
    - Ask for a new frame from the coremap
      using `page_alloc` that would return a physical address for the corresponding virtual address more information about this step in /LINK/, page alloc will also handle the case where the physical memory is full and we need to swap out pages in order to swap in the new page
    - Then we update the page table with the new physical address corresponding to the virtual address
    - if the page belongs to a stack we zero-it out since in C uninitialized variables are not guaranteed to be set to any particular value. Therefore, if a new page is not zeroed-out before it is used, it may contain arbitrary data that could cause the program to behave unpredictably. By zeroing-out the new page, we ensure that it is initialized to a known state of all zeroes 1
2. If physical address is found but it is swapped out (by checking the corresponding flag) we call `page_alloc` to allocate a new physical address for the page and then `swap_in` to load it again into the physical memory







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


