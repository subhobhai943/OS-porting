/**
 * AAAos Kernel - String Function Tests
 *
 * Unit tests for the libc string and memory functions.
 */

#include "../framework/test.h"
#include "../../lib/libc/string.h"

/*
 * strlen tests
 */

/**
 * Test: strlen basic functionality
 */
TEST_CASE(test_strlen) {
    TEST_ASSERT_EQ(strlen("hello"), 5);
    TEST_ASSERT_EQ(strlen(""), 0);
    TEST_ASSERT_EQ(strlen("a"), 1);
    TEST_ASSERT_EQ(strlen("test string"), 11);

    TEST_PASS();
}

/**
 * Test: strlen with various strings
 */
TEST_CASE(test_strlen_various) {
    TEST_ASSERT_EQ(strlen("12345678901234567890"), 20);
    TEST_ASSERT_EQ(strlen("with spaces inside"), 18);
    TEST_ASSERT_EQ(strlen("tab\there"), 8);
    TEST_ASSERT_EQ(strlen("newline\n"), 8);

    TEST_PASS();
}

/*
 * strcmp tests
 */

/**
 * Test: strcmp equal strings
 */
TEST_CASE(test_strcmp) {
    TEST_ASSERT_EQ(strcmp("hello", "hello"), 0);
    TEST_ASSERT_EQ(strcmp("", ""), 0);
    TEST_ASSERT_EQ(strcmp("test", "test"), 0);

    TEST_PASS();
}

/**
 * Test: strcmp different strings
 */
TEST_CASE(test_strcmp_different) {
    /* First string less than second */
    TEST_ASSERT_LT(strcmp("abc", "abd"), 0);
    TEST_ASSERT_LT(strcmp("abc", "abcd"), 0);
    TEST_ASSERT_LT(strcmp("", "a"), 0);

    /* First string greater than second */
    TEST_ASSERT_GT(strcmp("abd", "abc"), 0);
    TEST_ASSERT_GT(strcmp("abcd", "abc"), 0);
    TEST_ASSERT_GT(strcmp("a", ""), 0);

    TEST_PASS();
}

/**
 * Test: strncmp functionality
 */
TEST_CASE(test_strncmp) {
    /* Equal within n */
    TEST_ASSERT_EQ(strncmp("hello", "hello", 5), 0);
    TEST_ASSERT_EQ(strncmp("hello", "helloworld", 5), 0);
    TEST_ASSERT_EQ(strncmp("abc", "abd", 2), 0);

    /* Different within n */
    TEST_ASSERT_NE(strncmp("abc", "abd", 3), 0);
    TEST_ASSERT_LT(strncmp("abc", "abd", 3), 0);
    TEST_ASSERT_GT(strncmp("abd", "abc", 3), 0);

    /* n = 0 always equal */
    TEST_ASSERT_EQ(strncmp("abc", "xyz", 0), 0);

    TEST_PASS();
}

/*
 * strcpy tests
 */

/**
 * Test: strcpy basic functionality
 */
TEST_CASE(test_strcpy) {
    char dest[32];
    char *result;

    result = strcpy(dest, "hello");
    TEST_ASSERT_EQ(result, dest);
    TEST_ASSERT_STR_EQ(dest, "hello");

    /* Empty string */
    result = strcpy(dest, "");
    TEST_ASSERT_STR_EQ(dest, "");

    TEST_PASS();
}

/**
 * Test: strncpy functionality
 */
TEST_CASE(test_strncpy) {
    char dest[32];

    /* Normal copy */
    memset(dest, 'X', sizeof(dest));
    strncpy(dest, "hello", 10);
    TEST_ASSERT_STR_EQ(dest, "hello");
    /* Verify null padding */
    TEST_ASSERT_EQ(dest[5], '\0');
    TEST_ASSERT_EQ(dest[6], '\0');

    /* Truncated copy (no null terminator) */
    memset(dest, 'X', sizeof(dest));
    strncpy(dest, "hello", 3);
    TEST_ASSERT_EQ(dest[0], 'h');
    TEST_ASSERT_EQ(dest[1], 'e');
    TEST_ASSERT_EQ(dest[2], 'l');
    TEST_ASSERT_EQ(dest[3], 'X');  /* Not null terminated */

    TEST_PASS();
}

/**
 * Test: strcat functionality
 */
TEST_CASE(test_strcat) {
    char dest[32];

    strcpy(dest, "hello");
    strcat(dest, " world");
    TEST_ASSERT_STR_EQ(dest, "hello world");

    /* Concatenate to empty */
    strcpy(dest, "");
    strcat(dest, "test");
    TEST_ASSERT_STR_EQ(dest, "test");

    /* Concatenate empty */
    strcpy(dest, "test");
    strcat(dest, "");
    TEST_ASSERT_STR_EQ(dest, "test");

    TEST_PASS();
}

/*
 * strchr tests
 */

/**
 * Test: strchr functionality
 */
TEST_CASE(test_strchr) {
    const char *str = "hello world";
    char *result;

    /* Find existing character */
    result = strchr(str, 'w');
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(*result, 'w');
    TEST_ASSERT_EQ(result - str, 6);

    /* Find first occurrence */
    result = strchr(str, 'l');
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(result - str, 2);

    /* Find null terminator */
    result = strchr(str, '\0');
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(*result, '\0');

    /* Character not found */
    result = strchr(str, 'z');
    TEST_ASSERT_NULL(result);

    TEST_PASS();
}

/**
 * Test: strrchr functionality
 */
TEST_CASE(test_strrchr) {
    const char *str = "hello world";
    char *result;

    /* Find last occurrence */
    result = strrchr(str, 'l');
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(result - str, 9);

    /* Single occurrence */
    result = strrchr(str, 'w');
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(result - str, 6);

    /* Not found */
    result = strrchr(str, 'z');
    TEST_ASSERT_NULL(result);

    TEST_PASS();
}

/**
 * Test: strstr functionality
 */
TEST_CASE(test_strstr) {
    const char *str = "hello world hello";
    char *result;

    /* Find substring */
    result = strstr(str, "world");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(result - str, 6);

    /* Find first occurrence */
    result = strstr(str, "hello");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQ(result - str, 0);

    /* Empty needle returns haystack */
    result = strstr(str, "");
    TEST_ASSERT_EQ(result, str);

    /* Not found */
    result = strstr(str, "xyz");
    TEST_ASSERT_NULL(result);

    TEST_PASS();
}

/*
 * memcpy tests
 */

/**
 * Test: memcpy basic functionality
 */
TEST_CASE(test_memcpy) {
    uint8_t src[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t dest[16];
    void *result;
    int i;

    result = memcpy(dest, src, 16);
    TEST_ASSERT_EQ(result, dest);

    for (i = 0; i < 16; i++) {
        TEST_ASSERT_EQ(dest[i], src[i]);
    }

    TEST_PASS();
}

/**
 * Test: memcpy with various sizes
 */
TEST_CASE(test_memcpy_sizes) {
    uint8_t src[256];
    uint8_t dest[256];
    int i;

    /* Fill source with pattern */
    for (i = 0; i < 256; i++) {
        src[i] = (uint8_t)i;
    }

    /* Test various sizes */
    memcpy(dest, src, 1);
    TEST_ASSERT_EQ(dest[0], 0);

    memcpy(dest, src, 64);
    TEST_ASSERT_MEM_EQ(dest, src, 64);

    memcpy(dest, src, 256);
    TEST_ASSERT_MEM_EQ(dest, src, 256);

    /* Zero bytes */
    dest[0] = 0xFF;
    memcpy(dest, src, 0);
    TEST_ASSERT_EQ(dest[0], 0xFF);  /* Unchanged */

    TEST_PASS();
}

/**
 * Test: memmove overlapping (forward)
 */
TEST_CASE(test_memmove) {
    uint8_t buf[32];
    int i;

    /* Initialize buffer */
    for (i = 0; i < 32; i++) {
        buf[i] = (uint8_t)i;
    }

    /* Move forward (overlapping) */
    memmove(buf + 4, buf, 16);

    /* Verify source data was preserved during copy */
    for (i = 0; i < 16; i++) {
        TEST_ASSERT_EQ(buf[i + 4], i);
    }

    TEST_PASS();
}

/**
 * Test: memmove overlapping (backward)
 */
TEST_CASE(test_memmove_backward) {
    uint8_t buf[32];
    int i;

    /* Initialize buffer */
    for (i = 0; i < 32; i++) {
        buf[i] = (uint8_t)i;
    }

    /* Move backward (overlapping) */
    memmove(buf, buf + 4, 16);

    /* Verify */
    for (i = 0; i < 16; i++) {
        TEST_ASSERT_EQ(buf[i], i + 4);
    }

    TEST_PASS();
}

/*
 * memset tests
 */

/**
 * Test: memset basic functionality
 */
TEST_CASE(test_memset) {
    uint8_t buf[64];
    void *result;
    int i;

    result = memset(buf, 0xAA, 64);
    TEST_ASSERT_EQ(result, buf);

    for (i = 0; i < 64; i++) {
        TEST_ASSERT_EQ(buf[i], 0xAA);
    }

    TEST_PASS();
}

/**
 * Test: memset with zero
 */
TEST_CASE(test_memset_zero) {
    uint8_t buf[64];
    int i;

    /* Fill with non-zero first */
    for (i = 0; i < 64; i++) {
        buf[i] = 0xFF;
    }

    /* Zero it */
    memset(buf, 0, 64);

    for (i = 0; i < 64; i++) {
        TEST_ASSERT_EQ(buf[i], 0);
    }

    TEST_PASS();
}

/**
 * Test: memset partial buffer
 */
TEST_CASE(test_memset_partial) {
    uint8_t buf[64];
    int i;

    /* Fill entire buffer */
    memset(buf, 0xAA, 64);

    /* Overwrite middle portion */
    memset(buf + 16, 0xBB, 32);

    /* Verify */
    for (i = 0; i < 16; i++) {
        TEST_ASSERT_EQ(buf[i], 0xAA);
    }
    for (i = 16; i < 48; i++) {
        TEST_ASSERT_EQ(buf[i], 0xBB);
    }
    for (i = 48; i < 64; i++) {
        TEST_ASSERT_EQ(buf[i], 0xAA);
    }

    TEST_PASS();
}

/*
 * memcmp tests
 */

/**
 * Test: memcmp equal memory
 */
TEST_CASE(test_memcmp) {
    uint8_t buf1[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t buf2[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    TEST_ASSERT_EQ(memcmp(buf1, buf2, 16), 0);
    TEST_ASSERT_EQ(memcmp(buf1, buf2, 8), 0);
    TEST_ASSERT_EQ(memcmp(buf1, buf2, 0), 0);

    TEST_PASS();
}

/**
 * Test: memcmp different memory
 */
TEST_CASE(test_memcmp_different) {
    uint8_t buf1[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint8_t buf2[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16};

    /* Last byte different */
    TEST_ASSERT_LT(memcmp(buf1, buf2, 16), 0);
    TEST_ASSERT_GT(memcmp(buf2, buf1, 16), 0);

    /* But equal for first 15 bytes */
    TEST_ASSERT_EQ(memcmp(buf1, buf2, 15), 0);

    TEST_PASS();
}
