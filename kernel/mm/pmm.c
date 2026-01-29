/**
 * AAAos Kernel - Physical Memory Manager Implementation
 *
 * Uses a bitmap to track page allocation.
 * Each bit = 1 page (4KB), 0 = free, 1 = used.
 */

#include "pmm.h"
#include "../include/serial.h"

/* Maximum supported physical memory (4GB for now) */
#define PMM_MAX_MEMORY      (4ULL * GB)
#define PMM_MAX_PAGES       (PMM_MAX_MEMORY / PMM_PAGE_SIZE)
#define PMM_BITMAP_SIZE     (PMM_MAX_PAGES / 8)

/* Bitmap storage - statically allocated */
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

/* Statistics */
static size_t pmm_total_pages = 0;
static size_t pmm_used_pages = 0;

/* Simple spinlock for thread safety */
static volatile int pmm_lock = 0;

static inline void pmm_acquire_lock(void) {
    while (__sync_lock_test_and_set(&pmm_lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

static inline void pmm_release_lock(void) {
    __sync_lock_release(&pmm_lock);
}

/* Bitmap operations */
static inline void bitmap_set(size_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(size_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline bool bitmap_test(size_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

/**
 * Find first fit of 'count' contiguous free pages
 */
static size_t find_free_pages(size_t count) {
    size_t consecutive = 0;
    size_t start = 0;

    for (size_t i = 0; i < pmm_total_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) {
                start = i;
            }
            consecutive++;
            if (consecutive >= count) {
                return start;
            }
        } else {
            consecutive = 0;
        }
    }

    return SIZE_MAX;  /* Not found */
}

/**
 * Initialize the physical memory manager
 */
size_t pmm_init(boot_info_t *boot_info) {
    kprintf("[PMM] Initializing Physical Memory Manager...\n");

    /* Mark all memory as used initially */
    for (size_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFF;
    }
    pmm_used_pages = PMM_MAX_PAGES;

    /* If no valid boot info, assume minimal memory */
    if (!boot_info_valid(boot_info) || boot_info->mem_map_count == 0) {
        kprintf("[PMM] Warning: No memory map, using defaults\n");

        /* Mark 16MB as usable (above 1MB to skip low memory) */
        size_t start_page = PMM_ADDR_TO_PFN(0x100000);  /* 1MB */
        size_t end_page = PMM_ADDR_TO_PFN(0x1000000);   /* 16MB */

        for (size_t i = start_page; i < end_page && i < PMM_MAX_PAGES; i++) {
            bitmap_clear(i);
            pmm_used_pages--;
        }

        pmm_total_pages = end_page;
        kprintf("[PMM] Default: %llu pages (%llu MB) total, %llu pages free\n",
                (uint64_t)pmm_total_pages,
                (uint64_t)(pmm_total_pages * PMM_PAGE_SIZE / MB),
                (uint64_t)(pmm_total_pages - pmm_used_pages));

        return pmm_total_pages - pmm_used_pages;
    }

    /* Process memory map from bootloader */
    memory_map_entry_t *entries = (memory_map_entry_t*)boot_info->mem_map_addr;
    uint64_t highest_addr = 0;
    size_t usable_count = 0;

    for (uint64_t i = 0; i < boot_info->mem_map_count; i++) {
        memory_map_entry_t *entry = &entries[i];

        /* Track highest address for total page count */
        uint64_t end = entry->base + entry->length;
        if (end > highest_addr) {
            highest_addr = end;
        }

        /* Only process usable memory */
        if (entry->type != MEMORY_TYPE_USABLE) {
            continue;
        }

        /* Calculate page range */
        uint64_t start_addr = ALIGN_UP(entry->base, PMM_PAGE_SIZE);
        uint64_t end_addr = ALIGN_DOWN(entry->base + entry->length, PMM_PAGE_SIZE);

        if (start_addr >= end_addr) {
            continue;
        }

        /* Skip low memory (first 1MB) - BIOS, real mode, etc. */
        if (start_addr < 0x100000) {
            start_addr = 0x100000;
        }

        if (start_addr >= end_addr) {
            continue;
        }

        /* Cap at maximum supported memory */
        if (end_addr > PMM_MAX_MEMORY) {
            end_addr = PMM_MAX_MEMORY;
        }

        /* Mark pages as free */
        size_t start_page = PMM_ADDR_TO_PFN(start_addr);
        size_t end_page = PMM_ADDR_TO_PFN(end_addr);

        for (size_t page = start_page; page < end_page; page++) {
            if (!bitmap_test(page)) {
                continue;  /* Already free */
            }
            bitmap_clear(page);
            pmm_used_pages--;
            usable_count++;
        }
    }

    /* Set total pages based on highest address */
    pmm_total_pages = PMM_ADDR_TO_PFN(MIN(highest_addr, PMM_MAX_MEMORY));

    kprintf("[PMM] Memory map processed:\n");
    kprintf("[PMM]   Total pages: %llu (%llu MB)\n",
            (uint64_t)pmm_total_pages,
            (uint64_t)(pmm_total_pages * PMM_PAGE_SIZE / MB));
    kprintf("[PMM]   Free pages:  %llu (%llu MB)\n",
            (uint64_t)(pmm_total_pages - pmm_used_pages),
            (uint64_t)((pmm_total_pages - pmm_used_pages) * PMM_PAGE_SIZE / MB));
    kprintf("[PMM]   Used pages:  %llu (%llu MB)\n",
            (uint64_t)pmm_used_pages,
            (uint64_t)(pmm_used_pages * PMM_PAGE_SIZE / MB));

    return pmm_total_pages - pmm_used_pages;
}

/**
 * Allocate physical page frames
 */
physaddr_t pmm_alloc_pages(size_t count) {
    if (count == 0) {
        return 0;
    }

    pmm_acquire_lock();

    size_t start = find_free_pages(count);

    if (start == SIZE_MAX) {
        pmm_release_lock();
        kprintf("[PMM] Warning: Failed to allocate %llu pages\n", (uint64_t)count);
        return 0;
    }

    /* Mark pages as used */
    for (size_t i = 0; i < count; i++) {
        bitmap_set(start + i);
        pmm_used_pages++;
    }

    pmm_release_lock();

    physaddr_t addr = PMM_PFN_TO_ADDR(start);
    return addr;
}

/**
 * Free previously allocated physical pages
 */
void pmm_free_pages(physaddr_t addr, size_t count) {
    if (addr == 0 || count == 0) {
        return;
    }

    /* Validate alignment */
    if (!IS_ALIGNED(addr, PMM_PAGE_SIZE)) {
        kprintf("[PMM] Warning: Freeing unaligned address %p\n", (void*)addr);
        return;
    }

    size_t start = PMM_ADDR_TO_PFN(addr);

    pmm_acquire_lock();

    for (size_t i = 0; i < count; i++) {
        size_t page = start + i;
        if (page >= pmm_total_pages) {
            break;
        }

        if (!bitmap_test(page)) {
            kprintf("[PMM] Warning: Double free at page %llu\n", (uint64_t)page);
            continue;
        }

        bitmap_clear(page);
        pmm_used_pages--;
    }

    pmm_release_lock();
}

/**
 * Get total number of physical pages
 */
size_t pmm_get_total_pages(void) {
    return pmm_total_pages;
}

/**
 * Get number of free pages
 */
size_t pmm_get_free_pages(void) {
    return pmm_total_pages - pmm_used_pages;
}

/**
 * Get number of used pages
 */
size_t pmm_get_used_pages(void) {
    return pmm_used_pages;
}

/**
 * Mark a range as reserved
 */
void pmm_reserve_range(physaddr_t addr, size_t size) {
    if (size == 0) {
        return;
    }

    physaddr_t start_addr = ALIGN_DOWN(addr, PMM_PAGE_SIZE);
    physaddr_t end_addr = ALIGN_UP(addr + size, PMM_PAGE_SIZE);

    pmm_acquire_lock();

    for (physaddr_t a = start_addr; a < end_addr; a += PMM_PAGE_SIZE) {
        size_t page = PMM_ADDR_TO_PFN(a);
        if (page >= pmm_total_pages) {
            break;
        }

        if (!bitmap_test(page)) {
            bitmap_set(page);
            pmm_used_pages++;
        }
    }

    pmm_release_lock();
}

/**
 * Check if a page is free
 */
bool pmm_is_page_free(physaddr_t addr) {
    if (!IS_ALIGNED(addr, PMM_PAGE_SIZE)) {
        return false;
    }

    size_t page = PMM_ADDR_TO_PFN(addr);
    if (page >= pmm_total_pages) {
        return false;
    }

    return !bitmap_test(page);
}
