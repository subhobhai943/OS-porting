/**
 * AAAos Kernel - Physical Memory Manager Tests
 *
 * Unit tests for the PMM bitmap allocator.
 */

#include "../framework/test.h"
#include "../../kernel/mm/pmm.h"

/**
 * Test: Allocate and free a single page
 */
TEST_CASE(test_pmm_alloc) {
    physaddr_t page;
    size_t free_before, free_after;

    /* Get initial free page count */
    free_before = pmm_get_free_pages();

    /* Allocate a page */
    page = pmm_alloc_page();
    TEST_ASSERT_NE(page, 0);

    /* Verify page is aligned */
    TEST_ASSERT_EQ(page % PMM_PAGE_SIZE, 0);

    /* Verify free count decreased */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before - 1);

    /* Free the page */
    pmm_free_page(page);

    /* Verify free count restored */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before);

    TEST_PASS();
}

/**
 * Test: Allocate multiple pages
 */
TEST_CASE(test_pmm_alloc_multiple) {
    physaddr_t pages[10];
    size_t free_before, free_after;
    int i;

    /* Get initial free page count */
    free_before = pmm_get_free_pages();

    /* Allocate 10 pages */
    for (i = 0; i < 10; i++) {
        pages[i] = pmm_alloc_page();
        TEST_ASSERT_NE(pages[i], 0);
        TEST_ASSERT_EQ(pages[i] % PMM_PAGE_SIZE, 0);
    }

    /* Verify all pages are unique */
    for (i = 0; i < 10; i++) {
        int j;
        for (j = i + 1; j < 10; j++) {
            TEST_ASSERT_NE(pages[i], pages[j]);
        }
    }

    /* Verify free count decreased by 10 */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before - 10);

    /* Free all pages */
    for (i = 0; i < 10; i++) {
        pmm_free_page(pages[i]);
    }

    /* Verify free count restored */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before);

    TEST_PASS();
}

/**
 * Test: Allocate contiguous pages
 */
TEST_CASE(test_pmm_alloc_contiguous) {
    physaddr_t block;
    size_t free_before, free_after;

    /* Get initial free page count */
    free_before = pmm_get_free_pages();

    /* Allocate 4 contiguous pages */
    block = pmm_alloc_pages(4);
    TEST_ASSERT_NE(block, 0);
    TEST_ASSERT_EQ(block % PMM_PAGE_SIZE, 0);

    /* Verify free count decreased by 4 */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before - 4);

    /* Free the block */
    pmm_free_pages(block, 4);

    /* Verify free count restored */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before);

    TEST_PASS();
}

/**
 * Test: Free and reallocate pages
 */
TEST_CASE(test_pmm_free) {
    physaddr_t page1, page2;

    /* Allocate a page */
    page1 = pmm_alloc_page();
    TEST_ASSERT_NE(page1, 0);

    /* Free it */
    pmm_free_page(page1);

    /* Allocate another page - might get the same one back */
    page2 = pmm_alloc_page();
    TEST_ASSERT_NE(page2, 0);

    /* The page should be usable */
    TEST_ASSERT_EQ(page2 % PMM_PAGE_SIZE, 0);

    /* Clean up */
    pmm_free_page(page2);

    TEST_PASS();
}

/**
 * Test: Check PMM statistics
 */
TEST_CASE(test_pmm_stats) {
    size_t total, used, free_pages;

    /* Get statistics */
    total = pmm_get_total_pages();
    used = pmm_get_used_pages();
    free_pages = pmm_get_free_pages();

    /* Verify basic consistency */
    TEST_ASSERT_GT(total, 0);
    TEST_ASSERT_EQ(total, used + free_pages);

    /* Verify we have some free memory */
    TEST_ASSERT_GT(free_pages, 0);

    TEST_PASS();
}

/**
 * Test: Page free status check
 */
TEST_CASE(test_pmm_is_page_free) {
    physaddr_t page;
    bool is_free;

    /* Allocate a page */
    page = pmm_alloc_page();
    TEST_ASSERT_NE(page, 0);

    /* Page should not be free */
    is_free = pmm_is_page_free(page);
    TEST_ASSERT_EQ(is_free, false);

    /* Free the page */
    pmm_free_page(page);

    /* Page should now be free */
    is_free = pmm_is_page_free(page);
    TEST_ASSERT_EQ(is_free, true);

    TEST_PASS();
}

/**
 * Test: Stress test with many allocations
 */
TEST_CASE(test_pmm_stress) {
    physaddr_t pages[100];
    size_t free_before, free_after;
    int i;

    /* Get initial free page count */
    free_before = pmm_get_free_pages();

    /* Need at least 100 free pages */
    if (free_before < 100) {
        TEST_SKIP("Not enough free pages for stress test");
    }

    /* Allocate 100 pages */
    for (i = 0; i < 100; i++) {
        pages[i] = pmm_alloc_page();
        TEST_ASSERT_NE(pages[i], 0);
    }

    /* Verify free count */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before - 100);

    /* Free pages in reverse order */
    for (i = 99; i >= 0; i--) {
        pmm_free_page(pages[i]);
    }

    /* Verify free count restored */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before);

    TEST_PASS();
}

/**
 * Test: Alternating alloc/free pattern
 */
TEST_CASE(test_pmm_alternating) {
    physaddr_t pages[10];
    size_t free_before, free_after;
    int i;

    free_before = pmm_get_free_pages();

    /* Allocate 10 pages */
    for (i = 0; i < 10; i++) {
        pages[i] = pmm_alloc_page();
        TEST_ASSERT_NE(pages[i], 0);
    }

    /* Free even-indexed pages */
    for (i = 0; i < 10; i += 2) {
        pmm_free_page(pages[i]);
    }

    /* Verify fragmented free count */
    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before - 5);

    /* Reallocate to fill holes */
    for (i = 0; i < 10; i += 2) {
        pages[i] = pmm_alloc_page();
        TEST_ASSERT_NE(pages[i], 0);
    }

    /* Free all */
    for (i = 0; i < 10; i++) {
        pmm_free_page(pages[i]);
    }

    free_after = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after, free_before);

    TEST_PASS();
}
