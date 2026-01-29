/**
 * AAAos Kernel - Test Framework
 *
 * A lightweight unit testing framework for kernel components.
 * Provides macros for defining test cases, assertions, and result reporting.
 */

#ifndef _AAAOS_TEST_H
#define _AAAOS_TEST_H

#include "../../kernel/include/types.h"
#include "../../kernel/include/serial.h"

/* Maximum number of test cases */
#define TEST_MAX_CASES      256

/* Test result codes */
#define TEST_RESULT_PASS    0
#define TEST_RESULT_FAIL    1
#define TEST_RESULT_SKIP    2

/* Test case function pointer type */
typedef int (*test_func_t)(void);

/**
 * Test case structure
 */
typedef struct test_case {
    const char *name;           /* Test case name */
    test_func_t func;           /* Test function pointer */
    const char *file;           /* Source file */
    int line;                   /* Line number */
} test_case_t;

/**
 * Test suite statistics
 */
typedef struct test_stats {
    size_t total;               /* Total tests run */
    size_t passed;              /* Tests passed */
    size_t failed;              /* Tests failed */
    size_t skipped;             /* Tests skipped */
} test_stats_t;

/* Global test state */
extern test_case_t g_test_cases[TEST_MAX_CASES];
extern size_t g_test_count;
extern test_stats_t g_test_stats;

/* Current test failure info */
extern const char *g_test_fail_file;
extern int g_test_fail_line;
extern const char *g_test_fail_msg;

/**
 * Register a test case
 */
void test_register(const char *name, test_func_t func, const char *file, int line);

/**
 * Run all registered tests
 * @return Number of failed tests
 */
size_t test_run_all(void);

/**
 * Print test summary to serial console
 */
void test_print_summary(void);

/**
 * Reset test framework state
 */
void test_reset(void);

/**
 * Define a test case
 * Usage:
 *   TEST_CASE(test_name) {
 *       // test code
 *       TEST_PASS();
 *   }
 */
#define TEST_CASE(name)                                         \
    static int name(void);                                       \
    static void __attribute__((constructor)) _register_##name(void) { \
        test_register(#name, name, __FILE__, __LINE__);          \
    }                                                            \
    static int name(void)

/**
 * Alternative TEST_CASE for systems without constructor support
 * Use TEST_REGISTER in a setup function instead
 */
#define TEST_DEFINE(name) static int name(void)
#define TEST_REGISTER(name) test_register(#name, name, __FILE__, __LINE__)

/**
 * Assert that condition is true
 */
#define TEST_ASSERT(condition)                                   \
    do {                                                         \
        if (!(condition)) {                                      \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #condition;   \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that two values are equal
 */
#define TEST_ASSERT_EQ(a, b)                                     \
    do {                                                         \
        if ((a) != (b)) {                                        \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " == " #b; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that two values are not equal
 */
#define TEST_ASSERT_NE(a, b)                                     \
    do {                                                         \
        if ((a) == (b)) {                                        \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " != " #b; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that a value is greater than another
 */
#define TEST_ASSERT_GT(a, b)                                     \
    do {                                                         \
        if ((a) <= (b)) {                                        \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " > " #b;  \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that a value is less than another
 */
#define TEST_ASSERT_LT(a, b)                                     \
    do {                                                         \
        if ((a) >= (b)) {                                        \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " < " #b;  \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that a value is greater than or equal to another
 */
#define TEST_ASSERT_GE(a, b)                                     \
    do {                                                         \
        if ((a) < (b)) {                                         \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " >= " #b; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that a value is less than or equal to another
 */
#define TEST_ASSERT_LE(a, b)                                     \
    do {                                                         \
        if ((a) > (b)) {                                         \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #a " <= " #b; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that pointer is NULL
 */
#define TEST_ASSERT_NULL(ptr)                                    \
    do {                                                         \
        if ((ptr) != NULL) {                                     \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #ptr " is NULL"; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that pointer is not NULL
 */
#define TEST_ASSERT_NOT_NULL(ptr)                                \
    do {                                                         \
        if ((ptr) == NULL) {                                     \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: " #ptr " is not NULL"; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that two strings are equal
 */
#define TEST_ASSERT_STR_EQ(s1, s2)                               \
    do {                                                         \
        if (test_strcmp(s1, s2) != 0) {                          \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: strings equal"; \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Assert that two memory regions are equal
 */
#define TEST_ASSERT_MEM_EQ(m1, m2, n)                            \
    do {                                                         \
        if (test_memcmp(m1, m2, n) != 0) {                       \
            g_test_fail_file = __FILE__;                         \
            g_test_fail_line = __LINE__;                         \
            g_test_fail_msg = "assertion failed: memory equal";  \
            return TEST_RESULT_FAIL;                             \
        }                                                        \
    } while (0)

/**
 * Mark test as passed and return
 */
#define TEST_PASS()                                              \
    do {                                                         \
        return TEST_RESULT_PASS;                                 \
    } while (0)

/**
 * Mark test as failed with message and return
 */
#define TEST_FAIL(msg)                                           \
    do {                                                         \
        g_test_fail_file = __FILE__;                             \
        g_test_fail_line = __LINE__;                             \
        g_test_fail_msg = msg;                                   \
        return TEST_RESULT_FAIL;                                 \
    } while (0)

/**
 * Skip test with reason
 */
#define TEST_SKIP(reason)                                        \
    do {                                                         \
        g_test_fail_msg = reason;                                \
        return TEST_RESULT_SKIP;                                 \
    } while (0)

/* Helper functions for string/memory comparison in tests */
int test_strcmp(const char *s1, const char *s2);
int test_memcmp(const void *s1, const void *s2, size_t n);

#endif /* _AAAOS_TEST_H */
