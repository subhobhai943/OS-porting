/**
 * AAAos C Library - String and Memory Functions Implementation
 *
 * Freestanding implementations of standard C string and memory functions.
 * These implementations have no external dependencies.
 */

#include "string.h"

/*
 * String Functions
 */

size_t strlen(const char *str)
{
    const char *s = str;
    while (*s) {
        s++;
    }
    return (size_t)(s - str);
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) {
        return 0;
    }

    while (n > 1 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++)) {
        /* Empty loop body - assignment in condition does the work */
    }
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;

    /* Copy characters from src */
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }

    /* Pad with null bytes if src was shorter than n */
    while (n > 0) {
        *d++ = '\0';
        n--;
    }

    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;

    /* Find the end of dest */
    while (*d) {
        d++;
    }

    /* Copy src to the end of dest */
    while ((*d++ = *src++)) {
        /* Empty loop body - assignment in condition does the work */
    }

    return dest;
}

char *strchr(const char *str, int c)
{
    char ch = (char)c;

    while (*str) {
        if (*str == ch) {
            return (char *)str;
        }
        str++;
    }

    /* Check for null terminator match */
    if (ch == '\0') {
        return (char *)str;
    }

    return NULL;
}

char *strrchr(const char *str, int c)
{
    char ch = (char)c;
    const char *last = NULL;

    while (*str) {
        if (*str == ch) {
            last = str;
        }
        str++;
    }

    /* Check for null terminator match */
    if (ch == '\0') {
        return (char *)str;
    }

    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    const char *h, *n;

    /* Empty needle matches at the start */
    if (*needle == '\0') {
        return (char *)haystack;
    }

    while (*haystack) {
        /* Check if needle starts here */
        h = haystack;
        n = needle;

        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }

        /* If we reached the end of needle, we found a match */
        if (*n == '\0') {
            return (char *)haystack;
        }

        haystack++;
    }

    return NULL;
}

/*
 * Memory Functions
 */

void *memset(void *dest, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    unsigned char byte = (unsigned char)c;

    while (n > 0) {
        *d++ = byte;
        n--;
    }

    return dest;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    while (n > 0) {
        *d++ = *s++;
        n--;
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (n == 0 || dest == src) {
        return dest;
    }

    /* Check for overlap and copy direction */
    if (d < s || d >= s + n) {
        /* No overlap or dest is before src, copy forward */
        while (n > 0) {
            *d++ = *s++;
            n--;
        }
    } else {
        /* Overlap with dest after src, copy backward */
        d += n;
        s += n;
        while (n > 0) {
            *--d = *--s;
            n--;
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n > 0) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
        n--;
    }

    return 0;
}
