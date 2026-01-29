/**
 * AAAos Kernel - Physical Memory Manager
 *
 * Manages physical memory using a bitmap allocator.
 * Each bit represents one 4KB page frame.
 */

#ifndef _AAAOS_MM_PMM_H
#define _AAAOS_MM_PMM_H

#include "../include/types.h"
#include "../include/boot.h"

/* Page constants */
#define PMM_PAGE_SIZE       4096
#define PMM_PAGE_SHIFT      12

/* Convert between addresses and page frame numbers */
#define PMM_ADDR_TO_PFN(addr)   ((addr) >> PMM_PAGE_SHIFT)
#define PMM_PFN_TO_ADDR(pfn)    ((pfn) << PMM_PAGE_SHIFT)

/**
 * Initialize the physical memory manager
 * @param boot_info Boot information containing memory map
 * @return Total number of usable pages
 */
size_t pmm_init(boot_info_t *boot_info);

/**
 * Allocate physical page frames
 * @param count Number of contiguous pages needed
 * @return Physical address of first page, or 0 on failure
 * @thread_safety Safe to call from any context (uses spinlock internally)
 */
physaddr_t pmm_alloc_pages(size_t count);

/**
 * Allocate a single physical page
 * @return Physical address of page, or 0 on failure
 */
static inline physaddr_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

/**
 * Free previously allocated physical pages
 * @param addr Physical address from pmm_alloc_pages
 * @param count Number of pages to free
 */
void pmm_free_pages(physaddr_t addr, size_t count);

/**
 * Free a single physical page
 * @param addr Physical address from pmm_alloc_page
 */
static inline void pmm_free_page(physaddr_t addr) {
    pmm_free_pages(addr, 1);
}

/**
 * Get total number of physical pages in system
 * @return Total page count
 */
size_t pmm_get_total_pages(void);

/**
 * Get number of free (available) pages
 * @return Free page count
 */
size_t pmm_get_free_pages(void);

/**
 * Get number of used pages
 * @return Used page count
 */
size_t pmm_get_used_pages(void);

/**
 * Mark a range of physical memory as reserved (not allocatable)
 * Used for kernel, MMIO regions, etc.
 * @param addr Start physical address (page-aligned)
 * @param size Size in bytes
 */
void pmm_reserve_range(physaddr_t addr, size_t size);

/**
 * Check if a physical page is free
 * @param addr Physical address (page-aligned)
 * @return true if free, false if used/reserved
 */
bool pmm_is_page_free(physaddr_t addr);

#endif /* _AAAOS_MM_PMM_H */
