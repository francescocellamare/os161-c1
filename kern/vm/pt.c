#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>

#include <pt.h>
#include <vmc1.h>

/**
 * TLB structure is define into:
 * LINK /home/os161user/os161/src/kern/arch/mips/include/tlb.h
 * 
 * Right away tlb is defined as a global handler to the structure.
 * It has 64 entries to be deactivated at each context switch [as_activate()]
 * 
*/
