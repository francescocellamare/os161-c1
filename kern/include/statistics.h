#ifndef STATISTICS_H
#define STATISTICS_H

/* define all required statistics */
#define STATISTICS_TLB_FAULT              0  
#define STATISTICS_TLB_FAULT_FREE         1
#define STATISTICS_TLB_FAULT_REPLACE      2
#define STATISTICS_TLB_INVALIDATE         3
#define STATISTICS_TLB_RELOAD             4
#define STATISTICS_PAGE_FAULT_ZERO        5
#define STATISTICS_PAGE_FAULT_DISK        6
#define STATISTICS_ELF_FILE_READ          7
#define STATISTICS_SWAP_FILE_READ         8
#define STATISTICS_SWAP_FILE_WRITE        9
#define N_STATS                           10


/* Initialize the statistics */
void init_statistics(void);

/* Increment the specified statistic counter */
void increment_statistics(unsigned int stat);

/* Print the statistics */
void print_all_statistics(void);

#endif /* STATISTICS_H */