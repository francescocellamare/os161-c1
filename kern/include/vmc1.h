#ifndef _VMC1_H_
#define _VMC1_H_

#include <vm.h>

#define VMC1_STACKPAGES 18

void vm_bootstrap(void);
void vm_can_sleep(void);
void vm_shutdown(void);

// TODO: https://cgi.cse.unsw.edu.au/~cs3231/14s1/lectures/asst3x6.pdf, slide 19
int vm_fault(int faulttype, vaddr_t faultaddress);
void vm_tlbshootdown(const struct tlbshootdown *ts);

#endif