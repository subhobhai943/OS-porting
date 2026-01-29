/**
 * AAAos Kernel - Virtual Memory Manager Implementation
 *
 * Implements x86_64 4-level paging with support for:
 * - Kernel higher-half mapping
 * - User-space address spaces
 * - On-demand page table allocation via PMM
 */

#include "vmm.h"
#include "pmm.h"
#include "../include/serial.h"

/* Kernel PML4 (root of kernel page tables) */
static physaddr_t kernel_pml4_phys = 0;

/* Simple spinlock for VMM operations */
static volatile int vmm_lock = 0;

static inline void vmm_acquire_lock(void) {
    while (__sync_lock_test_and_set(&vmm_lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

static inline void vmm_release_lock(void) {
    __sync_lock_release(&vmm_lock);
}

/**
 * Read CR3 register (current page table base)
 */
static inline physaddr_t read_cr3(void) {
    physaddr_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * Write CR3 register (switch page tables)
 */
static inline void write_cr3(physaddr_t cr3) {
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/**
 * Invalidate TLB entry for specific address
 */
static inline void invlpg(virtaddr_t addr) {
    __asm__ __volatile__("invlpg (%0)" :: "r"(addr) : "memory");
}

/**
 * Convert physical address to virtual address (direct mapping)
 * In the kernel, we use a direct mapping region where:
 *   virtual = physical + VMM_KERNEL_PHYS_MAP
 *
 * For early boot before VMM is fully initialized, we use identity mapping.
 */
static inline void* phys_to_virt(physaddr_t phys) {
    /* During early init, use identity mapping */
    if (kernel_pml4_phys == 0) {
        return (void*)phys;
    }
    /* After VMM init, could use higher-half direct map */
    /* For now, keep identity mapping for simplicity */
    return (void*)phys;
}

/**
 * Zero out a page
 */
static void zero_page(physaddr_t phys) {
    uint64_t *ptr = (uint64_t*)phys_to_virt(phys);
    for (size_t i = 0; i < VMM_PAGE_SIZE / sizeof(uint64_t); i++) {
        ptr[i] = 0;
    }
}

/**
 * Allocate and zero a page table
 * @return Physical address of new page table, or 0 on failure
 */
static physaddr_t alloc_page_table(void) {
    physaddr_t phys = pmm_alloc_page();
    if (phys == 0) {
        kprintf("[VMM] Error: Failed to allocate page table\n");
        return 0;
    }
    zero_page(phys);
    return phys;
}

/**
 * Get or create a page table entry at the next level
 * @param table Current level page table
 * @param index Index in the table
 * @param create If true, create the entry if it doesn't exist
 * @param flags Flags to use when creating (only PRESENT, WRITE, USER propagate)
 * @return Physical address of next level table, or 0 if not present/failed
 */
static physaddr_t get_or_create_entry(page_table_t *table, size_t index,
                                       bool create, uint64_t flags) {
    pte_t *entry = &table->entries[index];

    if (*entry & VMM_FLAG_PRESENT) {
        /* Entry exists, return its address */
        return *entry & VMM_ADDR_MASK;
    }

    if (!create) {
        return 0;
    }

    /* Allocate new page table */
    physaddr_t new_table = alloc_page_table();
    if (new_table == 0) {
        return 0;
    }

    /* Set entry with appropriate flags */
    /* Intermediate tables need PRESENT, WRITE, and USER if user-accessible */
    uint64_t entry_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITE;
    if (flags & VMM_FLAG_USER) {
        entry_flags |= VMM_FLAG_USER;
    }

    *entry = new_table | entry_flags;

    return new_table;
}

/**
 * Walk page tables to find the page table entry for a virtual address
 * @param pml4_phys Physical address of PML4
 * @param virt Virtual address to look up
 * @param create If true, create missing page tables
 * @param flags Flags to use when creating intermediate tables
 * @return Pointer to PTE, or NULL if not found/couldn't create
 */
static pte_t* vmm_walk(physaddr_t pml4_phys, virtaddr_t virt,
                       bool create, uint64_t flags) {
    page_table_t *pml4 = (page_table_t*)phys_to_virt(pml4_phys);

    /* Get PDPT */
    physaddr_t pdpt_phys = get_or_create_entry(pml4, VMM_PML4_INDEX(virt),
                                                create, flags);
    if (pdpt_phys == 0) {
        return NULL;
    }
    page_table_t *pdpt = (page_table_t*)phys_to_virt(pdpt_phys);

    /* Get PD */
    physaddr_t pd_phys = get_or_create_entry(pdpt, VMM_PDPT_INDEX(virt),
                                              create, flags);
    if (pd_phys == 0) {
        return NULL;
    }
    page_table_t *pd = (page_table_t*)phys_to_virt(pd_phys);

    /* Get PT */
    physaddr_t pt_phys = get_or_create_entry(pd, VMM_PD_INDEX(virt),
                                              create, flags);
    if (pt_phys == 0) {
        return NULL;
    }
    page_table_t *pt = (page_table_t*)phys_to_virt(pt_phys);

    /* Return pointer to the final PTE */
    return &pt->entries[VMM_PT_INDEX(virt)];
}

/**
 * Initialize the Virtual Memory Manager
 */
void vmm_init(void) {
    kprintf("[VMM] Initializing Virtual Memory Manager...\n");

    /* Allocate kernel PML4 */
    kernel_pml4_phys = alloc_page_table();
    if (kernel_pml4_phys == 0) {
        kprintf("[VMM] FATAL: Cannot allocate kernel PML4!\n");
        return;
    }

    kprintf("[VMM] Kernel PML4 at physical address 0x%llx\n",
            (uint64_t)kernel_pml4_phys);

    /* Create identity mapping for first 4MB (for early boot compatibility) */
    kprintf("[VMM] Creating identity mapping for first 4MB...\n");
    for (physaddr_t addr = 0; addr < 4 * MB; addr += VMM_PAGE_SIZE) {
        if (!vmm_map_page(addr, addr, VMM_FLAGS_KERNEL)) {
            kprintf("[VMM] Warning: Failed to map 0x%llx\n", (uint64_t)addr);
        }
    }

    /* Map kernel higher-half (optional, depends on your kernel linking) */
    /* For now, we'll use identity mapping which is simpler */

    /* Also identity-map more memory for kernel use (up to 16MB) */
    kprintf("[VMM] Extending identity mapping to 16MB...\n");
    for (physaddr_t addr = 4 * MB; addr < 16 * MB; addr += VMM_PAGE_SIZE) {
        if (!vmm_map_page(addr, addr, VMM_FLAGS_KERNEL)) {
            kprintf("[VMM] Warning: Failed to map 0x%llx\n", (uint64_t)addr);
        }
    }

    /* Switch to our new page tables */
    kprintf("[VMM] Switching to kernel page tables...\n");
    vmm_switch_address_space(kernel_pml4_phys);

    kprintf("[VMM] Virtual Memory Manager initialized successfully\n");
    kprintf("[VMM] Identity mapped: 0x0 - 0x%llx\n", (uint64_t)(16 * MB));
}

/**
 * Map a virtual page to a physical page
 */
bool vmm_map_page(virtaddr_t virt, physaddr_t phys, uint64_t flags) {
    /* Validate alignment */
    if (!IS_ALIGNED(virt, VMM_PAGE_SIZE) || !IS_ALIGNED(phys, VMM_PAGE_SIZE)) {
        kprintf("[VMM] Error: Unaligned addresses in vmm_map_page\n");
        kprintf("[VMM]   virt=0x%llx, phys=0x%llx\n",
                (uint64_t)virt, (uint64_t)phys);
        return false;
    }

    /* Use current address space if kernel PML4 not set yet */
    physaddr_t pml4 = kernel_pml4_phys;
    if (pml4 == 0) {
        /* VMM not initialized, use identity mapping assumption */
        pml4 = read_cr3() & VMM_ADDR_MASK;
    }

    vmm_acquire_lock();

    /* Walk page tables, creating as needed */
    pte_t *pte = vmm_walk(pml4, virt, true, flags);
    if (pte == NULL) {
        vmm_release_lock();
        kprintf("[VMM] Error: Failed to walk/create page tables for 0x%llx\n",
                (uint64_t)virt);
        return false;
    }

    /* Check if already mapped */
    if (*pte & VMM_FLAG_PRESENT) {
        physaddr_t old_phys = *pte & VMM_ADDR_MASK;
        if (old_phys != phys) {
            kprintf("[VMM] Warning: Remapping 0x%llx from 0x%llx to 0x%llx\n",
                    (uint64_t)virt, (uint64_t)old_phys, (uint64_t)phys);
        }
    }

    /* Set the page table entry */
    *pte = (phys & VMM_ADDR_MASK) | (flags & ~VMM_ADDR_MASK) | VMM_FLAG_PRESENT;

    vmm_release_lock();

    /* Invalidate TLB for this page */
    invlpg(virt);

    return true;
}

/**
 * Map a range of pages
 */
bool vmm_map_pages(virtaddr_t virt, physaddr_t phys, size_t count, uint64_t flags) {
    for (size_t i = 0; i < count; i++) {
        virtaddr_t v = virt + (i * VMM_PAGE_SIZE);
        physaddr_t p = phys + (i * VMM_PAGE_SIZE);

        if (!vmm_map_page(v, p, flags)) {
            /* Rollback previously mapped pages */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(virt + (j * VMM_PAGE_SIZE));
            }
            return false;
        }
    }
    return true;
}

/**
 * Unmap a virtual page
 */
physaddr_t vmm_unmap_page(virtaddr_t virt) {
    if (!IS_ALIGNED(virt, VMM_PAGE_SIZE)) {
        kprintf("[VMM] Error: Unaligned address in vmm_unmap_page: 0x%llx\n",
                (uint64_t)virt);
        return 0;
    }

    physaddr_t pml4 = kernel_pml4_phys;
    if (pml4 == 0) {
        pml4 = read_cr3() & VMM_ADDR_MASK;
    }

    vmm_acquire_lock();

    /* Walk page tables without creating */
    pte_t *pte = vmm_walk(pml4, virt, false, 0);
    if (pte == NULL || !(*pte & VMM_FLAG_PRESENT)) {
        vmm_release_lock();
        return 0;  /* Not mapped */
    }

    /* Get physical address before clearing */
    physaddr_t phys = *pte & VMM_ADDR_MASK;

    /* Clear the entry */
    *pte = 0;

    vmm_release_lock();

    /* Invalidate TLB */
    invlpg(virt);

    return phys;
}

/**
 * Get physical address for a virtual address
 */
physaddr_t vmm_get_physical(virtaddr_t virt) {
    physaddr_t pml4 = kernel_pml4_phys;
    if (pml4 == 0) {
        pml4 = read_cr3() & VMM_ADDR_MASK;
    }

    vmm_acquire_lock();

    pte_t *pte = vmm_walk(pml4, virt, false, 0);

    vmm_release_lock();

    if (pte == NULL || !(*pte & VMM_FLAG_PRESENT)) {
        return 0;  /* Not mapped */
    }

    /* Return physical address with page offset */
    return (*pte & VMM_ADDR_MASK) | VMM_PAGE_OFFSET(virt);
}

/**
 * Create a new address space (new PML4)
 */
physaddr_t vmm_create_address_space(void) {
    /* Allocate new PML4 */
    physaddr_t new_pml4 = alloc_page_table();
    if (new_pml4 == 0) {
        kprintf("[VMM] Error: Failed to allocate new PML4\n");
        return 0;
    }

    /* Copy kernel mappings (upper half entries) to new address space */
    /* This ensures kernel is always accessible in every address space */
    if (kernel_pml4_phys != 0) {
        page_table_t *src = (page_table_t*)phys_to_virt(kernel_pml4_phys);
        page_table_t *dst = (page_table_t*)phys_to_virt(new_pml4);

        /* Copy entries 256-511 (upper half, kernel space) */
        /* Entry 256 starts at virtual address 0xFFFF800000000000 */
        for (size_t i = 256; i < VMM_ENTRIES_PER_TABLE; i++) {
            dst->entries[i] = src->entries[i];
        }

        /* Also copy lower entries for identity-mapped kernel regions */
        /* This allows the kernel to access low memory */
        for (size_t i = 0; i < 256; i++) {
            if (src->entries[i] & VMM_FLAG_PRESENT) {
                /* Only copy kernel mappings, not user mappings */
                if (!(src->entries[i] & VMM_FLAG_USER)) {
                    dst->entries[i] = src->entries[i];
                }
            }
        }
    }

    kprintf("[VMM] Created new address space at 0x%llx\n", (uint64_t)new_pml4);
    return new_pml4;
}

/**
 * Recursively free page tables
 */
static void free_page_table_recursive(physaddr_t table_phys, int level,
                                       bool free_user_only) {
    if (table_phys == 0) {
        return;
    }

    page_table_t *table = (page_table_t*)phys_to_virt(table_phys);

    /* Determine range to process */
    size_t start = free_user_only ? 0 : 0;
    size_t end = free_user_only ? 256 : VMM_ENTRIES_PER_TABLE;

    for (size_t i = start; i < end; i++) {
        pte_t entry = table->entries[i];

        if (!(entry & VMM_FLAG_PRESENT)) {
            continue;
        }

        /* Don't free huge pages at intermediate levels */
        if (entry & VMM_FLAG_HUGE) {
            continue;
        }

        physaddr_t child = entry & VMM_ADDR_MASK;

        /* Recurse into child tables (but not at PT level) */
        if (level < 3) {  /* 0=PML4, 1=PDPT, 2=PD, 3=PT */
            free_page_table_recursive(child, level + 1, false);
        }

        /* Free the child page table */
        pmm_free_page(child);
    }
}

/**
 * Destroy an address space and free all page tables
 */
void vmm_destroy_address_space(physaddr_t pml4_phys) {
    if (pml4_phys == 0 || pml4_phys == kernel_pml4_phys) {
        kprintf("[VMM] Error: Cannot destroy kernel address space\n");
        return;
    }

    /* Don't destroy current address space */
    if (pml4_phys == (read_cr3() & VMM_ADDR_MASK)) {
        kprintf("[VMM] Error: Cannot destroy current address space\n");
        return;
    }

    kprintf("[VMM] Destroying address space at 0x%llx\n", (uint64_t)pml4_phys);

    vmm_acquire_lock();

    /* Free user-space page tables (first 256 entries only) */
    /* Don't free kernel mappings as they're shared */
    free_page_table_recursive(pml4_phys, 0, true);

    /* Free the PML4 itself */
    pmm_free_page(pml4_phys);

    vmm_release_lock();
}

/**
 * Switch to a different address space
 */
void vmm_switch_address_space(physaddr_t pml4_phys) {
    if (pml4_phys == 0) {
        kprintf("[VMM] Error: Cannot switch to NULL address space\n");
        return;
    }

    /* Only switch if different from current */
    physaddr_t current = read_cr3() & VMM_ADDR_MASK;
    if (current != pml4_phys) {
        write_cr3(pml4_phys);
    }
}

/**
 * Get current address space
 */
physaddr_t vmm_get_current_address_space(void) {
    return read_cr3() & VMM_ADDR_MASK;
}

/**
 * Invalidate a TLB entry
 */
void vmm_invalidate_page(virtaddr_t virt) {
    invlpg(virt);
}

/**
 * Flush entire TLB
 */
void vmm_flush_tlb(void) {
    physaddr_t cr3 = read_cr3();
    write_cr3(cr3);
}

/**
 * Check if a virtual address is mapped
 */
bool vmm_is_mapped(virtaddr_t virt) {
    physaddr_t pml4 = kernel_pml4_phys;
    if (pml4 == 0) {
        pml4 = read_cr3() & VMM_ADDR_MASK;
    }

    vmm_acquire_lock();

    pte_t *pte = vmm_walk(pml4, virt, false, 0);

    vmm_release_lock();

    return (pte != NULL) && (*pte & VMM_FLAG_PRESENT);
}

/**
 * Get the kernel's PML4 physical address
 */
physaddr_t vmm_get_kernel_pml4(void) {
    return kernel_pml4_phys;
}
