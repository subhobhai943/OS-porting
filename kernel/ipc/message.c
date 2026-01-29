/**
 * AAAos Kernel - Message Passing Implementation
 *
 * Implements message-based IPC with per-process message queues.
 * Supports blocking and non-blocking receive operations.
 */

#include "message.h"
#include "../include/serial.h"
#include "../proc/process.h"

/* Message pool - statically allocated */
static message_t message_pool[MSG_POOL_SIZE];

/* Free message list */
static message_t *free_messages = NULL;

/* Per-process message queues */
/* Index by PID (simple approach for now) */
static msg_queue_t msg_queues[PROCESS_MAX_COUNT];

/* Global message subsystem lock */
static volatile int msg_subsystem_lock = 0;

/* Next message ID */
static uint32_t next_msg_id = 1;

/* Simple tick counter for timestamps (should come from timer in real implementation) */
static uint64_t msg_tick_counter = 0;

/* Statistics */
static uint64_t total_messages_sent = 0;
static uint64_t total_messages_received = 0;
static uint64_t total_bytes_sent = 0;

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
 * Simple memset implementation
 */
static void *kmemset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

/**
 * Allocate a message from the pool
 */
static message_t* msg_alloc(void) {
    spinlock_acquire(&msg_subsystem_lock);

    message_t *msg = free_messages;
    if (msg) {
        free_messages = msg->next;
        msg->next = NULL;
    }

    spinlock_release(&msg_subsystem_lock);

    return msg;
}

/**
 * Return a message to the pool
 */
static void msg_free(message_t *msg) {
    if (!msg) return;

    spinlock_acquire(&msg_subsystem_lock);

    /* Clear the message */
    kmemset(msg, 0, sizeof(message_t));

    /* Add to free list */
    msg->next = free_messages;
    free_messages = msg;

    spinlock_release(&msg_subsystem_lock);
}

/**
 * Block current process waiting for message
 */
static void msg_block_receiver(msg_queue_t *queue) {
    process_t *current = process_get_current();
    if (!current) return;

    if (queue->waiter_count < 8) {
        queue->waiters[queue->waiter_count++] = current;
        process_set_state(current, PROCESS_STATE_BLOCKED);
        kprintf("[MSG] Process '%s' (PID %u) blocked waiting for message\n",
                current->name, current->pid);
    }
}

/**
 * Wake one process waiting for messages
 */
static void msg_wake_receiver(msg_queue_t *queue) {
    if (queue->waiter_count > 0) {
        process_t *proc = queue->waiters[0];
        /* Shift remaining waiters */
        for (uint32_t i = 0; i < queue->waiter_count - 1; i++) {
            queue->waiters[i] = queue->waiters[i + 1];
        }
        queue->waiter_count--;
        queue->waiters[queue->waiter_count] = NULL;

        if (proc) {
            process_set_state(proc, PROCESS_STATE_READY);
            kprintf("[MSG] Woke process '%s' (PID %u) - message available\n",
                    proc->name, proc->pid);
        }
    }
}

/**
 * Initialize the message passing subsystem
 */
void msg_init(void) {
    kprintf("[MSG] Initializing Message Passing Subsystem...\n");

    spinlock_acquire(&msg_subsystem_lock);

    /* Initialize message pool as free list */
    free_messages = NULL;
    for (int i = MSG_POOL_SIZE - 1; i >= 0; i--) {
        kmemset(&message_pool[i], 0, sizeof(message_t));
        message_pool[i].next = free_messages;
        free_messages = &message_pool[i];
    }

    kprintf("[MSG] Message pool initialized (%u messages, %u bytes each)\n",
            MSG_POOL_SIZE, (uint32_t)sizeof(message_t));

    /* Initialize per-process queues */
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        msg_queues[i].head = NULL;
        msg_queues[i].tail = NULL;
        msg_queues[i].count = 0;
        msg_queues[i].owner_pid = i;  /* Queue index == PID */
        msg_queues[i].waiter_count = 0;
        msg_queues[i].lock = 0;
        for (int j = 0; j < 8; j++) {
            msg_queues[i].waiters[j] = NULL;
        }
    }

    kprintf("[MSG] Message queues initialized (%u queues)\n", PROCESS_MAX_COUNT);

    spinlock_release(&msg_subsystem_lock);

    kprintf("[MSG] Message Passing Subsystem initialized successfully\n");
}

/**
 * Get message queue for a process
 */
msg_queue_t* msg_get_queue(uint32_t pid) {
    if (pid >= PROCESS_MAX_COUNT) return NULL;
    return &msg_queues[pid];
}

/**
 * Send a message to a process
 */
int msg_send(uint32_t dest_pid, const void *msg, size_t len) {
    return msg_send_flags(dest_pid, msg, len, 0);
}

/**
 * Send a message with flags
 */
int msg_send_flags(uint32_t dest_pid, const void *msg, size_t len, uint32_t flags) {
    if (!msg) {
        kprintf("[MSG] Error: msg_send called with NULL message\n");
        return MSG_ERR_INVALID_ARG;
    }

    if (len > MSG_MAX_SIZE) {
        kprintf("[MSG] Error: Message too large (%llu > %u)\n",
                (uint64_t)len, MSG_MAX_SIZE);
        return MSG_ERR_TOO_LARGE;
    }

    /* Validate destination process exists */
    process_t *dest_proc = process_get_by_pid(dest_pid);
    if (!dest_proc) {
        kprintf("[MSG] Error: Invalid destination PID %u\n", dest_pid);
        return MSG_ERR_INVALID_PID;
    }

    /* Get source PID */
    process_t *current = process_get_current();
    uint32_t src_pid = current ? current->pid : 0;

    /* Get destination queue */
    msg_queue_t *queue = msg_get_queue(dest_pid);
    if (!queue) {
        kprintf("[MSG] Error: No queue for PID %u\n", dest_pid);
        return MSG_ERR_INVALID_PID;
    }

    spinlock_acquire(&queue->lock);

    /* Check if queue is full */
    if (queue->count >= MSG_QUEUE_SIZE) {
        spinlock_release(&queue->lock);
        kprintf("[MSG] Error: Queue full for PID %u (%u messages)\n",
                dest_pid, queue->count);
        return MSG_ERR_QUEUE_FULL;
    }

    /* Allocate a message */
    message_t *new_msg = msg_alloc();
    if (!new_msg) {
        spinlock_release(&queue->lock);
        kprintf("[MSG] Error: No free message slots\n");
        return MSG_ERR_NO_MEMORY;
    }

    /* Fill in the message */
    new_msg->src_pid = src_pid;
    new_msg->dest_pid = dest_pid;
    new_msg->flags = flags;
    new_msg->msg_id = next_msg_id++;
    new_msg->length = len;
    new_msg->timestamp = msg_tick_counter++;
    new_msg->next = NULL;

    /* Copy payload */
    kmemcpy(new_msg->data, msg, len);

    /* Add to queue */
    if (queue->tail) {
        queue->tail->next = new_msg;
    } else {
        queue->head = new_msg;
    }
    queue->tail = new_msg;
    queue->count++;

    total_messages_sent++;
    total_bytes_sent += len;

    /* Wake a waiting receiver */
    msg_wake_receiver(queue);

    spinlock_release(&queue->lock);

    kprintf("[MSG] Sent message %u from PID %u to PID %u (%llu bytes)\n",
            new_msg->msg_id, src_pid, dest_pid, (uint64_t)len);

    return MSG_SUCCESS;
}

/**
 * Internal receive implementation
 */
static ssize_t msg_receive_internal(void *buf, size_t max_len, uint32_t *src_pid, bool blocking) {
    if (!buf) {
        kprintf("[MSG] Error: msg_receive called with NULL buffer\n");
        return MSG_ERR_INVALID_ARG;
    }

    process_t *current = process_get_current();
    if (!current) {
        kprintf("[MSG] Error: No current process for receive\n");
        return MSG_ERR_NO_PROCESS;
    }

    msg_queue_t *queue = msg_get_queue(current->pid);
    if (!queue) {
        kprintf("[MSG] Error: No queue for current process\n");
        return MSG_ERR_NO_PROCESS;
    }

    spinlock_acquire(&queue->lock);

    /* Wait for message if blocking */
    while (queue->count == 0) {
        if (!blocking) {
            spinlock_release(&queue->lock);
            return MSG_ERR_WOULDBLOCK;
        }

        /* Block the current process */
        msg_block_receiver(queue);
        spinlock_release(&queue->lock);

        /* Yield to scheduler */
        __asm__ __volatile__("pause");

        spinlock_acquire(&queue->lock);
    }

    /* Dequeue the message */
    message_t *msg = queue->head;
    queue->head = msg->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->count--;

    /* Copy data to buffer */
    size_t copy_len = MIN(msg->length, max_len);
    kmemcpy(buf, msg->data, copy_len);

    /* Return source PID if requested */
    if (src_pid) {
        *src_pid = msg->src_pid;
    }

    uint32_t msg_id = msg->msg_id;
    uint32_t sender = msg->src_pid;

    /* Free the message */
    msg_free(msg);

    total_messages_received++;

    spinlock_release(&queue->lock);

    kprintf("[MSG] Received message %u from PID %u (%llu bytes)\n",
            msg_id, sender, (uint64_t)copy_len);

    return (ssize_t)copy_len;
}

/**
 * Receive a message (blocking)
 */
ssize_t msg_receive(void *buf, size_t max_len) {
    return msg_receive_internal(buf, max_len, NULL, true);
}

/**
 * Receive with source info (blocking)
 */
ssize_t msg_receive_from(void *buf, size_t max_len, uint32_t *src_pid) {
    return msg_receive_internal(buf, max_len, src_pid, true);
}

/**
 * Try to receive (non-blocking)
 */
ssize_t msg_try_receive(void *buf, size_t max_len) {
    return msg_receive_internal(buf, max_len, NULL, false);
}

/**
 * Try to receive with source info (non-blocking)
 */
ssize_t msg_try_receive_from(void *buf, size_t max_len, uint32_t *src_pid) {
    return msg_receive_internal(buf, max_len, src_pid, false);
}

/**
 * Check if there are pending messages
 */
uint32_t msg_pending(void) {
    process_t *current = process_get_current();
    if (!current) return 0;

    msg_queue_t *queue = msg_get_queue(current->pid);
    if (!queue) return 0;

    spinlock_acquire(&queue->lock);
    uint32_t count = queue->count;
    spinlock_release(&queue->lock);

    return count;
}

/**
 * Peek at next message
 */
ssize_t msg_peek(void *buf, size_t max_len) {
    if (!buf) return MSG_ERR_INVALID_ARG;

    process_t *current = process_get_current();
    if (!current) return MSG_ERR_NO_PROCESS;

    msg_queue_t *queue = msg_get_queue(current->pid);
    if (!queue) return MSG_ERR_NO_PROCESS;

    spinlock_acquire(&queue->lock);

    if (queue->count == 0) {
        spinlock_release(&queue->lock);
        return MSG_ERR_QUEUE_EMPTY;
    }

    message_t *msg = queue->head;
    size_t copy_len = MIN(msg->length, max_len);
    kmemcpy(buf, msg->data, copy_len);

    spinlock_release(&queue->lock);

    return (ssize_t)copy_len;
}

/**
 * Reply to a message
 */
int msg_reply(uint32_t original_src_pid, const void *reply, size_t len) {
    return msg_send(original_src_pid, reply, len);
}

/**
 * Broadcast a message to all processes
 */
int msg_broadcast(const void *msg, size_t len) {
    if (!msg || len > MSG_MAX_SIZE) {
        return MSG_ERR_INVALID_ARG;
    }

    process_t *current = process_get_current();
    uint32_t src_pid = current ? current->pid : 0;

    int recipients = 0;

    kprintf("[MSG] Broadcasting message (%llu bytes) from PID %u\n",
            (uint64_t)len, src_pid);

    for (uint32_t pid = 1; pid < PROCESS_MAX_COUNT; pid++) {
        if (pid == src_pid) continue;  /* Don't send to self */

        process_t *proc = process_get_by_pid(pid);
        if (proc && proc->state != PROCESS_STATE_INVALID &&
            proc->state != PROCESS_STATE_TERMINATED) {
            int result = msg_send_flags(pid, msg, len, MSG_FLAG_BROADCAST);
            if (result == MSG_SUCCESS) {
                recipients++;
            }
        }
    }

    kprintf("[MSG] Broadcast complete: %d recipients\n", recipients);

    return recipients;
}

/**
 * Dump message statistics
 */
void msg_dump_stats(void) {
    kprintf("[MSG] ========== Message Statistics ==========\n");
    kprintf("[MSG] Total messages sent:     %llu\n", total_messages_sent);
    kprintf("[MSG] Total messages received: %llu\n", total_messages_received);
    kprintf("[MSG] Total bytes sent:        %llu\n", total_bytes_sent);
    kprintf("[MSG] Message pool size:       %u\n", MSG_POOL_SIZE);
    kprintf("[MSG] Max message size:        %u bytes\n", MSG_MAX_SIZE);
    kprintf("[MSG] Queue size per process:  %u messages\n", MSG_QUEUE_SIZE);
    kprintf("[MSG] ----------------------------------\n");

    /* Count free messages */
    uint32_t free_count = 0;
    spinlock_acquire(&msg_subsystem_lock);
    for (message_t *m = free_messages; m; m = m->next) {
        free_count++;
    }
    spinlock_release(&msg_subsystem_lock);

    kprintf("[MSG] Free messages:           %u\n", free_count);
    kprintf("[MSG] Used messages:           %u\n", MSG_POOL_SIZE - free_count);

    /* Show non-empty queues */
    kprintf("[MSG] ----------------------------------\n");
    kprintf("[MSG] Non-empty queues:\n");

    bool found_any = false;
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (msg_queues[i].count > 0) {
            process_t *proc = process_get_by_pid(i);
            kprintf("[MSG]   PID %u (%s): %u messages, %u waiters\n",
                    i,
                    proc ? proc->name : "unknown",
                    msg_queues[i].count,
                    msg_queues[i].waiter_count);
            found_any = true;
        }
    }

    if (!found_any) {
        kprintf("[MSG]   (none)\n");
    }

    kprintf("[MSG] =====================================\n");
}
