/**
 * AAAos Kernel - Virtual Memory Manager Tests
 *
 * Unit tests for the VMM paging implementation.
 */

#include "../framework/test.h"
#include "../../kernel/mm/vmm.h"
#include "../../kernel/mm/pmm.h"

/* Test virtual address range (use high addresses to avoid conflicts) */
#define TEST_VIRT_BASE  0xFFFF900000000000ULL

/**
 * Test: Map and unmap a single page
 */
TEST_CASE(test_vmm_map_page) {
    virtaddr_t virt = TEST_VIRT_BASE;
    physaddr_t phys, retrieved;
    bool result;

    /* Allocate a physical page */
    phys = pmm_alloc_page();
    TEST_ASSERT_NE(phys, 0);

    /* Map the page */
    result = vmm_map_page(virt, phys, VMM_FLAGS_KERNEL);
    TEST_ASSERT_EQ(result, true);

    /* Verify mapping */
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    /* Get physical address back */
    retrieved = vmm_get_physical(virt);
    TEST_ASSERT_EQ(retrieved, phys);

    /* Unmap the page */
    retrieved = vmm_unmap_page(virt);
    TEST_ASSERT_EQ(retrieved, phys);

    /* Verify unmapped */
    TEST_ASSERT_EQ(vmm_is_mapped(virt), false);

    /* Free the physical page */
    pmm_free_page(phys);

    TEST_PASS();
}

/**
 * Test: Map multiple pages
 */
TEST_CASE(test_vmm_map_pages) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x10 * VMM_PAGE_SIZE);
    physaddr_t phys;
    bool result;
    int i;

    /* Allocate 4 contiguous physical pages */
    phys = pmm_alloc_pages(4);
    TEST_ASSERT_NE(phys, 0);

    /* Map all 4 pages */
    result = vmm_map_pages(virt, phys, 4, VMM_FLAGS_KERNEL);
    TEST_ASSERT_EQ(result, true);

    /* Verify all pages are mapped */
    for (i = 0; i < 4; i++) {
        virtaddr_t addr = virt + (i * VMM_PAGE_SIZE);
        TEST_ASSERT_EQ(vmm_is_mapped(addr), true);
        TEST_ASSERT_EQ(vmm_get_physical(addr), phys + (i * VMM_PAGE_SIZE));
    }

    /* Unmap all pages */
    for (i = 0; i < 4; i++) {
        vmm_unmap_page(virt + (i * VMM_PAGE_SIZE));
    }

    /* Verify all unmapped */
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQ(vmm_is_mapped(virt + (i * VMM_PAGE_SIZE)), false);
    }

    /* Free physical pages */
    pmm_free_pages(phys, 4);

    TEST_PASS();
}

/**
 * Test: Virtual to physical translation
 */
TEST_CASE(test_vmm_get_physical) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x20 * VMM_PAGE_SIZE);
    physaddr_t phys, retrieved;

    /* Unmapped address should return 0 */
    retrieved = vmm_get_physical(virt);
    TEST_ASSERT_EQ(retrieved, 0);

    /* Allocate and map */
    phys = pmm_alloc_page();
    TEST_ASSERT_NE(phys, 0);

    vmm_map_page(virt, phys, VMM_FLAGS_KERNEL);

    /* Now translation should work */
    retrieved = vmm_get_physical(virt);
    TEST_ASSERT_EQ(retrieved, phys);

    /* Test with offset within page */
    retrieved = vmm_get_physical(virt + 0x123);
    TEST_ASSERT_EQ(retrieved, phys + 0x123);

    /* Clean up */
    vmm_unmap_page(virt);
    pmm_free_page(phys);

    TEST_PASS();
}

/**
 * Test: Page flags (write permission)
 */
TEST_CASE(test_vmm_flags) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x30 * VMM_PAGE_SIZE);
    physaddr_t phys;
    bool result;

    phys = pmm_alloc_page();
    TEST_ASSERT_NE(phys, 0);

    /* Map as read-only */
    result = vmm_map_page(virt, phys, VMM_FLAGS_KERNEL_RO);
    TEST_ASSERT_EQ(result, true);
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    vmm_unmap_page(virt);

    /* Map as read-write */
    result = vmm_map_page(virt, phys, VMM_FLAGS_KERNEL);
    TEST_ASSERT_EQ(result, true);
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    vmm_unmap_page(virt);
    pmm_free_page(phys);

    TEST_PASS();
}

/**
 * Test: User-space flags
 */
TEST_CASE(test_vmm_user_flags) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x40 * VMM_PAGE_SIZE);
    physaddr_t phys;
    bool result;

    phys = pmm_alloc_page();
    TEST_ASSERT_NE(phys, 0);

    /* Map with user flags */
    result = vmm_map_page(virt, phys, VMM_FLAGS_USER);
    TEST_ASSERT_EQ(result, true);
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    vmm_unmap_page(virt);

    /* Map with user read-only flags */
    result = vmm_map_page(virt, phys, VMM_FLAGS_USER_RO);
    TEST_ASSERT_EQ(result, true);
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    vmm_unmap_page(virt);
    pmm_free_page(phys);

    TEST_PASS();
}

/**
 * Test: Is mapped check
 */
TEST_CASE(test_vmm_is_mapped) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x50 * VMM_PAGE_SIZE);
    physaddr_t phys;

    /* Should not be mapped initially */
    TEST_ASSERT_EQ(vmm_is_mapped(virt), false);

    /* Map it */
    phys = pmm_alloc_page();
    TEST_ASSERT_NE(phys, 0);

    vmm_map_page(virt, phys, VMM_FLAGS_KERNEL);

    /* Now it should be mapped */
    TEST_ASSERT_EQ(vmm_is_mapped(virt), true);

    /* Unmap */
    vmm_unmap_page(virt);

    /* Should be unmapped again */
    TEST_ASSERT_EQ(vmm_is_mapped(virt), false);

    pmm_free_page(phys);

    TEST_PASS();
}

/**
 * Test: Get current address space
 */
TEST_CASE(test_vmm_get_current_address_space) {
    physaddr_t current_pml4;

    /* Get current address space */
    current_pml4 = vmm_get_current_address_space();

    /* Should be valid (non-zero and page-aligned) */
    TEST_ASSERT_NE(current_pml4, 0);
    TEST_ASSERT_EQ(current_pml4 % VMM_PAGE_SIZE, 0);

    TEST_PASS();
}

/**
 * Test: Get kernel PML4
 */
TEST_CASE(test_vmm_get_kernel_pml4) {
    physaddr_t kernel_pml4;

    /* Get kernel PML4 */
    kernel_pml4 = vmm_get_kernel_pml4();

    /* Should be valid (non-zero and page-aligned) */
    TEST_ASSERT_NE(kernel_pml4, 0);
    TEST_ASSERT_EQ(kernel_pml4 % VMM_PAGE_SIZE, 0);

    TEST_PASS();
}

/**
 * Test: TLB invalidation (basic functionality test)
 */
TEST_CASE(test_vmm_invalidate) {
    virtaddr_t virt = TEST_VIRT_BASE + (0x60 * VMM_PAGE_SIZE);
    physaddr_t phys1, phys2;

    /* Allocate two physical pages */
    phys1 = pmm_alloc_page();
    TEST_ASSERT_NE(phys1, 0);

    phys2 = pmm_alloc_page();
    TEST_ASSERT_NE(phys2, 0);

    /* Map to first physical page */
    vmm_map_page(virt, phys1, VMM_FLAGS_KERNEL);
    TEST_ASSERT_EQ(vmm_get_physical(virt), phys1);

    /* Unmap and remap to different physical page */
    vmm_unmap_page(virt);
    vmm_map_page(virt, phys2, VMM_FLAGS_KERNEL);

    /* Invalidate TLB entry */
    vmm_invalidate_page(virt);

    /* Should now translate to phys2 */
    TEST_ASSERT_EQ(vmm_get_physical(virt), phys2);

    /* Clean up */
    vmm_unmap_page(virt);
    pmm_free_page(phys1);
    pmm_free_page(phys2);

    TEST_PASS();
}

/**
 * Test: Address space creation and destruction
 */
TEST_CASE(test_vmm_address_space) {
    physaddr_t new_space;

    /* Create a new address space */
    new_space = vmm_create_address_space();
    TEST_ASSERT_NE(new_space, 0);
    TEST_ASSERT_EQ(new_space % VMM_PAGE_SIZE, 0);

    /* Destroy it */
    vmm_destroy_address_space(new_space);

    TEST_PASS();
}
