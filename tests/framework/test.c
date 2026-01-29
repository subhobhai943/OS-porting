/**
 * AAAos Kernel - Test Framework Implementation
 *
 * Provides the core functionality for the unit testing framework.
 */

#include "test.h"

/* Global test state */
test_case_t g_test_cases[TEST_MAX_CASES];
size_t g_test_count = 0;
test_stats_t g_test_stats = {0, 0, 0, 0};

/* Current test failure info */
const char *g_test_fail_file = NULL;
int g_test_fail_line = 0;
const char *g_test_fail_msg = NULL;

/**
 * Register a test case
 */
void test_register(const char *name, test_func_t func, const char *file, int line) {
    if (g_test_count >= TEST_MAX_CASES) {
        kprintf("[TEST] ERROR: Maximum test cases reached (%d)\n", TEST_MAX_CASES);
        return;
    }

    g_test_cases[g_test_count].name = name;
    g_test_cases[g_test_count].func = func;
    g_test_cases[g_test_count].file = file;
    g_test_cases[g_test_count].line = line;
    g_test_count++;
}

/**
 * Run a single test case
 */
static void test_run_one(test_case_t *test) {
    int result;

    /* Reset failure info */
    g_test_fail_file = NULL;
    g_test_fail_line = 0;
    g_test_fail_msg = NULL;

    /* Run the test */
    result = test->func();

    /* Update statistics and print result */
    g_test_stats.total++;

    switch (result) {
        case TEST_RESULT_PASS:
            g_test_stats.passed++;
            kprintf("[PASS] %s\n", test->name);
            break;

        case TEST_RESULT_FAIL:
            g_test_stats.failed++;
            if (g_test_fail_msg && g_test_fail_line > 0) {
                kprintf("[FAIL] %s: %s at line %d\n",
                        test->name, g_test_fail_msg, g_test_fail_line);
            } else if (g_test_fail_msg) {
                kprintf("[FAIL] %s: %s\n", test->name, g_test_fail_msg);
            } else {
                kprintf("[FAIL] %s\n", test->name);
            }
            break;

        case TEST_RESULT_SKIP:
            g_test_stats.skipped++;
            if (g_test_fail_msg) {
                kprintf("[SKIP] %s: %s\n", test->name, g_test_fail_msg);
            } else {
                kprintf("[SKIP] %s\n", test->name);
            }
            break;

        default:
            g_test_stats.failed++;
            kprintf("[FAIL] %s: unknown result code %d\n", test->name, result);
            break;
    }
}

/**
 * Run all registered tests
 */
size_t test_run_all(void) {
    size_t i;

    kprintf("\n");
    kprintf("========================================\n");
    kprintf("Running tests...\n");
    kprintf("========================================\n");

    /* Reset statistics */
    g_test_stats.total = 0;
    g_test_stats.passed = 0;
    g_test_stats.failed = 0;
    g_test_stats.skipped = 0;

    /* Run each test */
    for (i = 0; i < g_test_count; i++) {
        test_run_one(&g_test_cases[i]);
    }

    /* Print summary */
    test_print_summary();

    return g_test_stats.failed;
}

/**
 * Print test summary
 */
void test_print_summary(void) {
    kprintf("========================================\n");
    kprintf("Results: %u passed, %u failed",
            (uint32_t)g_test_stats.passed,
            (uint32_t)g_test_stats.failed);

    if (g_test_stats.skipped > 0) {
        kprintf(", %u skipped", (uint32_t)g_test_stats.skipped);
    }

    kprintf(", %u total\n", (uint32_t)g_test_stats.total);
    kprintf("========================================\n");

    if (g_test_stats.failed == 0) {
        kprintf("All tests passed!\n");
    } else {
        kprintf("Some tests failed.\n");
    }
    kprintf("\n");
}

/**
 * Reset test framework state
 */
void test_reset(void) {
    g_test_count = 0;
    g_test_stats.total = 0;
    g_test_stats.passed = 0;
    g_test_stats.failed = 0;
    g_test_stats.skipped = 0;
    g_test_fail_file = NULL;
    g_test_fail_line = 0;
    g_test_fail_msg = NULL;
}

/**
 * Compare two strings (standalone implementation for tests)
 */
int test_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/**
 * Compare two memory regions (standalone implementation for tests)
 */
int test_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}
