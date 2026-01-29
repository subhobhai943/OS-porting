/**
 * AAAos Kernel - Message Passing IPC
 *
 * Provides message-based inter-process communication.
 * Each process has a message queue for receiving messages.
 */

#ifndef _AAAOS_IPC_MESSAGE_H
#define _AAAOS_IPC_MESSAGE_H

#include "../include/types.h"

/* Message configuration */
#define MSG_MAX_SIZE            256     /* Maximum message payload size */
#define MSG_QUEUE_SIZE          32      /* Messages per process queue */
#define MSG_POOL_SIZE           512     /* Total message pool size */

/* Message flags */
#define MSG_FLAG_URGENT         BIT(0)  /* High priority message */
#define MSG_FLAG_REPLY_EXPECTED BIT(1)  /* Sender expects reply */
#define MSG_FLAG_BROADCAST      BIT(2)  /* Broadcast message */

/* Message error codes */
#define MSG_SUCCESS             0
#define MSG_ERR_NO_MEMORY       (-1)    /* Out of memory/message slots */
#define MSG_ERR_INVALID_PID     (-2)    /* Invalid destination PID */
#define MSG_ERR_QUEUE_FULL      (-3)    /* Destination queue is full */
#define MSG_ERR_QUEUE_EMPTY     (-4)    /* No messages available */
#define MSG_ERR_TOO_LARGE       (-5)    /* Message exceeds maximum size */
#define MSG_ERR_INVALID_ARG     (-6)    /* Invalid argument */
#define MSG_ERR_NO_PROCESS      (-7)    /* No current process */
#define MSG_ERR_WOULDBLOCK      (-8)    /* Would block (for non-blocking calls) */

/**
 * Message header structure
 */
typedef struct message {
    uint32_t src_pid;                   /* Source process ID */
    uint32_t dest_pid;                  /* Destination process ID */
    uint32_t flags;                     /* Message flags */
    uint32_t msg_id;                    /* Message ID (for tracking) */
    size_t length;                      /* Payload length */
    uint64_t timestamp;                 /* When message was sent */
    uint8_t data[MSG_MAX_SIZE];         /* Message payload */
    struct message *next;               /* Next message in queue */
} message_t;

/**
 * Message queue structure
 * One per process
 */
typedef struct msg_queue {
    message_t *head;                    /* First message in queue */
    message_t *tail;                    /* Last message in queue */
    uint32_t count;                     /* Number of messages */
    uint32_t owner_pid;                 /* Process that owns this queue */
    struct process *waiters[8];         /* Processes waiting for messages */
    uint32_t waiter_count;              /* Number of waiting processes */
    volatile int lock;                  /* Spinlock for queue access */
} msg_queue_t;

/**
 * Initialize the message passing subsystem
 */
void msg_init(void);

/**
 * Send a message to a process
 * @param dest_pid Destination process ID
 * @param msg Pointer to message data
 * @param len Length of message in bytes
 * @return MSG_SUCCESS on success, negative error code on failure
 */
int msg_send(uint32_t dest_pid, const void *msg, size_t len);

/**
 * Send a message with flags
 * @param dest_pid Destination process ID
 * @param msg Pointer to message data
 * @param len Length of message in bytes
 * @param flags Message flags (MSG_FLAG_*)
 * @return MSG_SUCCESS on success, negative error code on failure
 */
int msg_send_flags(uint32_t dest_pid, const void *msg, size_t len, uint32_t flags);

/**
 * Receive a message (blocking)
 * @param buf Buffer to receive message into
 * @param max_len Maximum buffer length
 * @return Number of bytes received, or negative error code
 */
ssize_t msg_receive(void *buf, size_t max_len);

/**
 * Receive a message with source info (blocking)
 * @param buf Buffer to receive message into
 * @param max_len Maximum buffer length
 * @param src_pid Pointer to store source PID (can be NULL)
 * @return Number of bytes received, or negative error code
 */
ssize_t msg_receive_from(void *buf, size_t max_len, uint32_t *src_pid);

/**
 * Try to receive a message (non-blocking)
 * @param buf Buffer to receive message into
 * @param max_len Maximum buffer length
 * @return Number of bytes received, or negative error code
 */
ssize_t msg_try_receive(void *buf, size_t max_len);

/**
 * Try to receive a message with source info (non-blocking)
 * @param buf Buffer to receive message into
 * @param max_len Maximum buffer length
 * @param src_pid Pointer to store source PID (can be NULL)
 * @return Number of bytes received, or negative error code
 */
ssize_t msg_try_receive_from(void *buf, size_t max_len, uint32_t *src_pid);

/**
 * Check if there are pending messages
 * @return Number of pending messages, or 0 if none
 */
uint32_t msg_pending(void);

/**
 * Peek at the next message without removing it
 * @param buf Buffer to copy message into
 * @param max_len Maximum buffer length
 * @return Number of bytes copied, or negative error code
 */
ssize_t msg_peek(void *buf, size_t max_len);

/**
 * Get the message queue for a process
 * @param pid Process ID
 * @return Pointer to message queue, or NULL if invalid
 */
msg_queue_t* msg_get_queue(uint32_t pid);

/**
 * Reply to a message
 * @param original_src_pid PID of original message sender
 * @param reply Pointer to reply data
 * @param len Length of reply
 * @return MSG_SUCCESS on success, negative error code on failure
 */
int msg_reply(uint32_t original_src_pid, const void *reply, size_t len);

/**
 * Broadcast a message to all processes
 * @param msg Pointer to message data
 * @param len Length of message
 * @return Number of processes that received the message
 */
int msg_broadcast(const void *msg, size_t len);

/**
 * Dump message queue statistics (for debugging)
 */
void msg_dump_stats(void);

#endif /* _AAAOS_IPC_MESSAGE_H */
