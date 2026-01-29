/**
 * AAAos C Library - String and Memory Functions
 *
 * This header provides standard C string and memory manipulation functions
 * for use in the kernel and userspace applications.
 */

#ifndef _AAAOS_STRING_H
#define _AAAOS_STRING_H

#include "../../kernel/include/types.h"

/*
 * String Functions
 */

/**
 * strlen - Calculate the length of a string
 * @str: The string to measure
 *
 * Returns the number of characters in @str, not including the null terminator.
 */
size_t strlen(const char *str);

/**
 * strcmp - Compare two strings
 * @s1: First string
 * @s2: Second string
 *
 * Returns negative if s1 < s2, zero if s1 == s2, positive if s1 > s2.
 */
int strcmp(const char *s1, const char *s2);

/**
 * strncmp - Compare at most n characters of two strings
 * @s1: First string
 * @s2: Second string
 * @n: Maximum number of characters to compare
 *
 * Returns negative if s1 < s2, zero if s1 == s2, positive if s1 > s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * strcpy - Copy a string
 * @dest: Destination buffer
 * @src: Source string
 *
 * Copies the string from @src to @dest, including the null terminator.
 * Returns @dest.
 *
 * WARNING: The destination buffer must be large enough to hold the source.
 */
char *strcpy(char *dest, const char *src);

/**
 * strncpy - Copy at most n characters of a string
 * @dest: Destination buffer
 * @src: Source string
 * @n: Maximum number of characters to copy
 *
 * Copies at most @n characters from @src to @dest. If @src is shorter than
 * @n characters, the remainder of @dest is filled with null bytes.
 * Returns @dest.
 *
 * WARNING: If @src is >= @n characters, @dest will NOT be null-terminated.
 */
char *strncpy(char *dest, const char *src, size_t n);

/**
 * strcat - Concatenate two strings
 * @dest: Destination string (must have space for additional characters)
 * @src: Source string to append
 *
 * Appends @src to the end of @dest, overwriting the null terminator at the
 * end of @dest, and adding a new null terminator.
 * Returns @dest.
 */
char *strcat(char *dest, const char *src);

/**
 * strchr - Find first occurrence of a character in a string
 * @str: String to search
 * @c: Character to find (passed as int, but treated as char)
 *
 * Returns a pointer to the first occurrence of @c in @str, or NULL if not found.
 * The terminating null byte is considered part of the string.
 */
char *strchr(const char *str, int c);

/**
 * strrchr - Find last occurrence of a character in a string
 * @str: String to search
 * @c: Character to find (passed as int, but treated as char)
 *
 * Returns a pointer to the last occurrence of @c in @str, or NULL if not found.
 * The terminating null byte is considered part of the string.
 */
char *strrchr(const char *str, int c);

/**
 * strstr - Find first occurrence of a substring
 * @haystack: String to search in
 * @needle: Substring to find
 *
 * Returns a pointer to the beginning of the first occurrence of @needle
 * in @haystack, or NULL if not found. If @needle is empty, returns @haystack.
 */
char *strstr(const char *haystack, const char *needle);

/*
 * Memory Functions
 */

/**
 * memset - Fill memory with a constant byte
 * @dest: Pointer to the memory area
 * @c: Byte value to fill with (passed as int, but treated as unsigned char)
 * @n: Number of bytes to fill
 *
 * Fills the first @n bytes of @dest with the byte value @c.
 * Returns @dest.
 */
void *memset(void *dest, int c, size_t n);

/**
 * memcpy - Copy memory area
 * @dest: Destination buffer
 * @src: Source buffer
 * @n: Number of bytes to copy
 *
 * Copies @n bytes from @src to @dest. The memory areas must not overlap.
 * Returns @dest.
 *
 * WARNING: If the memory areas overlap, use memmove instead.
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * memmove - Copy memory area (overlap safe)
 * @dest: Destination buffer
 * @src: Source buffer
 * @n: Number of bytes to copy
 *
 * Copies @n bytes from @src to @dest. The memory areas may overlap.
 * Returns @dest.
 */
void *memmove(void *dest, const void *src, size_t n);

/**
 * memcmp - Compare memory areas
 * @s1: First memory area
 * @s2: Second memory area
 * @n: Number of bytes to compare
 *
 * Compares the first @n bytes of @s1 and @s2.
 * Returns negative if s1 < s2, zero if equal, positive if s1 > s2.
 */
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* _AAAOS_STRING_H */
