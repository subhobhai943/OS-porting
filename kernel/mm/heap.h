/**
 * AAAos Kernel - Heap Memory Allocator
 *
 * Provides dynamic memory allocation for the kernel.
 * Uses a simple block allocator with a free list and first-fit strategy.
 */

#ifndef _AAAOS_MM_HEAP_H
#define _AAAOS_MM_HEAP_H

#include "../include/types.h"

/* Heap configuration */
#define HEAP_MIN_BLOCK_SIZE     32          /* Minimum allocation size */
#define HEAP_ALIGNMENT          16          /* Memory alignment */
#define HEAP_INITIAL_SIZE       (64 * KB)   /* Default initial heap size */
#define HEAP_EXPAND_SIZE        (64 * KB)   /* Size to expand heap by */
#define HEAP_MAX_SIZE           (16 * MB)   /* Maximum heap size */

/* Block header flags */
#define BLOCK_FLAG_USED         0x1         /* Block is allocated */
#define BLOCK_FLAG_LAST         0x2         /* Last block in heap */

/**
 * Block header structure
 * Each allocated/free block has this header
 */
typedef struct heap_block {
    size_t size;                    /* Block size (including header), low bit = used flag */
    struct heap_block *next;        /* Next block in free list (only valid when free) */
    struct heap_block *prev;        /* Previous block in memory (for coalescing) */
    uint32_t magic;                 /* Magic number for validation */
    uint32_t flags;                 /* Block flags */
} PACKED heap_block_t;

/* Magic number for block validation */
#define HEAP_BLOCK_MAGIC        0xDEADBEEF
#define HEAP_BLOCK_FREE_MAGIC   0xFEEDFACE

/**
 * Heap statistics structure
 */
typedef struct heap_stats {
    size_t total_size;              /* Total heap size */
    size_t used_size;               /* Currently allocated size */
    size_t free_size;               /* Currently free size */
    size_t block_count;             /* Total number of blocks */
    size_t free_block_count;        /* Number of free blocks */
    size_t alloc_count;             /* Total allocations made */
    size_t free_count;              /* Total frees made */
    size_t expand_count;            /* Number of heap expansions */
} heap_stats_t;

/**
 * Initialize the kernel heap
 * @param start Starting virtual address for heap
 * @param initial_size Initial heap size in bytes
 * @return true on success, false on failure
 */
bool heap_init(virtaddr_t start, size_t initial_size);

/**
 * Allocate memory from the heap
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *kmalloc(size_t size);

/**
 * Allocate aligned memory from the heap
 * @param size Number of bytes to allocate
 * @param alignment Required alignment (must be power of 2)
 * @return Pointer to aligned allocated memory, or NULL on failure
 */
void *kmalloc_aligned(size_t size, size_t alignment);

/**
 * Free previously allocated memory
 * @param ptr Pointer to memory to free (NULL is safe to pass)
 */
void kfree(void *ptr);

/**
 * Free memory allocated with kmalloc_aligned
 * @param ptr Pointer to aligned memory to free (NULL is safe to pass)
 */
void kfree_aligned(void *ptr);

/**
 * Allocate and zero-initialize memory
 * @param count Number of elements
 * @param size Size of each element
 * @return Pointer to zeroed memory, or NULL on failure
 */
void *kcalloc(size_t count, size_t size);

/**
 * Resize an allocation
 * @param ptr Pointer to existing allocation (NULL acts like kmalloc)
 * @param new_size New size in bytes (0 acts like kfree)
 * @return Pointer to resized memory, or NULL on failure
 */
void *krealloc(void *ptr, size_t new_size);

/**
 * Get heap statistics
 * @param stats Pointer to stats structure to fill
 */
void heap_get_stats(heap_stats_t *stats);

/**
 * Print heap statistics to serial console
 */
void heap_print_stats(void);

/**
 * Dump heap blocks for debugging
 */
void heap_dump(void);

/**
 * Validate heap integrity
 * @return true if heap is valid, false if corruption detected
 */
bool heap_validate(void);

/**
 * Expand the heap by requesting more pages
 * @param min_size Minimum size needed
 * @return true on success, false on failure
 */
bool heap_expand(size_t min_size);

#endif /* _AAAOS_MM_HEAP_H */
