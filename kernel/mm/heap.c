/**
 * AAAos Kernel - Heap Memory Allocator Implementation
 *
 * Simple block allocator with:
 * - Free list for tracking available blocks
 * - First-fit allocation strategy
 * - Block coalescing on free
 * - Automatic heap expansion via PMM
 */

#include "heap.h"
#include "pmm.h"
#include "../include/serial.h"

/* Heap state */
static virtaddr_t heap_start = 0;
static virtaddr_t heap_end = 0;
static virtaddr_t heap_max = 0;
static heap_block_t *free_list = NULL;
static bool heap_initialized = false;

/* Heap statistics */
static heap_stats_t heap_stats = {0};

/* Simple spinlock for thread safety */
static volatile int heap_lock = 0;

/**
 * Acquire heap lock
 */
static inline void heap_acquire_lock(void) {
    while (__sync_lock_test_and_set(&heap_lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

/**
 * Release heap lock
 */
static inline void heap_release_lock(void) {
    __sync_lock_release(&heap_lock);
}

/**
 * Get block size (masking out flags from size field)
 */
static inline size_t block_get_size(heap_block_t *block) {
    return block->size & ~(HEAP_ALIGNMENT - 1);
}

/**
 * Check if block is used
 */
static inline bool block_is_used(heap_block_t *block) {
    return (block->flags & BLOCK_FLAG_USED) != 0;
}

/**
 * Set block as used
 */
static inline void block_set_used(heap_block_t *block) {
    block->flags |= BLOCK_FLAG_USED;
    block->magic = HEAP_BLOCK_MAGIC;
}

/**
 * Set block as free
 */
static inline void block_set_free(heap_block_t *block) {
    block->flags &= ~BLOCK_FLAG_USED;
    block->magic = HEAP_BLOCK_FREE_MAGIC;
}

/**
 * Align size up to heap alignment
 */
static inline size_t align_size(size_t size) {
    return ALIGN_UP(size, HEAP_ALIGNMENT);
}

/**
 * Get data pointer from block
 */
static inline void *block_to_data(heap_block_t *block) {
    return (void*)((virtaddr_t)block + sizeof(heap_block_t));
}

/**
 * Get block from data pointer
 */
static inline heap_block_t *data_to_block(void *ptr) {
    return (heap_block_t*)((virtaddr_t)ptr - sizeof(heap_block_t));
}

/**
 * Get next block in memory
 */
static inline heap_block_t *block_get_next_physical(heap_block_t *block) {
    virtaddr_t next = (virtaddr_t)block + block_get_size(block);
    if (next >= heap_end) {
        return NULL;
    }
    return (heap_block_t*)next;
}

/**
 * Simple memset implementation
 */
static void heap_memset(void *dest, uint8_t val, size_t count) {
    uint8_t *d = (uint8_t*)dest;
    while (count--) {
        *d++ = val;
    }
}

/**
 * Simple memcpy implementation
 */
static void heap_memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (count--) {
        *d++ = *s++;
    }
}

/**
 * Add block to free list (sorted by address for coalescing)
 */
static void free_list_add(heap_block_t *block) {
    block_set_free(block);

    /* Empty list case */
    if (free_list == NULL) {
        free_list = block;
        block->next = NULL;
        heap_stats.free_block_count++;
        return;
    }

    /* Insert at head if block comes before current head */
    if ((virtaddr_t)block < (virtaddr_t)free_list) {
        block->next = free_list;
        free_list = block;
        heap_stats.free_block_count++;
        return;
    }

    /* Find insertion point (sorted by address) */
    heap_block_t *current = free_list;
    while (current->next != NULL && (virtaddr_t)current->next < (virtaddr_t)block) {
        current = current->next;
    }

    /* Insert after current */
    block->next = current->next;
    current->next = block;
    heap_stats.free_block_count++;
}

/**
 * Remove block from free list
 */
static void free_list_remove(heap_block_t *block) {
    if (free_list == NULL) {
        return;
    }

    /* Remove from head */
    if (free_list == block) {
        free_list = block->next;
        block->next = NULL;
        heap_stats.free_block_count--;
        return;
    }

    /* Find and remove */
    heap_block_t *current = free_list;
    while (current->next != NULL && current->next != block) {
        current = current->next;
    }

    if (current->next == block) {
        current->next = block->next;
        block->next = NULL;
        heap_stats.free_block_count--;
    }
}

/**
 * Coalesce adjacent free blocks
 */
static void coalesce_blocks(heap_block_t *block) {
    if (block == NULL || block_is_used(block)) {
        return;
    }

    /* Try to coalesce with next block in memory */
    heap_block_t *next = block_get_next_physical(block);
    if (next != NULL && !block_is_used(next)) {
        /* Remove next from free list */
        free_list_remove(next);

        /* Merge sizes */
        block->size = block_get_size(block) + block_get_size(next);

        /* Update prev pointer of block after next */
        heap_block_t *after_next = block_get_next_physical(block);
        if (after_next != NULL) {
            after_next->prev = block;
        }

        kprintf("[HEAP] Coalesced blocks at %p, new size: %llu\n",
                (void*)block, (uint64_t)block_get_size(block));
    }

    /* Try to coalesce with previous block */
    heap_block_t *prev = block->prev;
    if (prev != NULL && !block_is_used(prev)) {
        /* Remove current block from free list */
        free_list_remove(block);

        /* Merge sizes */
        prev->size = block_get_size(prev) + block_get_size(block);

        /* Update prev pointer of block after current */
        heap_block_t *after_block = block_get_next_physical(prev);
        if (after_block != NULL) {
            after_block->prev = prev;
        }

        kprintf("[HEAP] Coalesced with previous at %p, new size: %llu\n",
                (void*)prev, (uint64_t)block_get_size(prev));
    }
}

/**
 * Find a free block using first-fit strategy
 */
static heap_block_t *find_free_block(size_t size) {
    heap_block_t *current = free_list;

    while (current != NULL) {
        if (block_get_size(current) >= size) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

/**
 * Split a block if it's large enough
 */
static void split_block(heap_block_t *block, size_t size) {
    size_t block_size = block_get_size(block);
    size_t remaining = block_size - size;

    /* Only split if remaining space can hold header + minimum allocation */
    size_t min_split = sizeof(heap_block_t) + HEAP_MIN_BLOCK_SIZE;

    if (remaining < min_split) {
        /* Don't split - use entire block */
        return;
    }

    /* Create new free block in remaining space */
    heap_block_t *new_block = (heap_block_t*)((virtaddr_t)block + size);
    new_block->size = remaining;
    new_block->prev = block;
    new_block->next = NULL;
    new_block->flags = 0;
    block_set_free(new_block);

    /* Update block size */
    block->size = size;

    /* Update next block's prev pointer */
    heap_block_t *after_new = block_get_next_physical(new_block);
    if (after_new != NULL) {
        after_new->prev = new_block;
    }

    /* Add new block to free list */
    free_list_add(new_block);
    heap_stats.block_count++;

    kprintf("[HEAP] Split block: %llu -> %llu + %llu\n",
            (uint64_t)block_size, (uint64_t)size, (uint64_t)remaining);
}

/**
 * Expand the heap by requesting more pages from PMM
 */
bool heap_expand(size_t min_size) {
    /* Calculate pages needed */
    size_t expand_size = MAX(min_size, HEAP_EXPAND_SIZE);
    expand_size = ALIGN_UP(expand_size, PAGE_SIZE);

    /* Check if we would exceed max heap size */
    if (heap_end + expand_size > heap_max) {
        kprintf("[HEAP] Error: Cannot expand, would exceed max size\n");
        return false;
    }

    /* Allocate physical pages */
    size_t pages_needed = expand_size / PAGE_SIZE;
    physaddr_t phys = pmm_alloc_pages(pages_needed);

    if (phys == 0) {
        kprintf("[HEAP] Error: PMM allocation failed for heap expansion\n");
        return false;
    }

    /*
     * Note: In a full implementation, we would use VMM to map these pages.
     * For now, assume identity mapping (physical == virtual) or that
     * the kernel has direct physical memory access.
     *
     * TODO: Replace with vmm_map_pages() when VMM is implemented
     */
    virtaddr_t new_pages = (virtaddr_t)phys;

    /* If heap hasn't been initialized with contiguous memory, this won't work well */
    if (new_pages != heap_end) {
        kprintf("[HEAP] Warning: Non-contiguous heap expansion at %p (expected %p)\n",
                (void*)new_pages, (void*)heap_end);
        /* For now, we'll try to use it anyway */
        /* In production, you'd need VMM to map this to the right virtual address */
    }

    /* Create a free block for the new space */
    heap_block_t *new_block = (heap_block_t*)heap_end;
    new_block->size = expand_size;
    new_block->next = NULL;
    new_block->flags = 0;

    /* Find previous block */
    virtaddr_t prev_addr = heap_end - sizeof(heap_block_t);
    heap_block_t *prev_block = NULL;

    /* Walk backwards to find the last block */
    heap_block_t *current = (heap_block_t*)heap_start;
    while (current != NULL) {
        heap_block_t *next_phys = block_get_next_physical(current);
        if (next_phys == NULL || (virtaddr_t)next_phys >= heap_end) {
            prev_block = current;
            break;
        }
        current = next_phys;
    }

    new_block->prev = prev_block;
    block_set_free(new_block);

    /* Update heap end */
    heap_end += expand_size;
    heap_stats.total_size += expand_size;
    heap_stats.free_size += expand_size;
    heap_stats.block_count++;
    heap_stats.expand_count++;

    /* Add to free list */
    free_list_add(new_block);

    /* Try to coalesce with previous block */
    coalesce_blocks(new_block);

    kprintf("[HEAP] Expanded heap by %llu bytes (%llu pages), new end: %p\n",
            (uint64_t)expand_size, (uint64_t)pages_needed, (void*)heap_end);

    return true;
}

/**
 * Initialize the kernel heap
 */
bool heap_init(virtaddr_t start, size_t initial_size) {
    if (heap_initialized) {
        kprintf("[HEAP] Warning: Heap already initialized\n");
        return false;
    }

    kprintf("[HEAP] Initializing kernel heap...\n");
    kprintf("[HEAP]   Start address: %p\n", (void*)start);
    kprintf("[HEAP]   Initial size:  %llu bytes\n", (uint64_t)initial_size);

    /* Align start and size */
    start = ALIGN_UP(start, PAGE_SIZE);
    initial_size = ALIGN_UP(initial_size, PAGE_SIZE);

    if (initial_size < PAGE_SIZE) {
        initial_size = PAGE_SIZE;
    }

    /* Allocate physical pages for initial heap */
    size_t pages = initial_size / PAGE_SIZE;
    physaddr_t phys = pmm_alloc_pages(pages);

    if (phys == 0) {
        kprintf("[HEAP] Error: Failed to allocate initial heap pages\n");
        return false;
    }

    /*
     * TODO: When VMM is available, map pages to the desired virtual address.
     * For now, use identity mapping.
     */
    heap_start = (virtaddr_t)phys;
    heap_end = heap_start + initial_size;
    heap_max = heap_start + HEAP_MAX_SIZE;

    /* Clear heap memory */
    heap_memset((void*)heap_start, 0, initial_size);

    /* Create initial free block spanning entire heap */
    heap_block_t *initial_block = (heap_block_t*)heap_start;
    initial_block->size = initial_size;
    initial_block->next = NULL;
    initial_block->prev = NULL;
    initial_block->flags = 0;
    block_set_free(initial_block);

    /* Initialize free list */
    free_list = initial_block;

    /* Initialize statistics */
    heap_stats.total_size = initial_size;
    heap_stats.used_size = 0;
    heap_stats.free_size = initial_size - sizeof(heap_block_t);
    heap_stats.block_count = 1;
    heap_stats.free_block_count = 1;
    heap_stats.alloc_count = 0;
    heap_stats.free_count = 0;
    heap_stats.expand_count = 0;

    heap_initialized = true;

    kprintf("[HEAP] Heap initialized successfully\n");
    kprintf("[HEAP]   Actual start: %p\n", (void*)heap_start);
    kprintf("[HEAP]   End:          %p\n", (void*)heap_end);
    kprintf("[HEAP]   Max end:      %p\n", (void*)heap_max);

    return true;
}

/**
 * Allocate memory from the heap
 */
void *kmalloc(size_t size) {
    if (!heap_initialized) {
        kprintf("[HEAP] Error: Heap not initialized\n");
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    heap_acquire_lock();

    /* Calculate actual size needed (header + data, aligned) */
    size_t actual_size = align_size(sizeof(heap_block_t) + MAX(size, HEAP_MIN_BLOCK_SIZE));

    /* Find a free block */
    heap_block_t *block = find_free_block(actual_size);

    /* If no block found, try to expand heap */
    if (block == NULL) {
        kprintf("[HEAP] No suitable block found, expanding heap...\n");

        if (!heap_expand(actual_size)) {
            heap_release_lock();
            kprintf("[HEAP] Error: Failed to allocate %llu bytes\n", (uint64_t)size);
            return NULL;
        }

        /* Try again after expansion */
        block = find_free_block(actual_size);
        if (block == NULL) {
            heap_release_lock();
            kprintf("[HEAP] Error: Still no suitable block after expansion\n");
            return NULL;
        }
    }

    /* Remove block from free list */
    free_list_remove(block);

    /* Split block if it's much larger than needed */
    split_block(block, actual_size);

    /* Mark block as used */
    block_set_used(block);

    /* Update statistics */
    size_t block_size = block_get_size(block);
    heap_stats.used_size += block_size;
    heap_stats.free_size -= block_size;
    heap_stats.alloc_count++;

    heap_release_lock();

    void *ptr = block_to_data(block);

    kprintf("[HEAP] Allocated %llu bytes at %p (block: %p, size: %llu)\n",
            (uint64_t)size, ptr, (void*)block, (uint64_t)block_size);

    return ptr;
}

/**
 * Allocate aligned memory
 */
void *kmalloc_aligned(size_t size, size_t alignment) {
    if (!heap_initialized || size == 0) {
        return NULL;
    }

    /* Alignment must be power of 2 */
    if ((alignment & (alignment - 1)) != 0) {
        kprintf("[HEAP] Error: Alignment must be power of 2\n");
        return NULL;
    }

    /* Allocate extra space for alignment */
    size_t extra = alignment + sizeof(void*);
    void *ptr = kmalloc(size + extra);

    if (ptr == NULL) {
        return NULL;
    }

    /* Calculate aligned address */
    virtaddr_t aligned = ALIGN_UP((virtaddr_t)ptr + sizeof(void*), alignment);

    /* Store original pointer before aligned address */
    ((void**)aligned)[-1] = ptr;

    return (void*)aligned;
}

/**
 * Free memory allocated with kmalloc_aligned
 */
void kfree_aligned(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    /* Retrieve original pointer */
    void *original = ((void**)ptr)[-1];
    kfree(original);
}

/**
 * Free previously allocated memory
 */
void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    if (!heap_initialized) {
        kprintf("[HEAP] Error: Heap not initialized\n");
        return;
    }

    heap_acquire_lock();

    /* Get block header */
    heap_block_t *block = data_to_block(ptr);

    /* Validate block */
    if (block->magic != HEAP_BLOCK_MAGIC) {
        heap_release_lock();
        kprintf("[HEAP] Error: Invalid block magic at %p (expected 0x%x, got 0x%x)\n",
                ptr, HEAP_BLOCK_MAGIC, block->magic);
        return;
    }

    if (!block_is_used(block)) {
        heap_release_lock();
        kprintf("[HEAP] Warning: Double free detected at %p\n", ptr);
        return;
    }

    /* Validate block is within heap bounds */
    if ((virtaddr_t)block < heap_start || (virtaddr_t)block >= heap_end) {
        heap_release_lock();
        kprintf("[HEAP] Error: Block at %p is outside heap bounds\n", (void*)block);
        return;
    }

    size_t block_size = block_get_size(block);

    kprintf("[HEAP] Freeing %p (block: %p, size: %llu)\n",
            ptr, (void*)block, (uint64_t)block_size);

    /* Mark as free and add to free list */
    block_set_free(block);
    free_list_add(block);

    /* Update statistics */
    heap_stats.used_size -= block_size;
    heap_stats.free_size += block_size;
    heap_stats.free_count++;

    /* Try to coalesce with adjacent blocks */
    coalesce_blocks(block);

    heap_release_lock();
}

/**
 * Allocate and zero-initialize memory
 */
void *kcalloc(size_t count, size_t size) {
    /* Check for overflow */
    if (count != 0 && size > SIZE_MAX / count) {
        kprintf("[HEAP] Error: kcalloc overflow\n");
        return NULL;
    }

    size_t total = count * size;
    void *ptr = kmalloc(total);

    if (ptr != NULL) {
        heap_memset(ptr, 0, total);
    }

    return ptr;
}

/**
 * Resize an allocation
 */
void *krealloc(void *ptr, size_t new_size) {
    /* NULL ptr acts like kmalloc */
    if (ptr == NULL) {
        return kmalloc(new_size);
    }

    /* Zero size acts like kfree */
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    if (!heap_initialized) {
        kprintf("[HEAP] Error: Heap not initialized\n");
        return NULL;
    }

    /* Get current block */
    heap_block_t *block = data_to_block(ptr);

    /* Validate block */
    if (block->magic != HEAP_BLOCK_MAGIC || !block_is_used(block)) {
        kprintf("[HEAP] Error: Invalid block in krealloc\n");
        return NULL;
    }

    size_t current_size = block_get_size(block) - sizeof(heap_block_t);
    size_t actual_new_size = align_size(sizeof(heap_block_t) + MAX(new_size, HEAP_MIN_BLOCK_SIZE));

    /* If shrinking and current block is big enough, just return same ptr */
    if (new_size <= current_size) {
        /* Could split block here, but for simplicity just keep it */
        kprintf("[HEAP] krealloc: shrinking, keeping same block\n");
        return ptr;
    }

    /* Check if we can expand in place */
    heap_acquire_lock();

    heap_block_t *next = block_get_next_physical(block);
    if (next != NULL && !block_is_used(next)) {
        size_t combined = block_get_size(block) + block_get_size(next);
        if (combined >= actual_new_size) {
            /* Remove next from free list and merge */
            free_list_remove(next);
            block->size = combined;

            /* Update stats */
            heap_stats.free_size -= block_get_size(next);
            heap_stats.block_count--;

            /* Update next's next prev pointer */
            heap_block_t *after_next = block_get_next_physical(block);
            if (after_next != NULL) {
                after_next->prev = block;
            }

            /* Split if much larger than needed */
            split_block(block, actual_new_size);

            heap_release_lock();
            kprintf("[HEAP] krealloc: expanded in place to %llu bytes\n",
                    (uint64_t)new_size);
            return ptr;
        }
    }

    heap_release_lock();

    /* Must allocate new block and copy */
    void *new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;  /* Original block unchanged */
    }

    /* Copy old data */
    heap_memcpy(new_ptr, ptr, current_size);

    /* Free old block */
    kfree(ptr);

    kprintf("[HEAP] krealloc: moved to new block at %p\n", new_ptr);
    return new_ptr;
}

/**
 * Get heap statistics
 */
void heap_get_stats(heap_stats_t *stats) {
    if (stats == NULL) {
        return;
    }

    heap_acquire_lock();
    *stats = heap_stats;
    heap_release_lock();
}

/**
 * Print heap statistics
 */
void heap_print_stats(void) {
    heap_stats_t stats;
    heap_get_stats(&stats);

    kprintf("[HEAP] === Heap Statistics ===\n");
    kprintf("[HEAP]   Total size:       %llu bytes (%llu KB)\n",
            (uint64_t)stats.total_size, (uint64_t)(stats.total_size / KB));
    kprintf("[HEAP]   Used size:        %llu bytes\n", (uint64_t)stats.used_size);
    kprintf("[HEAP]   Free size:        %llu bytes\n", (uint64_t)stats.free_size);
    kprintf("[HEAP]   Block count:      %llu\n", (uint64_t)stats.block_count);
    kprintf("[HEAP]   Free blocks:      %llu\n", (uint64_t)stats.free_block_count);
    kprintf("[HEAP]   Allocations:      %llu\n", (uint64_t)stats.alloc_count);
    kprintf("[HEAP]   Frees:            %llu\n", (uint64_t)stats.free_count);
    kprintf("[HEAP]   Expansions:       %llu\n", (uint64_t)stats.expand_count);
    kprintf("[HEAP] ========================\n");
}

/**
 * Dump heap blocks for debugging
 */
void heap_dump(void) {
    if (!heap_initialized) {
        kprintf("[HEAP] Heap not initialized\n");
        return;
    }

    heap_acquire_lock();

    kprintf("[HEAP] === Heap Dump ===\n");
    kprintf("[HEAP] Start: %p, End: %p\n", (void*)heap_start, (void*)heap_end);

    heap_block_t *block = (heap_block_t*)heap_start;
    int block_num = 0;

    while ((virtaddr_t)block < heap_end) {
        kprintf("[HEAP] Block %d: addr=%p size=%llu %s magic=0x%x\n",
                block_num,
                (void*)block,
                (uint64_t)block_get_size(block),
                block_is_used(block) ? "USED" : "FREE",
                block->magic);

        block = block_get_next_physical(block);
        if (block == NULL) {
            break;
        }
        block_num++;

        /* Safety limit */
        if (block_num > 1000) {
            kprintf("[HEAP] Warning: Too many blocks, stopping dump\n");
            break;
        }
    }

    kprintf("[HEAP] Free list:\n");
    heap_block_t *free_block = free_list;
    while (free_block != NULL) {
        kprintf("[HEAP]   Free: %p size=%llu\n",
                (void*)free_block, (uint64_t)block_get_size(free_block));
        free_block = free_block->next;
    }

    kprintf("[HEAP] ==================\n");

    heap_release_lock();
}

/**
 * Validate heap integrity
 */
bool heap_validate(void) {
    if (!heap_initialized) {
        kprintf("[HEAP] Heap not initialized\n");
        return false;
    }

    heap_acquire_lock();

    bool valid = true;
    heap_block_t *block = (heap_block_t*)heap_start;
    heap_block_t *prev = NULL;
    size_t total_size = 0;
    size_t block_count = 0;

    while ((virtaddr_t)block < heap_end) {
        /* Check magic */
        if (block_is_used(block)) {
            if (block->magic != HEAP_BLOCK_MAGIC) {
                kprintf("[HEAP] Validation failed: Bad magic for used block at %p\n",
                        (void*)block);
                valid = false;
            }
        } else {
            if (block->magic != HEAP_BLOCK_FREE_MAGIC) {
                kprintf("[HEAP] Validation failed: Bad magic for free block at %p\n",
                        (void*)block);
                valid = false;
            }
        }

        /* Check prev pointer */
        if (block->prev != prev) {
            kprintf("[HEAP] Validation failed: Bad prev pointer at %p\n",
                    (void*)block);
            valid = false;
        }

        /* Check size is reasonable */
        size_t block_size = block_get_size(block);
        if (block_size < sizeof(heap_block_t) || block_size > heap_end - (virtaddr_t)block) {
            kprintf("[HEAP] Validation failed: Invalid size %llu at %p\n",
                    (uint64_t)block_size, (void*)block);
            valid = false;
            break;
        }

        total_size += block_size;
        block_count++;
        prev = block;
        block = block_get_next_physical(block);

        /* Safety limit */
        if (block_count > 10000) {
            kprintf("[HEAP] Validation failed: Too many blocks\n");
            valid = false;
            break;
        }
    }

    /* Check total size */
    if (total_size != (heap_end - heap_start)) {
        kprintf("[HEAP] Validation failed: Size mismatch (blocks: %llu, heap: %llu)\n",
                (uint64_t)total_size, (uint64_t)(heap_end - heap_start));
        valid = false;
    }

    heap_release_lock();

    if (valid) {
        kprintf("[HEAP] Validation passed: %llu blocks, %llu bytes\n",
                (uint64_t)block_count, (uint64_t)total_size);
    }

    return valid;
}
