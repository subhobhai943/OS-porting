/**
 * AAAos Kernel - Basic Type Definitions
 *
 * This header provides standard integer types and common type definitions
 * used throughout the kernel.
 */

#ifndef _AAAOS_TYPES_H
#define _AAAOS_TYPES_H

/* Fixed-width integer types */
typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef signed short        int16_t;
typedef unsigned short      uint16_t;
typedef signed int          int32_t;
typedef unsigned int        uint32_t;
typedef signed long long    int64_t;
typedef unsigned long long  uint64_t;

/* Size types */
typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef int64_t             ptrdiff_t;

/* Address types */
typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;
typedef uint64_t            physaddr_t;
typedef uint64_t            virtaddr_t;

/* Boolean type */
typedef _Bool               bool;
#define true                1
#define false               0

/* NULL pointer */
#ifndef NULL
#define NULL                ((void*)0)
#endif

/* Min/Max values */
#define INT8_MIN            (-128)
#define INT8_MAX            127
#define UINT8_MAX           255
#define INT16_MIN           (-32768)
#define INT16_MAX           32767
#define UINT16_MAX          65535
#define INT32_MIN           (-2147483647 - 1)
#define INT32_MAX           2147483647
#define UINT32_MAX          4294967295U
#define INT64_MIN           (-9223372036854775807LL - 1)
#define INT64_MAX           9223372036854775807LL
#define UINT64_MAX          18446744073709551615ULL

#define SIZE_MAX            UINT64_MAX

/* Common macros */
#define ALIGN_UP(x, align)      (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align)    (((x) & ((align) - 1)) == 0)

#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi)        MIN(MAX(x, lo), hi)

#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))

#define UNUSED(x)               ((void)(x))

/* Bit manipulation */
#define BIT(n)                  (1ULL << (n))
#define MASK(n)                 (BIT(n) - 1)
#define GET_BIT(x, n)           (((x) >> (n)) & 1)
#define SET_BIT(x, n)           ((x) | BIT(n))
#define CLEAR_BIT(x, n)         ((x) & ~BIT(n))
#define TOGGLE_BIT(x, n)        ((x) ^ BIT(n))

/* Memory sizes */
#define KB                      (1024ULL)
#define MB                      (1024ULL * KB)
#define GB                      (1024ULL * MB)
#define TB                      (1024ULL * GB)

/* Page size */
#define PAGE_SIZE               4096
#define PAGE_SHIFT              12
#define PAGE_MASK               (~(PAGE_SIZE - 1))

/* Compiler attributes */
#define PACKED                  __attribute__((packed))
#define ALIGNED(n)              __attribute__((aligned(n)))
#define NORETURN                __attribute__((noreturn))
#define UNUSED_ATTR             __attribute__((unused))
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)

/* Inline assembly helpers */
#define barrier()               __asm__ __volatile__("" ::: "memory")

#endif /* _AAAOS_TYPES_H */
