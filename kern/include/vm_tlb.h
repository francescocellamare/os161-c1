#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <types.h>

int tlb_check_victim_pa(paddr_t pa_victim, vaddr_t new_va, int state);
#endif