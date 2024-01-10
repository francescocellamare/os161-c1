#include <types.h>
#include <lib.h>
#include <synch.h>
#include <spl.h>
#include <statistics.h>

static struct spinlock statistics_spinlock = SPINLOCK_INITIALIZER;
static unsigned int counters[N_STATS];

static const char *statistics_names[] = {
    "TLB Faults",
    "TLB Faults with Free",
    "TLB Faults with Replace",
    "TLB Invalidations",
    "TLB Reloads",
    "Page Faults (Zeroed)",
    "Page Faults (Disk)",
    "Page Faults from ELF",
    "Page Faults from Swapfile",
    "Swapfile Writes",
};

static unsigned int is_active = 0;

void init_statistics(void) {
    int i = 0;

    spinlock_acquire(&statistics_spinlock);

    for (i = 0; i < N_STATS; i++)
    {
        counters[i] = 0;
    }
    is_active = 1;

    spinlock_release(&statistics_spinlock);
}

void increment_statistics(unsigned int stat) {
    spinlock_acquire(&statistics_spinlock);

    if (is_active == 1){
        KASSERT(stat < N_STATS);
        counters[stat] += 1;
    }

    spinlock_release(&statistics_spinlock);
}

void print_all_statistics(void) {
    int i = 0;
    // TLB Faults with Free and TLB Faults with Replace
    int fr = 0;
    // TLB Reloads and Page Faults (Disk) and Page Faults (Zeroed)”
    int tlbr_pfd_pfz = 0;
    // Page Faults from ELF and Page Faults from Swapfile
    int pfelf_pfswp = 0;

    int tlb_faults = 0;
    int pf_disk = 0;

    if (is_active == 0)
        return;

    kprintf("VM STATISTICS:\n");
    for (i = 0; i < N_STATS; i++) {
        kprintf("%25s = %10d\n", statistics_names[i], counters[i]);
    }

    tlb_faults = counters[STATISTICS_TLB_FAULT];
    pf_disk = counters[STATISTICS_PAGE_FAULT_DISK];

    fr = counters[STATISTICS_TLB_FAULT_FREE] + counters[STATISTICS_TLB_FAULT_REPLACE];
    tlbr_pfd_pfz = counters[STATISTICS_TLB_RELOAD] + counters[STATISTICS_PAGE_FAULT_DISK] + counters[STATISTICS_PAGE_FAULT_ZERO];
    pfelf_pfswp = counters[STATISTICS_ELF_FILE_READ] + counters[STATISTICS_SWAP_FILE_READ];
    
    /* consistency assertions */
    //kprintf("STATISTICS TLB Faults with Free + TLB Faults with Replace = %d\n", fr);
    if (tlb_faults != fr)
    {
        kprintf("WARNING: TLB Faults (%d) != TLB Faults with Free + TLB Faults with Replace (%d)\n", tlb_faults, fr);
    }

    //kprintf("STATISTICS TLB Reloads + Page Faults (Zeroed) + Page Faults (Disk) = %d\n", tlbr_pfd_pfz);
    if (tlb_faults != tlbr_pfd_pfz)
    {
        kprintf("WARNING: TLB Faults (%d) != TLB Reloads + Page Faults (Zeroed) + Page Faults (Disk) (%d)\n", tlb_faults, tlbr_pfd_pfz);
    }

    //kprintf("STATISTICS ELF File reads + Swapfile reads = %d\n", pfelf_pfswp);
    if (pf_disk != pfelf_pfswp)
    {
        kprintf("WARNING: ELF File reads + Swapfile reads != Page Faults (Disk) %d\n", pfelf_pfswp);
    }
}