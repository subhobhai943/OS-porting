/**
 * AAAos Network Stack - UDP Protocol Implementation
 *
 * Implements the User Datagram Protocol (RFC 768).
 * Features:
 *   - Connectionless datagram transmission
 *   - Port binding and management
 *   - Non-blocking receive with queue
 *   - Integration with IP layer
 */

#ifndef _AAAOS_NET_UDP_H
#define _AAAOS_NET_UDP_H

#include "../../kernel/include/types.h"

/* UDP Protocol Constants */
#define UDP_PROTOCOL            17          /* IP protocol number for UDP */
#define UDP_HEADER_LEN          8           /* UDP header is always 8 bytes */
#define UDP_MAX_DATAGRAM        65535       /* Maximum UDP datagram size */
#define UDP_MAX_PAYLOAD         (UDP_MAX_DATAGRAM - UDP_HEADER_LEN)
#define UDP_MAX_SOCKETS         256         /* Maximum bound UDP sockets */
#define UDP_RECV_QUEUE_SIZE     16          /* Max queued datagrams per socket */

/* Port ranges */
#define UDP_PORT_EPHEMERAL_MIN  49152       /* Start of ephemeral port range */
#define UDP_PORT_EPHEMERAL_MAX  65535       /* End of ephemeral port range */
#define UDP_PORT_PRIVILEGED_MAX 1023        /* End of privileged ports */

/**
 * UDP Header Structure (8 bytes)
 *
 * Format:
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          Source Port          |       Destination Port        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |            Length             |           Checksum            |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
typedef struct PACKED udp_header {
    uint16_t src_port;          /* Source port number */
    uint16_t dst_port;          /* Destination port number */
    uint16_t length;            /* Total length (header + data) */
    uint16_t checksum;          /* Checksum (optional in IPv4, 0 = unused) */
} udp_header_t;

/**
 * UDP Pseudo-header for checksum calculation
 */
typedef struct PACKED udp_pseudo_header {
    uint32_t src_ip;            /* Source IP address */
    uint32_t dst_ip;            /* Destination IP address */
    uint8_t  zero;              /* Reserved (must be zero) */
    uint8_t  protocol;          /* Protocol (17 for UDP) */
    uint16_t udp_length;        /* UDP segment length (header + data) */
} udp_pseudo_header_t;

/**
 * Queued datagram structure for receive queue
 */
typedef struct udp_datagram {
    uint8_t  *data;             /* Datagram payload */
    size_t   len;               /* Payload length */
    uint32_t src_ip;            /* Source IP address */
    uint16_t src_port;          /* Source port number */
    struct udp_datagram *next;  /* Next in queue */
} udp_datagram_t;

/**
 * UDP Socket Structure
 * Represents a bound UDP endpoint
 */
typedef struct udp_socket {
    uint16_t local_port;        /* Local port number (0 = unbound) */
    uint32_t local_ip;          /* Local IP address (0 = any) */
    bool     bound;             /* Whether socket is bound */

    /* Receive queue */
    udp_datagram_t *recv_head;  /* Head of receive queue */
    udp_datagram_t *recv_tail;  /* Tail of receive queue */
    uint32_t recv_count;        /* Number of queued datagrams */

    /* Socket flags */
    uint32_t flags;             /* Socket options/flags */

    /* Linked list for socket management */
    struct udp_socket *next;
    struct udp_socket *prev;
} udp_socket_t;

/* Socket flags */
#define UDP_SOCK_FLAG_BROADCAST     BIT(0)  /* Allow broadcast */
#define UDP_SOCK_FLAG_NONBLOCK      BIT(1)  /* Non-blocking mode */

/**
 * Error codes
 */
#define UDP_OK                  0
#define UDP_ERR_NOMEM          -1       /* Out of memory */
#define UDP_ERR_INVALID        -2       /* Invalid argument */
#define UDP_ERR_INUSE          -3       /* Port already in use */
#define UDP_ERR_NOTBOUND       -4       /* Socket not bound */
#define UDP_ERR_WOULDBLOCK     -5       /* No data available (non-blocking) */
#define UDP_ERR_TOOLARGE       -6       /* Datagram too large */
#define UDP_ERR_NOBUFS         -7       /* No buffer space available */
#define UDP_ERR_NOROUTE        -8       /* No route to destination */

/*
 * API Functions
 */

/**
 * Initialize the UDP subsystem
 * Must be called before any other UDP functions
 */
void udp_init(void);

/**
 * Bind to a local port
 * @param port Port number to bind to (0 for ephemeral port)
 * @return UDP socket on success, NULL on failure
 */
udp_socket_t *udp_bind(uint16_t port);

/**
 * Unbind and release a port
 * @param port Port number to release
 * @return UDP_OK on success, negative error code on failure
 */
int udp_unbind(uint16_t port);

/**
 * Close and destroy a UDP socket
 * @param sock Socket to close
 */
void udp_close(udp_socket_t *sock);

/**
 * Send a UDP datagram
 * @param dest_ip Destination IP address (host byte order)
 * @param dest_port Destination port number
 * @param src_port Source port number
 * @param data Payload data
 * @param len Payload length
 * @return Number of bytes sent, or negative error code
 */
ssize_t udp_send(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port,
                 const void *data, size_t len);

/**
 * Send a UDP datagram using a socket
 * @param sock UDP socket to send from
 * @param dest_ip Destination IP address (host byte order)
 * @param dest_port Destination port number
 * @param data Payload data
 * @param len Payload length
 * @return Number of bytes sent, or negative error code
 */
ssize_t udp_sendto(udp_socket_t *sock, uint32_t dest_ip, uint16_t dest_port,
                   const void *data, size_t len);

/**
 * Receive a UDP datagram (non-blocking)
 * @param port Local port to receive on
 * @param buf Buffer to store received data
 * @param max_len Maximum bytes to receive
 * @param src_ip Pointer to store source IP (can be NULL)
 * @param src_port Pointer to store source port (can be NULL)
 * @return Number of bytes received, 0 if no data, or negative error code
 */
ssize_t udp_recv(uint16_t port, void *buf, size_t max_len,
                 uint32_t *src_ip, uint16_t *src_port);

/**
 * Receive a UDP datagram using a socket (non-blocking)
 * @param sock UDP socket to receive from
 * @param buf Buffer to store received data
 * @param max_len Maximum bytes to receive
 * @param src_ip Pointer to store source IP (can be NULL)
 * @param src_port Pointer to store source port (can be NULL)
 * @return Number of bytes received, 0 if no data, or negative error code
 */
ssize_t udp_recvfrom(udp_socket_t *sock, void *buf, size_t max_len,
                     uint32_t *src_ip, uint16_t *src_port);

/**
 * Process incoming UDP packet from IP layer
 * @param src_ip Source IP address (host byte order)
 * @param packet Raw UDP packet data
 * @param len Packet length
 * @return 0 on success, negative error code on failure
 */
int udp_input(uint32_t src_ip, const void *packet, size_t len);

/**
 * Find socket bound to a port
 * @param port Port number to search for
 * @return Socket if found, NULL otherwise
 */
udp_socket_t *udp_find_socket(uint16_t port);

/**
 * Check if a port is in use
 * @param port Port number to check
 * @return true if port is bound, false otherwise
 */
bool udp_port_in_use(uint16_t port);

/**
 * Calculate UDP checksum
 * @param src_ip Source IP address (host byte order)
 * @param dst_ip Destination IP address (host byte order)
 * @param udp_data UDP header + data
 * @param udp_len Total UDP segment length
 * @return Checksum value (network byte order)
 */
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void *udp_data, size_t udp_len);

/**
 * Get number of queued datagrams for a port
 * @param port Port number to check
 * @return Number of queued datagrams, or 0 if port not bound
 */
uint32_t udp_recv_queue_count(uint16_t port);

/**
 * Set socket to non-blocking mode
 * @param sock Socket to modify
 * @param nonblock true for non-blocking, false for blocking
 */
void udp_set_nonblock(udp_socket_t *sock, bool nonblock);

/**
 * Enable/disable broadcast on socket
 * @param sock Socket to modify
 * @param enable true to enable broadcast, false to disable
 */
void udp_set_broadcast(udp_socket_t *sock, bool enable);

/**
 * Print UDP statistics for debugging
 */
void udp_debug_stats(void);

#endif /* _AAAOS_NET_UDP_H */
