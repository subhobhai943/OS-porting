/**
 * AAAos Kernel - Unix-style Pipes
 *
 * Provides unidirectional byte streams for inter-process communication.
 * Pipes use a circular buffer internally and support blocking I/O.
 */

#ifndef _AAAOS_IPC_PIPE_H
#define _AAAOS_IPC_PIPE_H

#include "../include/types.h"

/* Pipe configuration */
#define PIPE_BUFFER_SIZE        4096    /* 4KB circular buffer */
#define PIPE_MAX_COUNT          128     /* Maximum number of pipes */

/* Pipe flags */
#define PIPE_FLAG_READ_OPEN     BIT(0)  /* Read end is open */
#define PIPE_FLAG_WRITE_OPEN    BIT(1)  /* Write end is open */
#define PIPE_FLAG_NONBLOCK      BIT(2)  /* Non-blocking mode */

/* Pipe error codes */
#define PIPE_SUCCESS            0
#define PIPE_ERR_NO_MEMORY      (-1)    /* Out of memory */
#define PIPE_ERR_INVALID        (-2)    /* Invalid pipe or argument */
#define PIPE_ERR_CLOSED         (-3)    /* Pipe end is closed */
#define PIPE_ERR_FULL           (-4)    /* Pipe buffer is full (non-blocking) */
#define PIPE_ERR_EMPTY          (-5)    /* Pipe buffer is empty (non-blocking) */
#define PIPE_ERR_WOULDBLOCK     (-6)    /* Operation would block */
#define PIPE_ERR_MAX_PIPES      (-7)    /* Maximum pipes reached */

/**
 * Pipe structure
 * Represents a unidirectional byte stream
 */
typedef struct pipe {
    /* Circular buffer */
    uint8_t buffer[PIPE_BUFFER_SIZE];
    size_t read_pos;                    /* Read position in buffer */
    size_t write_pos;                   /* Write position in buffer */
    size_t count;                       /* Number of bytes in buffer */

    /* State */
    uint32_t flags;                     /* Pipe flags */
    uint32_t id;                        /* Pipe identifier */

    /* Reference counts */
    uint32_t readers;                   /* Number of read references */
    uint32_t writers;                   /* Number of write references */

    /* Process waiting */
    struct process *read_waiters[8];    /* Processes waiting to read */
    uint32_t read_waiter_count;
    struct process *write_waiters[8];   /* Processes waiting to write */
    uint32_t write_waiter_count;

    /* Synchronization */
    volatile int lock;                  /* Spinlock for pipe access */
} pipe_t;

/**
 * Initialize the pipe subsystem
 */
void pipe_init(void);

/**
 * Create a new pipe
 * @param fds Array of two integers: fds[0] for read, fds[1] for write
 * @return PIPE_SUCCESS on success, negative error code on failure
 */
int pipe_create(int fds[2]);

/**
 * Get pipe structure by file descriptor
 * @param fd File descriptor
 * @return Pointer to pipe, or NULL if invalid
 */
pipe_t* pipe_get(int fd);

/**
 * Read from a pipe (blocking)
 * @param pipe Pointer to pipe structure
 * @param buf Buffer to read into
 * @param count Maximum number of bytes to read
 * @return Number of bytes read, or negative error code
 */
ssize_t pipe_read(pipe_t *pipe, void *buf, size_t count);

/**
 * Write to a pipe
 * @param pipe Pointer to pipe structure
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
ssize_t pipe_write(pipe_t *pipe, const void *buf, size_t count);

/**
 * Close a pipe end
 * @param pipe Pointer to pipe structure
 * @param is_read_end true to close read end, false for write end
 * @return PIPE_SUCCESS on success, negative error code on failure
 */
int pipe_close(pipe_t *pipe);

/**
 * Close pipe by file descriptor
 * @param fd File descriptor to close
 * @return PIPE_SUCCESS on success, negative error code on failure
 */
int pipe_close_fd(int fd);

/**
 * Try to read from a pipe (non-blocking)
 * @param pipe Pointer to pipe structure
 * @param buf Buffer to read into
 * @param count Maximum number of bytes to read
 * @return Number of bytes read, or negative error code
 */
ssize_t pipe_try_read(pipe_t *pipe, void *buf, size_t count);

/**
 * Try to write to a pipe (non-blocking)
 * @param pipe Pointer to pipe structure
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
ssize_t pipe_try_write(pipe_t *pipe, const void *buf, size_t count);

/**
 * Get number of bytes available to read
 * @param pipe Pointer to pipe structure
 * @return Number of bytes available
 */
size_t pipe_available(pipe_t *pipe);

/**
 * Get free space in pipe buffer
 * @param pipe Pointer to pipe structure
 * @return Number of bytes of free space
 */
size_t pipe_free_space(pipe_t *pipe);

/**
 * Check if pipe is empty
 * @param pipe Pointer to pipe structure
 * @return true if empty
 */
static inline bool pipe_is_empty(pipe_t *pipe) {
    return pipe ? pipe->count == 0 : true;
}

/**
 * Check if pipe is full
 * @param pipe Pointer to pipe structure
 * @return true if full
 */
static inline bool pipe_is_full(pipe_t *pipe) {
    return pipe ? pipe->count == PIPE_BUFFER_SIZE : true;
}

/**
 * Dump pipe statistics (for debugging)
 */
void pipe_dump_stats(void);

#endif /* _AAAOS_IPC_PIPE_H */
