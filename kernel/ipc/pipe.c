/**
 * AAAos Kernel - Unix-style Pipes Implementation
 *
 * Implements unidirectional byte streams using circular buffers.
 * Supports blocking I/O with process waiting.
 */

#include "pipe.h"
#include "../include/serial.h"
#include "../proc/process.h"

/* Pipe table - statically allocated */
static pipe_t pipe_table[PIPE_MAX_COUNT];

/* Next available pipe ID */
static uint32_t next_pipe_id = 1;

/* File descriptor to pipe mapping */
/* Even FDs are read ends, odd FDs are write ends */
/* fd / 2 = pipe index, fd % 2 = 0 for read, 1 for write */
#define FD_TO_PIPE_INDEX(fd)    ((fd) / 2)
#define FD_IS_READ_END(fd)      (((fd) % 2) == 0)
#define PIPE_TO_READ_FD(idx)    ((idx) * 2)
#define PIPE_TO_WRITE_FD(idx)   ((idx) * 2 + 1)

/* Global pipe subsystem lock */
static volatile int pipe_subsystem_lock = 0;

/* Statistics */
static uint64_t total_pipes_created = 0;
static uint64_t total_bytes_transferred = 0;

/**
 * Acquire a spinlock
 */
static inline void spinlock_acquire(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

/**
 * Release a spinlock
 */
static inline void spinlock_release(volatile int *lock) {
    __sync_lock_release(lock);
}

/**
 * Simple memcpy implementation
 */
static void *kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/**
 * Block current process waiting for pipe
 */
static void pipe_block_reader(pipe_t *pipe) {
    process_t *current = process_get_current();
    if (!current) return;

    if (pipe->read_waiter_count < 8) {
        pipe->read_waiters[pipe->read_waiter_count++] = current;
        process_set_state(current, PROCESS_STATE_BLOCKED);
        kprintf("[PIPE] Process '%s' (PID %u) blocked waiting to read from pipe %u\n",
                current->name, current->pid, pipe->id);
    }
}

/**
 * Block current process waiting to write
 */
static void pipe_block_writer(pipe_t *pipe) {
    process_t *current = process_get_current();
    if (!current) return;

    if (pipe->write_waiter_count < 8) {
        pipe->write_waiters[pipe->write_waiter_count++] = current;
        process_set_state(current, PROCESS_STATE_BLOCKED);
        kprintf("[PIPE] Process '%s' (PID %u) blocked waiting to write to pipe %u\n",
                current->name, current->pid, pipe->id);
    }
}

/**
 * Wake one reader waiting on pipe
 */
static void pipe_wake_reader(pipe_t *pipe) {
    if (pipe->read_waiter_count > 0) {
        process_t *proc = pipe->read_waiters[0];
        /* Shift remaining waiters */
        for (uint32_t i = 0; i < pipe->read_waiter_count - 1; i++) {
            pipe->read_waiters[i] = pipe->read_waiters[i + 1];
        }
        pipe->read_waiter_count--;
        pipe->read_waiters[pipe->read_waiter_count] = NULL;

        if (proc) {
            process_set_state(proc, PROCESS_STATE_READY);
            kprintf("[PIPE] Woke reader process '%s' (PID %u) for pipe %u\n",
                    proc->name, proc->pid, pipe->id);
        }
    }
}

/**
 * Wake one writer waiting on pipe
 */
static void pipe_wake_writer(pipe_t *pipe) {
    if (pipe->write_waiter_count > 0) {
        process_t *proc = pipe->write_waiters[0];
        /* Shift remaining waiters */
        for (uint32_t i = 0; i < pipe->write_waiter_count - 1; i++) {
            pipe->write_waiters[i] = pipe->write_waiters[i + 1];
        }
        pipe->write_waiter_count--;
        pipe->write_waiters[pipe->write_waiter_count] = NULL;

        if (proc) {
            process_set_state(proc, PROCESS_STATE_READY);
            kprintf("[PIPE] Woke writer process '%s' (PID %u) for pipe %u\n",
                    proc->name, proc->pid, pipe->id);
        }
    }
}

/**
 * Wake all readers (used when write end closes)
 */
static void pipe_wake_all_readers(pipe_t *pipe) {
    while (pipe->read_waiter_count > 0) {
        pipe_wake_reader(pipe);
    }
}

/**
 * Wake all writers (used when read end closes)
 */
static void pipe_wake_all_writers(pipe_t *pipe) {
    while (pipe->write_waiter_count > 0) {
        pipe_wake_writer(pipe);
    }
}

/**
 * Initialize the pipe subsystem
 */
void pipe_init(void) {
    kprintf("[PIPE] Initializing Pipe Subsystem...\n");

    spinlock_acquire(&pipe_subsystem_lock);

    /* Initialize all pipes as unused */
    for (uint32_t i = 0; i < PIPE_MAX_COUNT; i++) {
        pipe_table[i].flags = 0;
        pipe_table[i].id = 0;
        pipe_table[i].count = 0;
        pipe_table[i].read_pos = 0;
        pipe_table[i].write_pos = 0;
        pipe_table[i].readers = 0;
        pipe_table[i].writers = 0;
        pipe_table[i].read_waiter_count = 0;
        pipe_table[i].write_waiter_count = 0;
        pipe_table[i].lock = 0;
    }

    spinlock_release(&pipe_subsystem_lock);

    kprintf("[PIPE] Pipe table initialized (%u slots, %u bytes each)\n",
            PIPE_MAX_COUNT, (uint32_t)sizeof(pipe_t));
    kprintf("[PIPE] Pipe Subsystem initialized successfully\n");
}

/**
 * Find a free pipe slot
 */
static pipe_t* pipe_alloc(void) {
    for (uint32_t i = 0; i < PIPE_MAX_COUNT; i++) {
        if (pipe_table[i].flags == 0) {
            return &pipe_table[i];
        }
    }
    return NULL;
}

/**
 * Get pipe index from pointer
 */
static int pipe_get_index(pipe_t *pipe) {
    if (!pipe) return -1;
    ptrdiff_t idx = pipe - pipe_table;
    if (idx < 0 || idx >= PIPE_MAX_COUNT) return -1;
    return (int)idx;
}

/**
 * Create a new pipe
 */
int pipe_create(int fds[2]) {
    if (!fds) {
        kprintf("[PIPE] Error: pipe_create called with NULL fds\n");
        return PIPE_ERR_INVALID;
    }

    spinlock_acquire(&pipe_subsystem_lock);

    pipe_t *pipe = pipe_alloc();
    if (!pipe) {
        spinlock_release(&pipe_subsystem_lock);
        kprintf("[PIPE] Error: No free pipe slots (max %u)\n", PIPE_MAX_COUNT);
        return PIPE_ERR_MAX_PIPES;
    }

    int idx = pipe_get_index(pipe);

    /* Initialize the pipe */
    pipe->id = next_pipe_id++;
    pipe->flags = PIPE_FLAG_READ_OPEN | PIPE_FLAG_WRITE_OPEN;
    pipe->count = 0;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->read_waiter_count = 0;
    pipe->write_waiter_count = 0;
    pipe->lock = 0;

    /* Clear buffer */
    for (size_t i = 0; i < PIPE_BUFFER_SIZE; i++) {
        pipe->buffer[i] = 0;
    }

    /* Clear waiter arrays */
    for (int i = 0; i < 8; i++) {
        pipe->read_waiters[i] = NULL;
        pipe->write_waiters[i] = NULL;
    }

    /* Set file descriptors */
    fds[0] = PIPE_TO_READ_FD(idx);   /* Read end */
    fds[1] = PIPE_TO_WRITE_FD(idx);  /* Write end */

    total_pipes_created++;

    spinlock_release(&pipe_subsystem_lock);

    kprintf("[PIPE] Created pipe %u (fds: read=%d, write=%d)\n",
            pipe->id, fds[0], fds[1]);

    return PIPE_SUCCESS;
}

/**
 * Get pipe structure by file descriptor
 */
pipe_t* pipe_get(int fd) {
    if (fd < 0) return NULL;
    int idx = FD_TO_PIPE_INDEX(fd);
    if (idx >= PIPE_MAX_COUNT) return NULL;
    if (pipe_table[idx].flags == 0) return NULL;
    return &pipe_table[idx];
}

/**
 * Read from pipe (blocking)
 */
ssize_t pipe_read(pipe_t *pipe, void *buf, size_t count) {
    if (!pipe || !buf) {
        kprintf("[PIPE] Error: pipe_read called with NULL argument\n");
        return PIPE_ERR_INVALID;
    }

    if (count == 0) return 0;

    spinlock_acquire(&pipe->lock);

    /* Check if read end is open */
    if (!(pipe->flags & PIPE_FLAG_READ_OPEN)) {
        spinlock_release(&pipe->lock);
        kprintf("[PIPE] Error: Read end of pipe %u is closed\n", pipe->id);
        return PIPE_ERR_CLOSED;
    }

    /* Block while empty and write end is still open */
    while (pipe->count == 0) {
        if (!(pipe->flags & PIPE_FLAG_WRITE_OPEN)) {
            /* Write end closed, return EOF */
            spinlock_release(&pipe->lock);
            kprintf("[PIPE] EOF on pipe %u (write end closed)\n", pipe->id);
            return 0;
        }

        /* Block the current process */
        pipe_block_reader(pipe);
        spinlock_release(&pipe->lock);

        /* Yield to scheduler - in a real implementation this would
         * actually context switch. For now, we spin. */
        __asm__ __volatile__("pause");

        spinlock_acquire(&pipe->lock);
    }

    /* Read data from circular buffer */
    size_t to_read = MIN(count, pipe->count);
    size_t bytes_read = 0;
    uint8_t *dest = (uint8_t *)buf;

    while (bytes_read < to_read) {
        dest[bytes_read++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count -= bytes_read;
    total_bytes_transferred += bytes_read;

    /* Wake a writer if any are waiting */
    pipe_wake_writer(pipe);

    spinlock_release(&pipe->lock);

    kprintf("[PIPE] Read %llu bytes from pipe %u (%llu bytes remaining)\n",
            (uint64_t)bytes_read, pipe->id, (uint64_t)pipe->count);

    return (ssize_t)bytes_read;
}

/**
 * Write to pipe
 */
ssize_t pipe_write(pipe_t *pipe, const void *buf, size_t count) {
    if (!pipe || !buf) {
        kprintf("[PIPE] Error: pipe_write called with NULL argument\n");
        return PIPE_ERR_INVALID;
    }

    if (count == 0) return 0;

    spinlock_acquire(&pipe->lock);

    /* Check if write end is open */
    if (!(pipe->flags & PIPE_FLAG_WRITE_OPEN)) {
        spinlock_release(&pipe->lock);
        kprintf("[PIPE] Error: Write end of pipe %u is closed\n", pipe->id);
        return PIPE_ERR_CLOSED;
    }

    /* Check if read end is open (broken pipe) */
    if (!(pipe->flags & PIPE_FLAG_READ_OPEN)) {
        spinlock_release(&pipe->lock);
        kprintf("[PIPE] Error: Broken pipe %u (read end closed)\n", pipe->id);
        return PIPE_ERR_CLOSED;
    }

    const uint8_t *src = (const uint8_t *)buf;
    size_t bytes_written = 0;

    while (bytes_written < count) {
        /* Block while full */
        while (pipe->count == PIPE_BUFFER_SIZE) {
            if (!(pipe->flags & PIPE_FLAG_READ_OPEN)) {
                /* Read end closed while we were waiting */
                spinlock_release(&pipe->lock);
                if (bytes_written > 0) {
                    return (ssize_t)bytes_written;
                }
                return PIPE_ERR_CLOSED;
            }

            /* Block the current process */
            pipe_block_writer(pipe);
            spinlock_release(&pipe->lock);

            /* Yield to scheduler */
            __asm__ __volatile__("pause");

            spinlock_acquire(&pipe->lock);
        }

        /* Write as much as we can */
        size_t space = PIPE_BUFFER_SIZE - pipe->count;
        size_t to_write = MIN(count - bytes_written, space);

        for (size_t i = 0; i < to_write; i++) {
            pipe->buffer[pipe->write_pos] = src[bytes_written++];
            pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
        }

        pipe->count += to_write;

        /* Wake a reader if any are waiting */
        pipe_wake_reader(pipe);
    }

    total_bytes_transferred += bytes_written;

    spinlock_release(&pipe->lock);

    kprintf("[PIPE] Wrote %llu bytes to pipe %u (%llu bytes buffered)\n",
            (uint64_t)bytes_written, pipe->id, (uint64_t)pipe->count);

    return (ssize_t)bytes_written;
}

/**
 * Try to read (non-blocking)
 */
ssize_t pipe_try_read(pipe_t *pipe, void *buf, size_t count) {
    if (!pipe || !buf) {
        return PIPE_ERR_INVALID;
    }

    if (count == 0) return 0;

    spinlock_acquire(&pipe->lock);

    if (!(pipe->flags & PIPE_FLAG_READ_OPEN)) {
        spinlock_release(&pipe->lock);
        return PIPE_ERR_CLOSED;
    }

    if (pipe->count == 0) {
        spinlock_release(&pipe->lock);
        if (!(pipe->flags & PIPE_FLAG_WRITE_OPEN)) {
            return 0; /* EOF */
        }
        return PIPE_ERR_WOULDBLOCK;
    }

    /* Read available data */
    size_t to_read = MIN(count, pipe->count);
    uint8_t *dest = (uint8_t *)buf;

    for (size_t i = 0; i < to_read; i++) {
        dest[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count -= to_read;
    total_bytes_transferred += to_read;

    pipe_wake_writer(pipe);

    spinlock_release(&pipe->lock);

    return (ssize_t)to_read;
}

/**
 * Try to write (non-blocking)
 */
ssize_t pipe_try_write(pipe_t *pipe, const void *buf, size_t count) {
    if (!pipe || !buf) {
        return PIPE_ERR_INVALID;
    }

    if (count == 0) return 0;

    spinlock_acquire(&pipe->lock);

    if (!(pipe->flags & PIPE_FLAG_WRITE_OPEN)) {
        spinlock_release(&pipe->lock);
        return PIPE_ERR_CLOSED;
    }

    if (!(pipe->flags & PIPE_FLAG_READ_OPEN)) {
        spinlock_release(&pipe->lock);
        return PIPE_ERR_CLOSED;
    }

    if (pipe->count == PIPE_BUFFER_SIZE) {
        spinlock_release(&pipe->lock);
        return PIPE_ERR_WOULDBLOCK;
    }

    /* Write available space */
    size_t space = PIPE_BUFFER_SIZE - pipe->count;
    size_t to_write = MIN(count, space);
    const uint8_t *src = (const uint8_t *)buf;

    for (size_t i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = src[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
    }

    pipe->count += to_write;
    total_bytes_transferred += to_write;

    pipe_wake_reader(pipe);

    spinlock_release(&pipe->lock);

    return (ssize_t)to_write;
}

/**
 * Close a pipe end
 */
int pipe_close(pipe_t *pipe) {
    if (!pipe) {
        kprintf("[PIPE] Error: pipe_close called with NULL pipe\n");
        return PIPE_ERR_INVALID;
    }

    spinlock_acquire(&pipe->lock);

    /* This function closes both ends - for more granular control,
     * use the internal mechanism or pipe_close_fd */
    kprintf("[PIPE] Closing pipe %u\n", pipe->id);

    /* Wake all waiters */
    pipe_wake_all_readers(pipe);
    pipe_wake_all_writers(pipe);

    /* Mark as closed */
    pipe->flags = 0;
    pipe->readers = 0;
    pipe->writers = 0;

    spinlock_release(&pipe->lock);

    kprintf("[PIPE] Pipe %u closed\n", pipe->id);

    return PIPE_SUCCESS;
}

/**
 * Close pipe by file descriptor
 */
int pipe_close_fd(int fd) {
    pipe_t *pipe = pipe_get(fd);
    if (!pipe) {
        kprintf("[PIPE] Error: Invalid fd %d for pipe_close_fd\n", fd);
        return PIPE_ERR_INVALID;
    }

    spinlock_acquire(&pipe->lock);

    bool is_read = FD_IS_READ_END(fd);

    if (is_read) {
        if (pipe->readers > 0) pipe->readers--;
        if (pipe->readers == 0) {
            pipe->flags &= ~PIPE_FLAG_READ_OPEN;
            kprintf("[PIPE] Closed read end of pipe %u\n", pipe->id);
            /* Wake all writers - they'll get broken pipe error */
            pipe_wake_all_writers(pipe);
        }
    } else {
        if (pipe->writers > 0) pipe->writers--;
        if (pipe->writers == 0) {
            pipe->flags &= ~PIPE_FLAG_WRITE_OPEN;
            kprintf("[PIPE] Closed write end of pipe %u\n", pipe->id);
            /* Wake all readers - they'll get EOF */
            pipe_wake_all_readers(pipe);
        }
    }

    /* If both ends are closed, free the pipe */
    if (!(pipe->flags & (PIPE_FLAG_READ_OPEN | PIPE_FLAG_WRITE_OPEN))) {
        kprintf("[PIPE] Both ends closed, freeing pipe %u\n", pipe->id);
        pipe->flags = 0;
        pipe->id = 0;
    }

    spinlock_release(&pipe->lock);

    return PIPE_SUCCESS;
}

/**
 * Get number of bytes available to read
 */
size_t pipe_available(pipe_t *pipe) {
    if (!pipe) return 0;

    spinlock_acquire(&pipe->lock);
    size_t available = pipe->count;
    spinlock_release(&pipe->lock);

    return available;
}

/**
 * Get free space in pipe buffer
 */
size_t pipe_free_space(pipe_t *pipe) {
    if (!pipe) return 0;

    spinlock_acquire(&pipe->lock);
    size_t free_space = PIPE_BUFFER_SIZE - pipe->count;
    spinlock_release(&pipe->lock);

    return free_space;
}

/**
 * Dump pipe statistics
 */
void pipe_dump_stats(void) {
    kprintf("[PIPE] ========== Pipe Statistics ==========\n");
    kprintf("[PIPE] Total pipes created:    %llu\n", total_pipes_created);
    kprintf("[PIPE] Total bytes transferred: %llu\n", total_bytes_transferred);
    kprintf("[PIPE] Max pipe slots:         %u\n", PIPE_MAX_COUNT);
    kprintf("[PIPE] Buffer size per pipe:   %u bytes\n", PIPE_BUFFER_SIZE);
    kprintf("[PIPE] ----------------------------------\n");

    uint32_t active_count = 0;
    for (uint32_t i = 0; i < PIPE_MAX_COUNT; i++) {
        if (pipe_table[i].flags != 0) {
            active_count++;
            pipe_t *p = &pipe_table[i];
            kprintf("[PIPE] Pipe %u: %llu bytes buffered, R:%s W:%s, %u readers waiting, %u writers waiting\n",
                    p->id, (uint64_t)p->count,
                    (p->flags & PIPE_FLAG_READ_OPEN) ? "open" : "closed",
                    (p->flags & PIPE_FLAG_WRITE_OPEN) ? "open" : "closed",
                    p->read_waiter_count, p->write_waiter_count);
        }
    }

    kprintf("[PIPE] Active pipes:           %u\n", active_count);
    kprintf("[PIPE] =====================================\n");
}
