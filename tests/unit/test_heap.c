/**
 * AAAos Kernel - Heap Allocator Tests
 *
 * Unit tests for the kernel heap (kmalloc/kfree).
 */

#include "../framework/test.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/**
 * Test: Basic allocation
 */
TEST_CASE(test_heap_alloc) {
    void *ptr;

    /* Allocate memory */
    ptr = kmalloc(64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Free it */
    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: Multiple allocations
 */
TEST_CASE(test_heap_alloc_multiple) {
    void *ptrs[10];
    int i;

    /* Allocate 10 blocks */
    for (i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(64);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Verify all pointers are unique */
    for (i = 0; i < 10; i++) {
        int j;
        for (j = i + 1; j < 10; j++) {
            TEST_ASSERT_NE(ptrs[i], ptrs[j]);
        }
    }

    /* Free all */
    for (i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }

    TEST_PASS();
}

/**
 * Test: Various allocation sizes
 */
TEST_CASE(test_heap_alloc_sizes) {
    void *ptr;
    size_t sizes[] = {1, 8, 16, 32, 64, 128, 256, 512, 1024, 4096};
    int i;

    for (i = 0; i < 10; i++) {
        ptr = kmalloc(sizes[i]);
        TEST_ASSERT_NOT_NULL(ptr);
        kfree(ptr);
    }

    TEST_PASS();
}

/**
 * Test: Free NULL pointer (should not crash)
 */
TEST_CASE(test_heap_free_null) {
    /* This should not crash */
    kfree(NULL);

    TEST_PASS();
}

/**
 * Test: kcalloc zeros memory
 */
TEST_CASE(test_heap_calloc) {
    uint8_t *ptr;
    size_t i;

    /* Allocate zeroed memory */
    ptr = (uint8_t *)kcalloc(10, 10);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Verify all bytes are zero */
    for (i = 0; i < 100; i++) {
        TEST_ASSERT_EQ(ptr[i], 0);
    }

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: krealloc grow
 */
TEST_CASE(test_heap_realloc_grow) {
    uint8_t *ptr;
    size_t i;

    /* Allocate initial block */
    ptr = (uint8_t *)kmalloc(32);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Fill with pattern */
    for (i = 0; i < 32; i++) {
        ptr[i] = (uint8_t)i;
    }

    /* Grow it */
    ptr = (uint8_t *)krealloc(ptr, 64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Verify original data preserved */
    for (i = 0; i < 32; i++) {
        TEST_ASSERT_EQ(ptr[i], (uint8_t)i);
    }

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: krealloc shrink
 */
TEST_CASE(test_heap_realloc_shrink) {
    uint8_t *ptr;
    size_t i;

    /* Allocate initial block */
    ptr = (uint8_t *)kmalloc(64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Fill with pattern */
    for (i = 0; i < 64; i++) {
        ptr[i] = (uint8_t)i;
    }

    /* Shrink it */
    ptr = (uint8_t *)krealloc(ptr, 32);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Verify data preserved */
    for (i = 0; i < 32; i++) {
        TEST_ASSERT_EQ(ptr[i], (uint8_t)i);
    }

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: krealloc with NULL (acts like kmalloc)
 */
TEST_CASE(test_heap_realloc_null) {
    void *ptr;

    /* krealloc(NULL, size) should act like kmalloc(size) */
    ptr = krealloc(NULL, 64);
    TEST_ASSERT_NOT_NULL(ptr);

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: krealloc with size 0 (acts like kfree)
 */
TEST_CASE(test_heap_realloc_zero) {
    void *ptr;

    /* Allocate something */
    ptr = kmalloc(64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* krealloc(ptr, 0) should act like kfree and return NULL */
    ptr = krealloc(ptr, 0);
    TEST_ASSERT_NULL(ptr);

    TEST_PASS();
}

/**
 * Test: Aligned allocation
 */
TEST_CASE(test_heap_alloc_aligned) {
    void *ptr;
    uintptr_t addr;

    /* Allocate with 64-byte alignment */
    ptr = kmalloc_aligned(128, 64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Verify alignment */
    addr = (uintptr_t)ptr;
    TEST_ASSERT_EQ(addr % 64, 0);

    kfree_aligned(ptr);

    /* Test with 4096-byte (page) alignment */
    ptr = kmalloc_aligned(256, 4096);
    TEST_ASSERT_NOT_NULL(ptr);

    addr = (uintptr_t)ptr;
    TEST_ASSERT_EQ(addr % 4096, 0);

    kfree_aligned(ptr);

    TEST_PASS();
}

/**
 * Test: Memory can be written to
 */
TEST_CASE(test_heap_write_memory) {
    uint8_t *ptr;
    size_t i;

    ptr = (uint8_t *)kmalloc(256);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write pattern */
    for (i = 0; i < 256; i++) {
        ptr[i] = (uint8_t)(i ^ 0xAA);
    }

    /* Read back and verify */
    for (i = 0; i < 256; i++) {
        TEST_ASSERT_EQ(ptr[i], (uint8_t)(i ^ 0xAA));
    }

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: Heap statistics
 */
TEST_CASE(test_heap_stats) {
    heap_stats_t stats;
    void *ptr;

    /* Get initial stats */
    heap_get_stats(&stats);

    /* Verify basic sanity */
    TEST_ASSERT_GT(stats.total_size, 0);
    TEST_ASSERT_EQ(stats.total_size, stats.used_size + stats.free_size);

    /* Allocate some memory */
    ptr = kmalloc(1024);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Get new stats */
    heap_get_stats(&stats);

    /* Used size should have increased */
    TEST_ASSERT_GT(stats.used_size, 0);

    kfree(ptr);

    TEST_PASS();
}

/**
 * Test: Heap validation
 */
TEST_CASE(test_heap_validate) {
    void *ptr;
    bool valid;

    /* Heap should be valid initially */
    valid = heap_validate();
    TEST_ASSERT_EQ(valid, true);

    /* Allocate and free some memory */
    ptr = kmalloc(128);
    TEST_ASSERT_NOT_NULL(ptr);

    valid = heap_validate();
    TEST_ASSERT_EQ(valid, true);

    kfree(ptr);

    valid = heap_validate();
    TEST_ASSERT_EQ(valid, true);

    TEST_PASS();
}

/**
 * Test: Fragmentation handling
 */
TEST_CASE(test_heap_fragmentation) {
    void *ptrs[20];
    int i;

    /* Allocate 20 blocks */
    for (i = 0; i < 20; i++) {
        ptrs[i] = kmalloc(64);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Free every other block to create fragmentation */
    for (i = 0; i < 20; i += 2) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    /* Reallocate into the holes */
    for (i = 0; i < 20; i += 2) {
        ptrs[i] = kmalloc(64);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Free all */
    for (i = 0; i < 20; i++) {
        kfree(ptrs[i]);
    }

    /* Validate heap integrity */
    TEST_ASSERT_EQ(heap_validate(), true);

    TEST_PASS();
}

/**
 * Test: Large allocation
 */
TEST_CASE(test_heap_large_alloc) {
    void *ptr;
    heap_stats_t stats;

    heap_get_stats(&stats);

    /* Try to allocate a large block (but not bigger than heap) */
    if (stats.free_size > 16 * KB) {
        ptr = kmalloc(16 * KB);
        TEST_ASSERT_NOT_NULL(ptr);
        kfree(ptr);
    } else {
        TEST_SKIP("Not enough heap space for large allocation test");
    }

    TEST_PASS();
}
