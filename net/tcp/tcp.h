/**
 * AAAos Network Stack - TCP Protocol Implementation
 *
 * Implements the Transmission Control Protocol (RFC 793).
 * Features:
 *   - TCP state machine with all 11 states
 *   - Three-way handshake (connection establishment)
 *   - Sequence/acknowledgment number management
 *   - Basic flow control with sliding window
 *   - Connection termination
 */

#ifndef _AAAOS_NET_TCP_H
#define _AAAOS_NET_TCP_H

#include "../../kernel/include/types.h"
#include "../core/netbuf.h"

/* TCP Protocol Constants */
#define TCP_PROTOCOL            6           /* IP protocol number for TCP */
#define TCP_HEADER_MIN_LEN      20          /* Minimum TCP header size */
#define TCP_HEADER_MAX_LEN      60          /* Maximum TCP header size (with options) */
#define TCP_MAX_WINDOW          65535       /* Maximum window size (16-bit) */
#define TCP_DEFAULT_WINDOW      32768       /* Default window size */
#define TCP_MSS_DEFAULT         1460        /* Default MSS for Ethernet */
#define TCP_MAX_SOCKETS         256         /* Maximum concurrent TCP sockets */
#define TCP_LISTEN_BACKLOG_MAX  128         /* Maximum listen queue size */

/* Retransmission constants */
#define TCP_RETRANSMIT_TIMEOUT  1000        /* Initial retransmit timeout (ms) */
#define TCP_MAX_RETRIES         5           /* Maximum retransmission attempts */
#define TCP_TIME_WAIT_TIMEOUT   60000       /* TIME_WAIT duration (ms) */

/* TCP Buffer sizes */
#define TCP_RECV_BUF_SIZE       65536       /* Receive buffer size */
#define TCP_SEND_BUF_SIZE       65536       /* Send buffer size */

/**
 * TCP Header Flags
 */
#define TCP_FLAG_FIN            0x01        /* Finish - no more data from sender */
#define TCP_FLAG_SYN            0x02        /* Synchronize sequence numbers */
#define TCP_FLAG_RST            0x04        /* Reset the connection */
#define TCP_FLAG_PSH            0x08        /* Push function */
#define TCP_FLAG_ACK            0x10        /* Acknowledgment field significant */
#define TCP_FLAG_URG            0x20        /* Urgent pointer field significant */
#define TCP_FLAG_ECE            0x40        /* ECN-Echo */
#define TCP_FLAG_CWR            0x80        /* Congestion Window Reduced */

/**
 * TCP States (RFC 793 State Machine)
 */
typedef enum tcp_state {
    TCP_STATE_CLOSED = 0,       /* No connection */
    TCP_STATE_LISTEN,           /* Waiting for connection request */
    TCP_STATE_SYN_SENT,         /* SYN sent, waiting for SYN-ACK */
    TCP_STATE_SYN_RECEIVED,     /* SYN received, SYN-ACK sent */
    TCP_STATE_ESTABLISHED,      /* Connection established, data transfer */
    TCP_STATE_FIN_WAIT_1,       /* FIN sent, waiting for ACK or FIN */
    TCP_STATE_FIN_WAIT_2,       /* FIN acked, waiting for FIN */
    TCP_STATE_CLOSE_WAIT,       /* FIN received, waiting for close */
    TCP_STATE_CLOSING,          /* Both sides sent FIN simultaneously */
    TCP_STATE_LAST_ACK,         /* Waiting for final ACK */
    TCP_STATE_TIME_WAIT         /* Waiting before fully closing */
} tcp_state_t;

/**
 * TCP Header Structure (20 bytes minimum)
 *
 * Format:
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          Source Port          |       Destination Port        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                        Sequence Number                        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    Acknowledgment Number                      |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |  Data |           |U|A|P|R|S|F|                               |
 *  | Offset| Reserved  |R|C|S|S|Y|I|            Window             |
 *  |       |           |G|K|H|T|N|N|                               |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |           Checksum            |         Urgent Pointer        |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |                    Options                    |    Padding    |
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
typedef struct PACKED tcp_header {
    uint16_t src_port;          /* Source port number */
    uint16_t dst_port;          /* Destination port number */
    uint32_t seq_num;           /* Sequence number */
    uint32_t ack_num;           /* Acknowledgment number */
    uint8_t  data_offset;       /* Data offset (high 4 bits) + reserved (low 4 bits) */
    uint8_t  flags;             /* TCP flags */
    uint16_t window;            /* Window size */
    uint16_t checksum;          /* Checksum */
    uint16_t urgent_ptr;        /* Urgent pointer */
    /* Options follow if data_offset > 5 */
} tcp_header_t;

/**
 * TCP Pseudo-header for checksum calculation
 */
typedef struct PACKED tcp_pseudo_header {
    uint32_t src_ip;            /* Source IP address */
    uint32_t dst_ip;            /* Destination IP address */
    uint8_t  zero;              /* Reserved (must be zero) */
    uint8_t  protocol;          /* Protocol (6 for TCP) */
    uint16_t tcp_length;        /* TCP segment length (header + data) */
} tcp_pseudo_header_t;

/**
 * TCP Ring Buffer for data storage
 */
typedef struct tcp_ring_buffer {
    uint8_t *buffer;            /* Buffer data */
    size_t  size;               /* Total buffer size */
    size_t  head;               /* Write position */
    size_t  tail;               /* Read position */
    size_t  used;               /* Bytes currently in buffer */
} tcp_ring_buffer_t;

/**
 * TCP Socket Options
 */
typedef struct tcp_options {
    uint16_t mss;               /* Maximum Segment Size */
    bool     no_delay;          /* Disable Nagle's algorithm */
    bool     keep_alive;        /* Enable keep-alive probes */
    uint32_t keep_alive_time;   /* Keep-alive timeout (ms) */
} tcp_options_t;

/**
 * TCP Connection block for pending connections
 */
typedef struct tcp_pending_conn {
    uint32_t remote_ip;         /* Remote IP address */
    uint16_t remote_port;       /* Remote port */
    uint32_t seq_num;           /* Initial sequence number from remote */
    uint32_t timestamp;         /* Connection request timestamp */
    struct tcp_pending_conn *next;
} tcp_pending_conn_t;

/**
 * TCP Socket Structure
 * Represents a single TCP connection endpoint
 */
typedef struct tcp_socket {
    /* Socket identity */
    uint16_t local_port;        /* Local port number */
    uint16_t remote_port;       /* Remote port number */
    uint32_t local_ip;          /* Local IP address */
    uint32_t remote_ip;         /* Remote IP address */

    /* State machine */
    tcp_state_t state;          /* Current TCP state */

    /* Sequence numbers - Send side */
    uint32_t snd_una;           /* Send unacknowledged (oldest unacked seq) */
    uint32_t snd_nxt;           /* Send next (next seq to send) */
    uint32_t snd_wnd;           /* Send window (advertised by remote) */
    uint32_t iss;               /* Initial send sequence number */

    /* Sequence numbers - Receive side */
    uint32_t rcv_nxt;           /* Receive next (next expected seq) */
    uint32_t rcv_wnd;           /* Receive window (our advertised window) */
    uint32_t irs;               /* Initial receive sequence number */

    /* Buffers */
    tcp_ring_buffer_t send_buf; /* Send buffer */
    tcp_ring_buffer_t recv_buf; /* Receive buffer */

    /* Retransmission */
    uint32_t rto;               /* Retransmission timeout (ms) */
    uint32_t srtt;              /* Smoothed round-trip time */
    uint32_t rttvar;            /* RTT variance */
    uint8_t  retries;           /* Current retry count */

    /* Timing */
    uint32_t time_wait_start;   /* TIME_WAIT start timestamp */
    uint32_t last_activity;     /* Last activity timestamp */

    /* Listen queue (for listening sockets) */
    int backlog;                /* Maximum pending connections */
    int pending_count;          /* Current pending connections */
    tcp_pending_conn_t *pending_head; /* Pending connection queue */

    /* Options */
    tcp_options_t options;      /* Socket options */

    /* Socket flags */
    uint32_t flags;             /* Internal flags */

    /* Linked list for socket management */
    struct tcp_socket *next;
    struct tcp_socket *prev;
} tcp_socket_t;

/* Socket flags */
#define TCP_SOCK_FLAG_BOUND         BIT(0)  /* Socket is bound to port */
#define TCP_SOCK_FLAG_LISTENING     BIT(1)  /* Socket is listening */
#define TCP_SOCK_FLAG_CONNECTED     BIT(2)  /* Socket is connected */
#define TCP_SOCK_FLAG_NONBLOCK      BIT(3)  /* Non-blocking mode */

/**
 * Error codes
 */
#define TCP_OK                  0
#define TCP_ERR_NOMEM          -1       /* Out of memory */
#define TCP_ERR_INVALID        -2       /* Invalid argument */
#define TCP_ERR_NOTCONN        -3       /* Not connected */
#define TCP_ERR_CONNREFUSED    -4       /* Connection refused */
#define TCP_ERR_TIMEOUT        -5       /* Operation timed out */
#define TCP_ERR_INUSE          -6       /* Port already in use */
#define TCP_ERR_WOULDBLOCK     -7       /* Operation would block */
#define TCP_ERR_RESET          -8       /* Connection reset */
#define TCP_ERR_CLOSED         -9       /* Socket closed */
#define TCP_ERR_NOBUFS         -10      /* No buffer space */

/*
 * API Functions
 */

/**
 * Initialize the TCP layer
 * Must be called before any other TCP functions
 */
void tcp_init(void);

/**
 * Create a new TCP socket
 * @return Pointer to new socket, or NULL on failure
 */
tcp_socket_t *tcp_socket_create(void);

/**
 * Destroy a TCP socket and free resources
 * @param sock Socket to destroy
 */
void tcp_socket_destroy(tcp_socket_t *sock);

/**
 * Bind socket to a local port
 * @param sock Socket to bind
 * @param port Local port number (0 for ephemeral port)
 * @return TCP_OK on success, negative error code on failure
 */
int tcp_bind(tcp_socket_t *sock, uint16_t port);

/**
 * Start listening for incoming connections
 * @param sock Socket to listen on (must be bound)
 * @param backlog Maximum pending connections
 * @return TCP_OK on success, negative error code on failure
 */
int tcp_listen(tcp_socket_t *sock, int backlog);

/**
 * Accept an incoming connection
 * @param sock Listening socket
 * @return New connected socket, or NULL if no pending connections
 */
tcp_socket_t *tcp_accept(tcp_socket_t *sock);

/**
 * Connect to a remote server
 * @param sock Socket to use for connection
 * @param remote_ip Remote IP address (network byte order)
 * @param remote_port Remote port number
 * @return TCP_OK on success, negative error code on failure
 */
int tcp_connect(tcp_socket_t *sock, uint32_t remote_ip, uint16_t remote_port);

/**
 * Send data over TCP connection
 * @param sock Connected socket
 * @param data Data to send
 * @param len Number of bytes to send
 * @return Number of bytes sent, or negative error code
 */
ssize_t tcp_send(tcp_socket_t *sock, const void *data, size_t len);

/**
 * Receive data from TCP connection
 * @param sock Connected socket
 * @param buf Buffer to store received data
 * @param max_len Maximum bytes to receive
 * @return Number of bytes received, or negative error code
 */
ssize_t tcp_recv(tcp_socket_t *sock, void *buf, size_t max_len);

/**
 * Close a TCP connection gracefully
 * @param sock Socket to close
 * @return TCP_OK on success, negative error code on failure
 */
int tcp_close(tcp_socket_t *sock);

/**
 * Abort a TCP connection immediately (send RST)
 * @param sock Socket to abort
 */
void tcp_abort(tcp_socket_t *sock);

/**
 * Handle incoming TCP packet (called by IP layer)
 * @param packet Raw TCP segment
 * @param len Segment length
 * @param src_ip Source IP address (network byte order)
 * @param dst_ip Destination IP address (network byte order)
 * @return 0 on success, negative on error
 */
int tcp_receive(const void *packet, size_t len, uint32_t src_ip, uint32_t dst_ip);

/**
 * Process TCP timers (should be called periodically)
 * Handles retransmission, TIME_WAIT cleanup, etc.
 */
void tcp_timer_tick(void);

/**
 * Send pending data from send buffer
 * @param sock Connected socket
 */
void tcp_output(tcp_socket_t *sock);

/*
 * Utility Functions
 */

/**
 * Get string name for TCP state
 * @param state TCP state value
 * @return Human-readable state name
 */
const char *tcp_state_name(tcp_state_t state);

/**
 * Calculate TCP checksum
 * @param src_ip Source IP address
 * @param dst_ip Destination IP address
 * @param tcp_data TCP header + data
 * @param tcp_len Total TCP segment length
 * @return Checksum value (already in network byte order)
 */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void *tcp_data, size_t tcp_len);

/**
 * Generate initial sequence number
 * @return Random initial sequence number
 */
uint32_t tcp_generate_isn(void);

/**
 * Set socket to non-blocking mode
 * @param sock Socket to modify
 * @param nonblock true for non-blocking, false for blocking
 */
void tcp_set_nonblock(tcp_socket_t *sock, bool nonblock);

/**
 * Get socket error (clears error after reading)
 * @param sock Socket to query
 * @return Last error code, or 0 if no error
 */
int tcp_get_error(tcp_socket_t *sock);

/**
 * Check if socket is connected
 * @param sock Socket to check
 * @return true if in ESTABLISHED state
 */
static inline bool tcp_is_connected(const tcp_socket_t *sock) {
    return sock && sock->state == TCP_STATE_ESTABLISHED;
}

/**
 * Check if socket can send data
 * @param sock Socket to check
 * @return true if data can be sent
 */
static inline bool tcp_can_send(const tcp_socket_t *sock) {
    if (!sock) return false;
    return sock->state == TCP_STATE_ESTABLISHED ||
           sock->state == TCP_STATE_CLOSE_WAIT;
}

/**
 * Check if socket can receive data
 * @param sock Socket to check
 * @return true if data can be received
 */
static inline bool tcp_can_recv(const tcp_socket_t *sock) {
    if (!sock) return false;
    return sock->state == TCP_STATE_ESTABLISHED ||
           sock->state == TCP_STATE_FIN_WAIT_1 ||
           sock->state == TCP_STATE_FIN_WAIT_2;
}

/**
 * Get data offset from TCP header
 * @param hdr TCP header
 * @return Header length in bytes
 */
static inline uint8_t tcp_get_header_len(const tcp_header_t *hdr) {
    return (hdr->data_offset >> 4) * 4;
}

/**
 * Print TCP socket information for debugging
 * @param sock Socket to print
 */
void tcp_debug_print_socket(const tcp_socket_t *sock);

#endif /* _AAAOS_NET_TCP_H */
