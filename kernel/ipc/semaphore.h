/**
 * AAAos Kernel - Semaphores
 *
 * Provides counting semaphores for process synchronization.
 * Supports blocking and non-blocking operations.
 */

#ifndef _AAAOS_IPC_SEMAPHORE_H
#define _AAAOS_IPC_SEMAPHORE_H

#include "../include/types.h"

/* Semaphore configuration */
#define SEM_MAX_COUNT           128     /* Maximum number of semaphores */
#define SEM_MAX_WAITERS         32      /* Maximum waiters per semaphore */
#define SEM_VALUE_MAX           INT32_MAX /* Maximum semaphore value */

/* Semaphore flags */
#define SEM_FLAG_VALID          BIT(0)  /* Semaphore is valid/allocated */
#define SEM_FLAG_BINARY         BIT(1)  /* Binary semaphore (mutex-like) */

/* Semaphore error codes */
#define SEM_SUCCESS             0
#define SEM_ERR_INVALID         (-1)    /* Invalid semaphore */
#define SEM_ERR_NO_MEMORY       (-2)    /* Out of memory/semaphore slots */
#define SEM_ERR_WOULDBLOCK      (-3)    /* Would block (for try operations) */
#define SEM_ERR_OVERFLOW        (-4)    /* Value would overflow */
#define SEM_ERR_DESTROYED       (-5)    /* Semaphore was destroyed while waiting */
#define SEM_ERR_TIMEOUT         (-6)    /* Operation timed out */
#define SEM_ERR_MAX_SEMS        (-7)    /* Maximum semaphores reached */

/**
 * Semaphore structure
 */
typedef struct semaphore {
    int32_t value;                      /* Current value (can be negative) */
    uint32_t id;                        /* Semaphore identifier */
    uint32_t flags;                     /* Semaphore flags */
    uint32_t owner_pid;                 /* Process that created the semaphore */

    /* Wait queue */
    struct process *waiters[SEM_MAX_WAITERS]; /* Processes waiting on semaphore */
    uint32_t waiter_count;              /* Number of waiting processes */
    uint32_t waiter_head;               /* Head of waiter queue (FIFO) */
    uint32_t waiter_tail;               /* Tail of waiter queue */

    /* Statistics */
    uint64_t wait_count;                /* Total number of wait operations */
    uint64_t post_count;                /* Total number of post operations */

    /* Synchronization */
    volatile int lock;                  /* Spinlock for semaphore access */
} semaphore_t;

/**
 * Initialize the semaphore subsystem
 */
void sem_init_subsystem(void);

/**
 * Create a new semaphore
 * @param initial_value Initial semaphore value
 * @return Pointer to semaphore, or NULL on failure
 */
semaphore_t* sem_create(int initial_value);

/**
 * Create a binary semaphore (mutex-like)
 * @param initial_value Initial value (0 or 1)
 * @return Pointer to semaphore, or NULL on failure
 */
semaphore_t* sem_create_binary(int initial_value);

/**
 * Wait on a semaphore (blocking, decrement)
 * Blocks if value is 0 or negative until it becomes positive
 * @param sem Pointer to semaphore
 * @return SEM_SUCCESS on success, negative error code on failure
 */
int sem_wait(semaphore_t *sem);

/**
 * Post to a semaphore (increment)
 * Wakes one waiting process if any
 * @param sem Pointer to semaphore
 * @return SEM_SUCCESS on success, negative error code on failure
 */
int sem_post(semaphore_t *sem);

/**
 * Try to wait on a semaphore (non-blocking)
 * @param sem Pointer to semaphore
 * @return SEM_SUCCESS if decremented, SEM_ERR_WOULDBLOCK if would block
 */
int sem_try_wait(semaphore_t *sem);

/**
 * Destroy a semaphore
 * Wakes all waiting processes with error
 * @param sem Pointer to semaphore
 * @return SEM_SUCCESS on success, negative error code on failure
 */
int sem_destroy(semaphore_t *sem);

/**
 * Get the current value of a semaphore
 * @param sem Pointer to semaphore
 * @return Current value, or SEM_ERR_INVALID if invalid
 */
int sem_get_value(semaphore_t *sem);

/**
 * Get a semaphore by ID
 * @param id Semaphore identifier
 * @return Pointer to semaphore, or NULL if not found
 */
semaphore_t* sem_get_by_id(uint32_t id);

/**
 * Check if semaphore is valid
 * @param sem Pointer to semaphore
 * @return true if valid
 */
static inline bool sem_is_valid(semaphore_t *sem) {
    return sem && (sem->flags & SEM_FLAG_VALID);
}

/**
 * Get number of processes waiting on semaphore
 * @param sem Pointer to semaphore
 * @return Number of waiters
 */
static inline uint32_t sem_waiter_count(semaphore_t *sem) {
    return sem ? sem->waiter_count : 0;
}

/**
 * Dump semaphore statistics (for debugging)
 */
void sem_dump_stats(void);

/**
 * Dump info for a specific semaphore
 * @param sem Pointer to semaphore
 */
void sem_dump_info(semaphore_t *sem);

#endif /* _AAAOS_IPC_SEMAPHORE_H */
