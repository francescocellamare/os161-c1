#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <types.h>
#include <pt.h>

/**
 * (from elf.h)==========================
 * type: 
 *  PT_LOAD 1 Loadable program segment
 * flags:
 *   PF_R		0x4	Segment is readable 
 *   PF_W		0x2	Segment is writable
 *   PF_X		0x1	Segment is executable
 * 
 * if p_memsz > p_filesz, the leftover space should be zero-filled
 * ======================================
 * 
 * for boundaries of the segment size:
 *  p_vaddr < current va < p_vaddr + p_memsz 
 * 
*/
struct segment {
    uint32_t	p_type;      /* Type of segment */
	uint32_t	p_offset;    /* Location of data within file */
	uint32_t	p_vaddr;     /* Virtual address aka base_addr*/
	uint32_t	p_filesz;    /* Size of data within file */
	uint32_t	p_memsz;     /* Size of data to be loaded into memory*/
	uint32_t	p_permission;   

    /*  
    flow is: 
        miss -> look for boundaries* -> find the segment -> look over pt 
            -> !found, use the address's offset to retrieve data from base_addr + offset
            -> found, use the entry
        
        *boundaries check: for each segment, check if vaddr which missed is inside base_addr and base_addr+p_memsz
    */
};



// for each define operation over a segment checks for avoiding multi-define operations are performed by using KASSERT 

struct segment* seg_create(void);
int seg_define(struct segment* seg, uint32_t p_type, uint32_t p_offset, uint32_t p_vaddr, uint32_t p_filesz, uint32_t p_memsz, uint32_t p_permission);
void seg_destroy(struct segment*);
int seg_define_stack(struct segment*);

#endif