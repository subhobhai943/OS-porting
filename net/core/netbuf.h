/**
 * AAAos Network Stack - Network Buffer Management
 *
 * Provides a unified buffer structure for network packet handling.
 * Supports headroom/tailroom for protocol headers without copying.
 */

#ifndef _AAAOS_NET_NETBUF_H
#define _AAAOS_NET_NETBUF_H

#include "../../kernel/include/types.h"

/* Default buffer sizes */
#define NETBUF_DEFAULT_SIZE     2048
#define NETBUF_DEFAULT_HEADROOM 64      /* Space for headers (ETH + IP + etc) */
#define NETBUF_MAX_SIZE         65536

/**
 * Network buffer structure
 *
 * Layout:
 *   +-------------+------------------+------------------+--------------+
 *   | headroom    | data (len bytes) | tailroom         |              |
 *   +-------------+------------------+------------------+--------------+
 *   ^             ^                  ^                  ^
 *   buffer_start  data               data+len           buffer_start+capacity
 */
typedef struct netbuf {
    uint8_t *buffer_start;      /* Start of allocated buffer */
    uint8_t *data;              /* Current data pointer */
    size_t  len;                /* Current data length */
    size_t  capacity;           /* Total buffer capacity */

    /* Metadata */
    uint16_t protocol;          /* Protocol identifier (e.g., ETH_TYPE_IP) */
    uint32_t flags;             /* Buffer flags */

    /* Network layer info (set during processing) */
    uint8_t  src_mac[6];        /* Source MAC address */
    uint8_t  dst_mac[6];        /* Destination MAC address */
    uint32_t src_ip;            /* Source IP address */
    uint32_t dst_ip;            /* Destination IP address */

    /* Linked list for buffer chains */
    struct netbuf *next;
} netbuf_t;

/* Buffer flags */
#define NETBUF_FLAG_BROADCAST   BIT(0)  /* Broadcast packet */
#define NETBUF_FLAG_MULTICAST   BIT(1)  /* Multicast packet */
#define NETBUF_FLAG_LOOPBACK    BIT(2)  /* Loopback packet */
#define NETBUF_FLAG_TX          BIT(3)  /* Transmit packet */
#define NETBUF_FLAG_RX          BIT(4)  /* Receive packet */

/**
 * Allocate a new network buffer
 * @param size Total buffer size (data + headroom)
 * @param headroom Bytes to reserve at start for headers
 * @return Allocated netbuf or NULL on failure
 */
netbuf_t *netbuf_alloc(size_t size, size_t headroom);

/**
 * Allocate a network buffer with default settings
 * @return Allocated netbuf or NULL on failure
 */
static inline netbuf_t *netbuf_alloc_default(void) {
    return netbuf_alloc(NETBUF_DEFAULT_SIZE, NETBUF_DEFAULT_HEADROOM);
}

/**
 * Free a network buffer
 * @param buf Buffer to free
 */
void netbuf_free(netbuf_t *buf);

/**
 * Push data at the front (reduce headroom, for adding headers)
 * @param buf Buffer to modify
 * @param len Number of bytes to push
 * @return Pointer to new data start, or NULL if insufficient headroom
 */
void *netbuf_push(netbuf_t *buf, size_t len);

/**
 * Pull data from the front (increase headroom, for stripping headers)
 * @param buf Buffer to modify
 * @param len Number of bytes to pull
 * @return Pointer to new data start, or NULL if insufficient data
 */
void *netbuf_pull(netbuf_t *buf, size_t len);

/**
 * Add data at the tail (reduce tailroom)
 * @param buf Buffer to modify
 * @param len Number of bytes to add
 * @return Pointer to the added space, or NULL if insufficient tailroom
 */
void *netbuf_put(netbuf_t *buf, size_t len);

/**
 * Trim data from the tail
 * @param buf Buffer to modify
 * @param len Number of bytes to trim
 * @return 0 on success, -1 if insufficient data
 */
int netbuf_trim(netbuf_t *buf, size_t len);

/**
 * Get available headroom
 * @param buf Buffer to query
 * @return Bytes of headroom available
 */
static inline size_t netbuf_headroom(const netbuf_t *buf) {
    return (size_t)(buf->data - buf->buffer_start);
}

/**
 * Get available tailroom
 * @param buf Buffer to query
 * @return Bytes of tailroom available
 */
static inline size_t netbuf_tailroom(const netbuf_t *buf) {
    return buf->capacity - netbuf_headroom(buf) - buf->len;
}

/**
 * Get pointer to data at offset
 * @param buf Buffer to read from
 * @param offset Offset from data start
 * @return Pointer to data, or NULL if offset exceeds length
 */
static inline void *netbuf_data_at(const netbuf_t *buf, size_t offset) {
    if (offset >= buf->len) return NULL;
    return buf->data + offset;
}

/**
 * Clone a network buffer (deep copy)
 * @param buf Buffer to clone
 * @return New buffer with copied data, or NULL on failure
 */
netbuf_t *netbuf_clone(const netbuf_t *buf);

/**
 * Reset buffer to initial state (preserves capacity)
 * @param buf Buffer to reset
 * @param headroom New headroom value
 */
void netbuf_reset(netbuf_t *buf, size_t headroom);

/**
 * Copy data into buffer at current position
 * @param buf Destination buffer
 * @param data Source data
 * @param len Bytes to copy
 * @return 0 on success, -1 if insufficient space
 */
int netbuf_copy_in(netbuf_t *buf, const void *data, size_t len);

#endif /* _AAAOS_NET_NETBUF_H */
