/**
 * AAAos Kernel - Virtual Memory Manager
 *
 * Manages x86_64 4-level paging:
 *   PML4 (Page Map Level 4) -> PDPT (Page Directory Pointer Table)
 *   -> PD (Page Directory) -> PT (Page Table)
 *
 * Each level contains 512 entries (9 bits each).
 * Virtual address breakdown (48-bit canonical):
 *   [63:48] Sign extension (must match bit 47)
 *   [47:39] PML4 index (9 bits)
 *   [38:30] PDPT index (9 bits)
 *   [29:21] PD index (9 bits)
 *   [20:12] PT index (9 bits)
 *   [11:0]  Page offset (12 bits)
 */

#ifndef _AAAOS_MM_VMM_H
#define _AAAOS_MM_VMM_H

#include "../include/types.h"

/* Page table entry flags (bits 0-11 of each entry) */
#define VMM_FLAG_PRESENT        BIT(0)   /* Page is present in memory */
#define VMM_FLAG_WRITE          BIT(1)   /* Page is writable */
#define VMM_FLAG_USER           BIT(2)   /* User-mode accessible */
#define VMM_FLAG_WRITETHROUGH   BIT(3)   /* Write-through caching */
#define VMM_FLAG_NOCACHE        BIT(4)   /* Disable caching */
#define VMM_FLAG_ACCESSED       BIT(5)   /* Page has been accessed */
#define VMM_FLAG_DIRTY          BIT(6)   /* Page has been written to */
#define VMM_FLAG_HUGE           BIT(7)   /* Huge page (2MB in PD, 1GB in PDPT) */
#define VMM_FLAG_GLOBAL         BIT(8)   /* Global page (not flushed on CR3 switch) */
#define VMM_FLAG_NX             BIT(63)  /* No-execute (requires NX bit enabled) */

/* Common flag combinations */
#define VMM_FLAGS_KERNEL        (VMM_FLAG_PRESENT | VMM_FLAG_WRITE)
#define VMM_FLAGS_KERNEL_RO     (VMM_FLAG_PRESENT)
#define VMM_FLAGS_USER          (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER)
#define VMM_FLAGS_USER_RO       (VMM_FLAG_PRESENT | VMM_FLAG_USER)
#define VMM_FLAGS_MMIO          (VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_NOCACHE)

/* Page table constants */
#define VMM_PAGE_SIZE           4096
#define VMM_PAGE_SHIFT          12
#define VMM_PAGE_MASK           (~(VMM_PAGE_SIZE - 1))

#define VMM_ENTRIES_PER_TABLE   512
#define VMM_ENTRY_SHIFT         9

/* Address extraction masks */
#define VMM_PML4_SHIFT          39
#define VMM_PDPT_SHIFT          30
#define VMM_PD_SHIFT            21
#define VMM_PT_SHIFT            12

#define VMM_INDEX_MASK          0x1FF   /* 9 bits */
#define VMM_OFFSET_MASK         0xFFF   /* 12 bits */

/* Physical address mask (bits 12-51) */
#define VMM_ADDR_MASK           0x000FFFFFFFFFF000ULL

/* Extract indices from virtual address */
#define VMM_PML4_INDEX(virt)    (((virt) >> VMM_PML4_SHIFT) & VMM_INDEX_MASK)
#define VMM_PDPT_INDEX(virt)    (((virt) >> VMM_PDPT_SHIFT) & VMM_INDEX_MASK)
#define VMM_PD_INDEX(virt)      (((virt) >> VMM_PD_SHIFT) & VMM_INDEX_MASK)
#define VMM_PT_INDEX(virt)      (((virt) >> VMM_PT_SHIFT) & VMM_INDEX_MASK)
#define VMM_PAGE_OFFSET(virt)   ((virt) & VMM_OFFSET_MASK)

/* Kernel virtual address space layout */
#define VMM_KERNEL_BASE         0xFFFFFFFF80000000ULL  /* Higher half kernel */
#define VMM_KERNEL_PHYS_MAP     0xFFFF800000000000ULL  /* Direct physical mapping */

/* Page table entry type */
typedef uint64_t pte_t;

/* Page table structure (512 entries, 4KB aligned) */
typedef struct ALIGNED(VMM_PAGE_SIZE) {
    pte_t entries[VMM_ENTRIES_PER_TABLE];
} page_table_t;

/**
 * Initialize the Virtual Memory Manager
 * Sets up kernel page tables with identity mapping for low memory
 * and higher-half mapping for kernel.
 */
void vmm_init(void);

/**
 * Map a virtual page to a physical page
 * @param virt Virtual address (page-aligned)
 * @param phys Physical address (page-aligned)
 * @param flags Page flags (VMM_FLAG_*)
 * @return true on success, false on failure (e.g., out of memory)
 */
bool vmm_map_page(virtaddr_t virt, physaddr_t phys, uint64_t flags);

/**
 * Map a range of pages
 * @param virt Starting virtual address (page-aligned)
 * @param phys Starting physical address (page-aligned)
 * @param count Number of pages to map
 * @param flags Page flags (VMM_FLAG_*)
 * @return true on success, false on failure
 */
bool vmm_map_pages(virtaddr_t virt, physaddr_t phys, size_t count, uint64_t flags);

/**
 * Unmap a virtual page
 * @param virt Virtual address to unmap (page-aligned)
 * @return Physical address that was mapped, or 0 if not mapped
 */
physaddr_t vmm_unmap_page(virtaddr_t virt);

/**
 * Get physical address for a virtual address
 * @param virt Virtual address to translate
 * @return Physical address, or 0 if not mapped
 */
physaddr_t vmm_get_physical(virtaddr_t virt);

/**
 * Create a new address space (new PML4)
 * @return Physical address of new PML4, or 0 on failure
 */
physaddr_t vmm_create_address_space(void);

/**
 * Destroy an address space and free all page tables
 * @param pml4_phys Physical address of PML4 to destroy
 */
void vmm_destroy_address_space(physaddr_t pml4_phys);

/**
 * Switch to a different address space
 * @param pml4_phys Physical address of new PML4
 */
void vmm_switch_address_space(physaddr_t pml4_phys);

/**
 * Get current address space (current CR3 value)
 * @return Physical address of current PML4
 */
physaddr_t vmm_get_current_address_space(void);

/**
 * Invalidate a TLB entry for a virtual address
 * @param virt Virtual address to invalidate
 */
void vmm_invalidate_page(virtaddr_t virt);

/**
 * Flush entire TLB (reload CR3)
 */
void vmm_flush_tlb(void);

/**
 * Check if a virtual address is mapped
 * @param virt Virtual address to check
 * @return true if mapped, false otherwise
 */
bool vmm_is_mapped(virtaddr_t virt);

/**
 * Get the kernel's PML4 physical address
 * Used for cloning kernel mappings into user address spaces
 * @return Physical address of kernel PML4
 */
physaddr_t vmm_get_kernel_pml4(void);

#endif /* _AAAOS_MM_VMM_H */
