/**
 * AAAos Network Stack - TCP Protocol Implementation
 *
 * Implements the Transmission Control Protocol (RFC 793).
 * Features:
 *   - TCP state machine with all 11 states
 *   - Three-way handshake (connection establishment)
 *   - Sequence/acknowledgment number management
 *   - Basic flow control with sliding window
 *   - Connection termination (graceful and abortive)
 */

#include "tcp.h"
#include "../ip/ip.h"
#include "../ethernet/ethernet.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"
#include "../../drivers/timer/pit.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Socket management */
static tcp_socket_t *tcp_socket_list = NULL;    /* List of all sockets */
static uint16_t tcp_next_ephemeral_port = 49152; /* Ephemeral port range start */

/* ISN (Initial Sequence Number) counter - simple increment */
static uint32_t tcp_isn_counter = 0;

/* TCP statistics */
static struct {
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t connections_established;
    uint64_t connections_closed;
    uint64_t retransmissions;
    uint64_t checksum_errors;
} tcp_stats;

/* Forward declarations */
static int tcp_send_segment(tcp_socket_t *sock, uint8_t flags,
                            const void *data, size_t data_len);
void tcp_output(tcp_socket_t *sock);

/* ============================================================================
 * State Name Lookup
 * ============================================================================ */

static const char *tcp_state_names[] = {
    "CLOSED",
    "LISTEN",
    "SYN_SENT",
    "SYN_RECEIVED",
    "ESTABLISHED",
    "FIN_WAIT_1",
    "FIN_WAIT_2",
    "CLOSE_WAIT",
    "CLOSING",
    "LAST_ACK",
    "TIME_WAIT"
};

const char *tcp_state_name(tcp_state_t state) {
    if (state <= TCP_STATE_TIME_WAIT) {
        return tcp_state_names[state];
    }
    return "UNKNOWN";
}

/* ============================================================================
 * Ring Buffer Implementation
 * ============================================================================ */

/**
 * Initialize a ring buffer
 */
static bool ring_buffer_init(tcp_ring_buffer_t *rb, size_t size) {
    rb->buffer = kmalloc(size);
    if (!rb->buffer) {
        return false;
    }
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
    return true;
}

/**
 * Free ring buffer resources
 */
static void ring_buffer_free(tcp_ring_buffer_t *rb) {
    if (rb->buffer) {
        kfree(rb->buffer);
        rb->buffer = NULL;
    }
    rb->size = 0;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
}

/**
 * Get available space in ring buffer
 */
static size_t ring_buffer_space(const tcp_ring_buffer_t *rb) {
    return rb->size - rb->used;
}

/**
 * Get used space in ring buffer
 */
static size_t ring_buffer_used(const tcp_ring_buffer_t *rb) {
    return rb->used;
}

/**
 * Write data to ring buffer
 * Returns number of bytes written
 */
static size_t ring_buffer_write(tcp_ring_buffer_t *rb, const void *data, size_t len) {
    const uint8_t *src = (const uint8_t *)data;
    size_t space = ring_buffer_space(rb);
    size_t to_write = (len < space) ? len : space;
    size_t written = 0;

    while (written < to_write) {
        rb->buffer[rb->head] = src[written];
        rb->head = (rb->head + 1) % rb->size;
        written++;
    }
    rb->used += written;

    return written;
}

/**
 * Read data from ring buffer
 * Returns number of bytes read
 */
static size_t ring_buffer_read(tcp_ring_buffer_t *rb, void *data, size_t len) {
    uint8_t *dst = (uint8_t *)data;
    size_t available = ring_buffer_used(rb);
    size_t to_read = (len < available) ? len : available;
    size_t bytes_read = 0;

    while (bytes_read < to_read) {
        dst[bytes_read] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
        bytes_read++;
    }
    rb->used -= bytes_read;

    return bytes_read;
}

/**
 * Peek data from ring buffer without removing
 */
static size_t ring_buffer_peek(const tcp_ring_buffer_t *rb, void *data, size_t len) {
    uint8_t *dst = (uint8_t *)data;
    size_t available = ring_buffer_used(rb);
    size_t to_read = (len < available) ? len : available;
    size_t tail = rb->tail;

    for (size_t i = 0; i < to_read; i++) {
        dst[i] = rb->buffer[tail];
        tail = (tail + 1) % rb->size;
    }

    return to_read;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Generate initial sequence number
 * Uses a simple counter + time-based value for uniqueness
 */
uint32_t tcp_generate_isn(void) {
    /* Combine counter with time for better randomness */
    uint32_t time_component = (uint32_t)pit_get_uptime_ms();
    tcp_isn_counter += 64000;  /* Increment by a large value */
    return tcp_isn_counter ^ (time_component * 1103515245);
}

/**
 * Calculate TCP checksum including pseudo-header
 */
uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void *tcp_data, size_t tcp_len) {
    uint32_t sum = 0;
    const uint16_t *ptr;

    /* Create pseudo-header and add to sum */
    tcp_pseudo_header_t pseudo;
    pseudo.src_ip = htonl(src_ip);
    pseudo.dst_ip = htonl(dst_ip);
    pseudo.zero = 0;
    pseudo.protocol = TCP_PROTOCOL;
    pseudo.tcp_length = htons((uint16_t)tcp_len);

    /* Sum pseudo-header */
    ptr = (const uint16_t *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo) / 2; i++) {
        sum += ptr[i];
    }

    /* Sum TCP segment */
    ptr = (const uint16_t *)tcp_data;
    size_t count = tcp_len;

    while (count > 1) {
        sum += *ptr++;
        count -= 2;
    }

    /* Add odd byte if present */
    if (count > 0) {
        sum += *(const uint8_t *)ptr;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/**
 * Sequence number comparison helpers
 * TCP sequence numbers wrap around, so we need special comparison
 */
static inline bool seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static inline bool seq_le(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) <= 0;
}

static inline bool seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static inline bool seq_ge(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) >= 0;
}

/**
 * Allocate an ephemeral port
 */
static uint16_t tcp_alloc_ephemeral_port(void) {
    uint16_t start = tcp_next_ephemeral_port;

    do {
        uint16_t port = tcp_next_ephemeral_port;
        tcp_next_ephemeral_port++;
        if (tcp_next_ephemeral_port >= 65535) {
            tcp_next_ephemeral_port = 49152;
        }

        /* Check if port is in use */
        bool in_use = false;
        for (tcp_socket_t *s = tcp_socket_list; s != NULL; s = s->next) {
            if (s->local_port == port) {
                in_use = true;
                break;
            }
        }

        if (!in_use) {
            return port;
        }
    } while (tcp_next_ephemeral_port != start);

    return 0;  /* No ports available */
}

/* ============================================================================
 * Socket Management
 * ============================================================================ */

/**
 * Add socket to global list
 */
static void tcp_socket_list_add(tcp_socket_t *sock) {
    sock->next = tcp_socket_list;
    sock->prev = NULL;
    if (tcp_socket_list) {
        tcp_socket_list->prev = sock;
    }
    tcp_socket_list = sock;
}

/**
 * Remove socket from global list
 */
static void tcp_socket_list_remove(tcp_socket_t *sock) {
    if (sock->prev) {
        sock->prev->next = sock->next;
    } else {
        tcp_socket_list = sock->next;
    }
    if (sock->next) {
        sock->next->prev = sock->prev;
    }
    sock->next = NULL;
    sock->prev = NULL;
}

/**
 * Find socket by 4-tuple (local addr/port, remote addr/port)
 */
static tcp_socket_t *tcp_find_socket(uint32_t local_ip, uint16_t local_port,
                                      uint32_t remote_ip, uint16_t remote_port) {
    for (tcp_socket_t *s = tcp_socket_list; s != NULL; s = s->next) {
        /* Exact match */
        if (s->local_port == local_port && s->remote_port == remote_port &&
            (s->local_ip == local_ip || s->local_ip == 0) &&
            s->remote_ip == remote_ip) {
            return s;
        }
    }
    return NULL;
}

/**
 * Find listening socket for port
 */
static tcp_socket_t *tcp_find_listener(uint16_t port) {
    for (tcp_socket_t *s = tcp_socket_list; s != NULL; s = s->next) {
        if (s->state == TCP_STATE_LISTEN && s->local_port == port) {
            return s;
        }
    }
    return NULL;
}

/* ============================================================================
 * TCP Segment Transmission
 * ============================================================================ */

/**
 * Send a TCP segment
 */
static int tcp_send_segment(tcp_socket_t *sock, uint8_t flags,
                            const void *data, size_t data_len) {
    /* Allocate buffer for TCP header + data */
    size_t total_len = TCP_HEADER_MIN_LEN + data_len;
    uint8_t *segment = kmalloc(total_len);
    if (!segment) {
        kprintf("[TCP] Failed to allocate segment buffer\n");
        return TCP_ERR_NOMEM;
    }

    /* Build TCP header */
    tcp_header_t *hdr = (tcp_header_t *)segment;
    memset(hdr, 0, TCP_HEADER_MIN_LEN);

    hdr->src_port = htons(sock->local_port);
    hdr->dst_port = htons(sock->remote_port);
    hdr->seq_num = htonl(sock->snd_nxt);
    hdr->ack_num = htonl(sock->rcv_nxt);
    hdr->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;  /* 5 words = 20 bytes */
    hdr->flags = flags;
    hdr->window = htons((uint16_t)sock->rcv_wnd);
    hdr->checksum = 0;
    hdr->urgent_ptr = 0;

    /* Copy data if present */
    if (data && data_len > 0) {
        memcpy(segment + TCP_HEADER_MIN_LEN, data, data_len);
    }

    /* Calculate checksum */
    uint32_t local_ip = sock->local_ip ? sock->local_ip : ip_get_addr();
    hdr->checksum = tcp_checksum(local_ip, sock->remote_ip, segment, total_len);

    /* Send via IP layer */
    int result = ip_send(sock->remote_ip, IP_PROTO_TCP, segment, total_len);

    kfree(segment);

    if (result == 0) {
        tcp_stats.packets_sent++;
        tcp_stats.bytes_sent += data_len;

        /* Update sequence number for data and SYN/FIN (they consume sequence space) */
        if (data_len > 0) {
            sock->snd_nxt += data_len;
        }
        if (flags & TCP_FLAG_SYN) {
            sock->snd_nxt++;
        }
        if (flags & TCP_FLAG_FIN) {
            sock->snd_nxt++;
        }
    }

    return result;
}

/**
 * Send RST segment (stateless)
 */
static void tcp_send_rst(uint32_t src_ip, uint32_t dst_ip,
                         uint16_t src_port, uint16_t dst_port,
                         uint32_t seq, uint32_t ack) {
    uint8_t segment[TCP_HEADER_MIN_LEN];
    tcp_header_t *hdr = (tcp_header_t *)segment;

    memset(hdr, 0, TCP_HEADER_MIN_LEN);
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->seq_num = htonl(seq);
    hdr->ack_num = htonl(ack);
    hdr->data_offset = (TCP_HEADER_MIN_LEN / 4) << 4;
    hdr->flags = TCP_FLAG_RST | TCP_FLAG_ACK;
    hdr->window = 0;
    hdr->checksum = 0;

    uint32_t local_ip = src_ip ? src_ip : ip_get_addr();
    hdr->checksum = tcp_checksum(local_ip, dst_ip, segment, TCP_HEADER_MIN_LEN);

    ip_send(dst_ip, IP_PROTO_TCP, segment, TCP_HEADER_MIN_LEN);
    kprintf("[TCP] Sent RST to %08X:%d\n", dst_ip, dst_port);
}

/* ============================================================================
 * TCP State Machine Transitions
 * ============================================================================ */

/**
 * Change TCP state with logging
 */
static void tcp_set_state(tcp_socket_t *sock, tcp_state_t new_state) {
    if (sock->state != new_state) {
        kprintf("[TCP] %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d: %s -> %s\n",
                (sock->local_ip >> 24) & 0xFF, (sock->local_ip >> 16) & 0xFF,
                (sock->local_ip >> 8) & 0xFF, sock->local_ip & 0xFF,
                sock->local_port,
                (sock->remote_ip >> 24) & 0xFF, (sock->remote_ip >> 16) & 0xFF,
                (sock->remote_ip >> 8) & 0xFF, sock->remote_ip & 0xFF,
                sock->remote_port,
                tcp_state_name(sock->state), tcp_state_name(new_state));
        sock->state = new_state;
        sock->last_activity = (uint32_t)pit_get_uptime_ms();
    }
}

/* ============================================================================
 * API Implementation
 * ============================================================================ */

/**
 * Initialize the TCP layer
 */
void tcp_init(void) {
    kprintf("[TCP] Initializing TCP subsystem\n");

    tcp_socket_list = NULL;
    tcp_next_ephemeral_port = 49152;
    tcp_isn_counter = tcp_generate_isn();

    memset(&tcp_stats, 0, sizeof(tcp_stats));

    kprintf("[TCP] TCP initialized, ISN base: %u\n", tcp_isn_counter);
}

/**
 * Create a new TCP socket
 */
tcp_socket_t *tcp_socket_create(void) {
    tcp_socket_t *sock = kcalloc(1, sizeof(tcp_socket_t));
    if (!sock) {
        kprintf("[TCP] Failed to allocate socket\n");
        return NULL;
    }

    /* Initialize buffers */
    if (!ring_buffer_init(&sock->send_buf, TCP_SEND_BUF_SIZE)) {
        kfree(sock);
        kprintf("[TCP] Failed to allocate send buffer\n");
        return NULL;
    }

    if (!ring_buffer_init(&sock->recv_buf, TCP_RECV_BUF_SIZE)) {
        ring_buffer_free(&sock->send_buf);
        kfree(sock);
        kprintf("[TCP] Failed to allocate receive buffer\n");
        return NULL;
    }

    /* Initialize socket */
    sock->state = TCP_STATE_CLOSED;
    sock->rcv_wnd = TCP_DEFAULT_WINDOW;
    sock->rto = TCP_RETRANSMIT_TIMEOUT;
    sock->options.mss = TCP_MSS_DEFAULT;
    sock->last_activity = (uint32_t)pit_get_uptime_ms();

    /* Add to socket list */
    tcp_socket_list_add(sock);

    kprintf("[TCP] Created new socket\n");
    return sock;
}

/**
 * Destroy a TCP socket
 */
void tcp_socket_destroy(tcp_socket_t *sock) {
    if (!sock) {
        return;
    }

    kprintf("[TCP] Destroying socket (state: %s)\n", tcp_state_name(sock->state));

    /* Remove from socket list */
    tcp_socket_list_remove(sock);

    /* Free pending connections if listening socket */
    tcp_pending_conn_t *pending = sock->pending_head;
    while (pending) {
        tcp_pending_conn_t *next = pending->next;
        kfree(pending);
        pending = next;
    }

    /* Free buffers */
    ring_buffer_free(&sock->send_buf);
    ring_buffer_free(&sock->recv_buf);

    /* Free socket */
    kfree(sock);
}

/**
 * Bind socket to a local port
 */
int tcp_bind(tcp_socket_t *sock, uint16_t port) {
    if (!sock) {
        return TCP_ERR_INVALID;
    }

    if (sock->flags & TCP_SOCK_FLAG_BOUND) {
        kprintf("[TCP] Socket already bound\n");
        return TCP_ERR_INVALID;
    }

    /* Allocate ephemeral port if 0 */
    if (port == 0) {
        port = tcp_alloc_ephemeral_port();
        if (port == 0) {
            kprintf("[TCP] No ephemeral ports available\n");
            return TCP_ERR_INUSE;
        }
    } else {
        /* Check if port is already in use */
        for (tcp_socket_t *s = tcp_socket_list; s != NULL; s = s->next) {
            if (s != sock && s->local_port == port &&
                (s->state == TCP_STATE_LISTEN ||
                 (s->flags & TCP_SOCK_FLAG_BOUND))) {
                kprintf("[TCP] Port %d already in use\n", port);
                return TCP_ERR_INUSE;
            }
        }
    }

    sock->local_port = port;
    sock->local_ip = ip_get_addr();  /* Bind to our IP */
    sock->flags |= TCP_SOCK_FLAG_BOUND;

    kprintf("[TCP] Bound to port %d\n", port);
    return TCP_OK;
}

/**
 * Start listening for connections
 */
int tcp_listen(tcp_socket_t *sock, int backlog) {
    if (!sock) {
        return TCP_ERR_INVALID;
    }

    if (!(sock->flags & TCP_SOCK_FLAG_BOUND)) {
        kprintf("[TCP] Socket not bound\n");
        return TCP_ERR_INVALID;
    }

    if (sock->state != TCP_STATE_CLOSED) {
        kprintf("[TCP] Socket not in CLOSED state\n");
        return TCP_ERR_INVALID;
    }

    if (backlog < 1) {
        backlog = 1;
    }
    if (backlog > TCP_LISTEN_BACKLOG_MAX) {
        backlog = TCP_LISTEN_BACKLOG_MAX;
    }

    sock->backlog = backlog;
    sock->pending_count = 0;
    sock->pending_head = NULL;
    sock->flags |= TCP_SOCK_FLAG_LISTENING;

    tcp_set_state(sock, TCP_STATE_LISTEN);

    kprintf("[TCP] Listening on port %d (backlog: %d)\n", sock->local_port, backlog);
    return TCP_OK;
}

/**
 * Accept an incoming connection
 */
tcp_socket_t *tcp_accept(tcp_socket_t *sock) {
    if (!sock || sock->state != TCP_STATE_LISTEN) {
        return NULL;
    }

    /* Check for pending connections */
    tcp_pending_conn_t *pending = sock->pending_head;
    if (!pending) {
        return NULL;  /* No pending connections */
    }

    /* Create new socket for the connection */
    tcp_socket_t *new_sock = tcp_socket_create();
    if (!new_sock) {
        return NULL;
    }

    /* Setup the new socket */
    new_sock->local_ip = sock->local_ip;
    new_sock->local_port = sock->local_port;
    new_sock->remote_ip = pending->remote_ip;
    new_sock->remote_port = pending->remote_port;

    /* Initialize sequence numbers */
    new_sock->iss = tcp_generate_isn();
    new_sock->snd_una = new_sock->iss;
    new_sock->snd_nxt = new_sock->iss;
    new_sock->irs = pending->seq_num;
    new_sock->rcv_nxt = pending->seq_num + 1;  /* SYN consumes one sequence */

    new_sock->flags |= TCP_SOCK_FLAG_BOUND;

    /* Send SYN-ACK */
    tcp_set_state(new_sock, TCP_STATE_SYN_RECEIVED);
    tcp_send_segment(new_sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);

    /* Remove from pending list */
    sock->pending_head = pending->next;
    sock->pending_count--;
    kfree(pending);

    kprintf("[TCP] Accepted connection from %d.%d.%d.%d:%d\n",
            (new_sock->remote_ip >> 24) & 0xFF, (new_sock->remote_ip >> 16) & 0xFF,
            (new_sock->remote_ip >> 8) & 0xFF, new_sock->remote_ip & 0xFF,
            new_sock->remote_port);

    return new_sock;
}

/**
 * Connect to a remote server (active open)
 */
int tcp_connect(tcp_socket_t *sock, uint32_t remote_ip, uint16_t remote_port) {
    if (!sock) {
        return TCP_ERR_INVALID;
    }

    if (sock->state != TCP_STATE_CLOSED) {
        kprintf("[TCP] Socket not in CLOSED state\n");
        return TCP_ERR_INVALID;
    }

    /* Bind to ephemeral port if not bound */
    if (!(sock->flags & TCP_SOCK_FLAG_BOUND)) {
        int result = tcp_bind(sock, 0);
        if (result != TCP_OK) {
            return result;
        }
    }

    /* Set remote endpoint */
    sock->remote_ip = remote_ip;
    sock->remote_port = remote_port;

    /* Initialize sequence numbers */
    sock->iss = tcp_generate_isn();
    sock->snd_una = sock->iss;
    sock->snd_nxt = sock->iss;

    /* Transition to SYN_SENT and send SYN */
    tcp_set_state(sock, TCP_STATE_SYN_SENT);
    int result = tcp_send_segment(sock, TCP_FLAG_SYN, NULL, 0);

    if (result != 0) {
        tcp_set_state(sock, TCP_STATE_CLOSED);
        return result;
    }

    kprintf("[TCP] Connecting to %d.%d.%d.%d:%d\n",
            (remote_ip >> 24) & 0xFF, (remote_ip >> 16) & 0xFF,
            (remote_ip >> 8) & 0xFF, remote_ip & 0xFF, remote_port);

    return TCP_OK;
}

/**
 * Send data over TCP connection
 */
ssize_t tcp_send(tcp_socket_t *sock, const void *data, size_t len) {
    if (!sock || !data) {
        return TCP_ERR_INVALID;
    }

    if (!tcp_can_send(sock)) {
        kprintf("[TCP] Cannot send in state %s\n", tcp_state_name(sock->state));
        return TCP_ERR_NOTCONN;
    }

    if (len == 0) {
        return 0;
    }

    /* Write to send buffer */
    size_t written = ring_buffer_write(&sock->send_buf, data, len);
    if (written == 0) {
        if (sock->flags & TCP_SOCK_FLAG_NONBLOCK) {
            return TCP_ERR_WOULDBLOCK;
        }
        return TCP_ERR_NOBUFS;
    }

    /* Try to send data immediately */
    tcp_output(sock);

    return (ssize_t)written;
}

/**
 * Receive data from TCP connection
 */
ssize_t tcp_recv(tcp_socket_t *sock, void *buf, size_t max_len) {
    if (!sock || !buf) {
        return TCP_ERR_INVALID;
    }

    /* Check if we can receive */
    if (sock->state == TCP_STATE_CLOSED) {
        return TCP_ERR_NOTCONN;
    }

    /* Read from receive buffer */
    size_t bytes_read = ring_buffer_read(&sock->recv_buf, buf, max_len);

    if (bytes_read == 0) {
        /* Check if connection is closed */
        if (sock->state == TCP_STATE_CLOSE_WAIT ||
            sock->state == TCP_STATE_LAST_ACK ||
            sock->state == TCP_STATE_TIME_WAIT ||
            sock->state == TCP_STATE_CLOSING) {
            return 0;  /* EOF - connection closed by peer */
        }

        if (sock->flags & TCP_SOCK_FLAG_NONBLOCK) {
            return TCP_ERR_WOULDBLOCK;
        }
    }

    /* Update receive window */
    sock->rcv_wnd = (uint32_t)ring_buffer_space(&sock->recv_buf);

    return (ssize_t)bytes_read;
}

/**
 * Close a TCP connection gracefully
 */
int tcp_close(tcp_socket_t *sock) {
    if (!sock) {
        return TCP_ERR_INVALID;
    }

    kprintf("[TCP] Closing socket (state: %s)\n", tcp_state_name(sock->state));

    switch (sock->state) {
        case TCP_STATE_CLOSED:
            /* Already closed, destroy socket */
            tcp_socket_destroy(sock);
            return TCP_OK;

        case TCP_STATE_LISTEN:
        case TCP_STATE_SYN_SENT:
            /* No connection established, just close */
            tcp_set_state(sock, TCP_STATE_CLOSED);
            tcp_socket_destroy(sock);
            return TCP_OK;

        case TCP_STATE_SYN_RECEIVED:
        case TCP_STATE_ESTABLISHED:
            /* Send FIN, wait for ACK and FIN from remote */
            tcp_set_state(sock, TCP_STATE_FIN_WAIT_1);
            tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            tcp_stats.connections_closed++;
            return TCP_OK;

        case TCP_STATE_CLOSE_WAIT:
            /* Remote already sent FIN, send our FIN */
            tcp_set_state(sock, TCP_STATE_LAST_ACK);
            tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
            tcp_stats.connections_closed++;
            return TCP_OK;

        case TCP_STATE_FIN_WAIT_1:
        case TCP_STATE_FIN_WAIT_2:
        case TCP_STATE_CLOSING:
        case TCP_STATE_LAST_ACK:
        case TCP_STATE_TIME_WAIT:
            /* Already closing */
            return TCP_OK;

        default:
            return TCP_ERR_INVALID;
    }
}

/**
 * Abort a TCP connection (send RST)
 */
void tcp_abort(tcp_socket_t *sock) {
    if (!sock) {
        return;
    }

    kprintf("[TCP] Aborting socket (state: %s)\n", tcp_state_name(sock->state));

    if (sock->state != TCP_STATE_CLOSED && sock->state != TCP_STATE_LISTEN) {
        /* Send RST */
        tcp_send_rst(sock->local_ip, sock->remote_ip,
                     sock->local_port, sock->remote_port,
                     sock->snd_nxt, sock->rcv_nxt);
    }

    tcp_set_state(sock, TCP_STATE_CLOSED);
    tcp_socket_destroy(sock);
}

/* ============================================================================
 * Output Processing
 * ============================================================================ */

/**
 * Send pending data from send buffer
 */
void tcp_output(tcp_socket_t *sock) {
    if (!sock || !tcp_can_send(sock)) {
        return;
    }

    /* Check if there's data to send */
    size_t pending = ring_buffer_used(&sock->send_buf);
    if (pending == 0) {
        return;
    }

    /* Calculate how much we can send (limited by window and MSS) */
    size_t window = sock->snd_wnd;
    size_t mss = sock->options.mss;
    size_t to_send = pending;

    if (to_send > window) {
        to_send = window;
    }
    if (to_send > mss) {
        to_send = mss;
    }

    if (to_send == 0) {
        return;  /* Window is closed */
    }

    /* Read data from send buffer */
    uint8_t *data = kmalloc(to_send);
    if (!data) {
        return;
    }

    /* Peek data (don't remove until ACKed) */
    size_t actual = ring_buffer_peek(&sock->send_buf, data, to_send);

    /* Send segment with PSH flag if this is the end of data */
    uint8_t flags = TCP_FLAG_ACK;
    if (actual == pending) {
        flags |= TCP_FLAG_PSH;  /* Push - no more data buffered */
    }

    tcp_send_segment(sock, flags, data, actual);

    /* Actually consume data from buffer (simplified - real impl would wait for ACK) */
    ring_buffer_read(&sock->send_buf, data, actual);

    kfree(data);
}

/* ============================================================================
 * Input Processing
 * ============================================================================ */

/**
 * Process incoming TCP packet
 */
int tcp_receive(const void *packet, size_t len, uint32_t src_ip, uint32_t dst_ip) {
    if (!packet || len < TCP_HEADER_MIN_LEN) {
        kprintf("[TCP] Invalid packet (too short)\n");
        return -1;
    }

    const tcp_header_t *hdr = (const tcp_header_t *)packet;

    /* Validate header length */
    uint8_t header_len = tcp_get_header_len(hdr);
    if (header_len < TCP_HEADER_MIN_LEN || header_len > len) {
        kprintf("[TCP] Invalid header length: %d\n", header_len);
        return -1;
    }

    /* Verify checksum */
    uint16_t received_checksum = hdr->checksum;
    tcp_header_t *hdr_copy = (tcp_header_t *)kmalloc(len);
    if (!hdr_copy) {
        return -1;
    }
    memcpy(hdr_copy, packet, len);
    hdr_copy->checksum = 0;

    uint16_t calc_checksum = tcp_checksum(src_ip, dst_ip, hdr_copy, len);
    kfree(hdr_copy);

    if (received_checksum != calc_checksum) {
        kprintf("[TCP] Checksum mismatch (received: %04X, calculated: %04X)\n",
                received_checksum, calc_checksum);
        tcp_stats.checksum_errors++;
        return -1;
    }

    /* Extract header fields */
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint32_t seq = ntohl(hdr->seq_num);
    uint32_t ack = ntohl(hdr->ack_num);
    uint8_t flags = hdr->flags;
    uint16_t window = ntohs(hdr->window);

    tcp_stats.packets_received++;

    kprintf("[TCP] Received: %d.%d.%d.%d:%d -> port %d, seq=%u, ack=%u, flags=%02X\n",
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >> 8) & 0xFF, src_ip & 0xFF,
            src_port, dst_port, seq, ack, flags);

    /* Find matching socket */
    tcp_socket_t *sock = tcp_find_socket(dst_ip, dst_port, src_ip, src_port);

    /* Check for listening socket if no exact match */
    if (!sock) {
        sock = tcp_find_listener(dst_port);
    }

    /* No socket found - send RST */
    if (!sock) {
        if (!(flags & TCP_FLAG_RST)) {
            kprintf("[TCP] No socket for port %d, sending RST\n", dst_port);
            if (flags & TCP_FLAG_ACK) {
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0);
            } else {
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, 0, seq + 1);
            }
        }
        return -1;
    }

    /* Process RST */
    if (flags & TCP_FLAG_RST) {
        kprintf("[TCP] Received RST\n");
        if (sock->state != TCP_STATE_LISTEN) {
            tcp_set_state(sock, TCP_STATE_CLOSED);
        }
        return 0;
    }

    /* Get data pointer and length */
    const uint8_t *data = (const uint8_t *)packet + header_len;
    size_t data_len = len - header_len;

    /* State machine processing */
    switch (sock->state) {
        case TCP_STATE_CLOSED:
            /* Unexpected packet on closed socket */
            if (!(flags & TCP_FLAG_RST)) {
                tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, seq + 1);
            }
            break;

        case TCP_STATE_LISTEN:
            if (flags & TCP_FLAG_SYN) {
                /* SYN received - add to pending queue */
                if (sock->pending_count >= sock->backlog) {
                    kprintf("[TCP] Listen backlog full\n");
                    break;
                }

                tcp_pending_conn_t *pending = kmalloc(sizeof(tcp_pending_conn_t));
                if (!pending) {
                    kprintf("[TCP] Failed to allocate pending connection\n");
                    break;
                }

                pending->remote_ip = src_ip;
                pending->remote_port = src_port;
                pending->seq_num = seq;
                pending->timestamp = (uint32_t)pit_get_uptime_ms();
                pending->next = sock->pending_head;
                sock->pending_head = pending;
                sock->pending_count++;

                kprintf("[TCP] Connection request queued from %d.%d.%d.%d:%d\n",
                        (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                        (src_ip >> 8) & 0xFF, src_ip & 0xFF, src_port);
            }
            break;

        case TCP_STATE_SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                /* SYN-ACK received - complete three-way handshake */
                if (ack != sock->snd_nxt) {
                    kprintf("[TCP] Invalid ACK in SYN_SENT\n");
                    tcp_send_rst(dst_ip, src_ip, dst_port, src_port, ack, 0);
                    break;
                }

                sock->snd_una = ack;
                sock->irs = seq;
                sock->rcv_nxt = seq + 1;
                sock->snd_wnd = window;

                /* Send ACK */
                tcp_set_state(sock, TCP_STATE_ESTABLISHED);
                tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);
                sock->flags |= TCP_SOCK_FLAG_CONNECTED;
                tcp_stats.connections_established++;

                kprintf("[TCP] Connection established!\n");
            } else if (flags & TCP_FLAG_SYN) {
                /* Simultaneous open - SYN without ACK */
                sock->irs = seq;
                sock->rcv_nxt = seq + 1;

                tcp_set_state(sock, TCP_STATE_SYN_RECEIVED);
                tcp_send_segment(sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
            }
            break;

        case TCP_STATE_SYN_RECEIVED:
            if (flags & TCP_FLAG_ACK) {
                if (ack == sock->snd_nxt) {
                    /* ACK for our SYN-ACK - connection established */
                    sock->snd_una = ack;
                    sock->snd_wnd = window;

                    tcp_set_state(sock, TCP_STATE_ESTABLISHED);
                    sock->flags |= TCP_SOCK_FLAG_CONNECTED;
                    tcp_stats.connections_established++;

                    kprintf("[TCP] Connection established (passive)!\n");
                }
            }
            break;

        case TCP_STATE_ESTABLISHED:
            /* Process ACK */
            if (flags & TCP_FLAG_ACK) {
                if (seq_gt(ack, sock->snd_una) && seq_le(ack, sock->snd_nxt)) {
                    sock->snd_una = ack;
                }
                sock->snd_wnd = window;
            }

            /* Process data */
            if (data_len > 0) {
                if (seq == sock->rcv_nxt) {
                    /* In-order data */
                    size_t written = ring_buffer_write(&sock->recv_buf, data, data_len);
                    sock->rcv_nxt += written;
                    sock->rcv_wnd = (uint32_t)ring_buffer_space(&sock->recv_buf);
                    tcp_stats.bytes_received += written;

                    /* Send ACK */
                    tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);

                    kprintf("[TCP] Received %zu bytes of data\n", written);
                } else {
                    /* Out of order - send duplicate ACK */
                    tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);
                }
            }

            /* Process FIN */
            if (flags & TCP_FLAG_FIN) {
                sock->rcv_nxt++;
                tcp_set_state(sock, TCP_STATE_CLOSE_WAIT);
                tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);
                kprintf("[TCP] Received FIN, entering CLOSE_WAIT\n");
            }
            break;

        case TCP_STATE_FIN_WAIT_1:
            /* Process ACK of our FIN */
            if (flags & TCP_FLAG_ACK) {
                if (ack == sock->snd_nxt) {
                    tcp_set_state(sock, TCP_STATE_FIN_WAIT_2);
                }
            }

            /* Process FIN (possibly simultaneous) */
            if (flags & TCP_FLAG_FIN) {
                sock->rcv_nxt++;
                tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);

                if (sock->state == TCP_STATE_FIN_WAIT_2) {
                    /* FIN already ACKed, go to TIME_WAIT */
                    tcp_set_state(sock, TCP_STATE_TIME_WAIT);
                    sock->time_wait_start = (uint32_t)pit_get_uptime_ms();
                } else {
                    /* Simultaneous close */
                    tcp_set_state(sock, TCP_STATE_CLOSING);
                }
            }
            break;

        case TCP_STATE_FIN_WAIT_2:
            /* Waiting for FIN from remote */
            if (flags & TCP_FLAG_FIN) {
                sock->rcv_nxt++;
                tcp_set_state(sock, TCP_STATE_TIME_WAIT);
                sock->time_wait_start = (uint32_t)pit_get_uptime_ms();
                tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);
            }
            break;

        case TCP_STATE_CLOSE_WAIT:
            /* Waiting for application to close */
            if (flags & TCP_FLAG_ACK) {
                sock->snd_una = ack;
            }
            break;

        case TCP_STATE_CLOSING:
            /* Waiting for ACK of our FIN */
            if (flags & TCP_FLAG_ACK) {
                if (ack == sock->snd_nxt) {
                    tcp_set_state(sock, TCP_STATE_TIME_WAIT);
                    sock->time_wait_start = (uint32_t)pit_get_uptime_ms();
                }
            }
            break;

        case TCP_STATE_LAST_ACK:
            /* Waiting for final ACK */
            if (flags & TCP_FLAG_ACK) {
                if (ack == sock->snd_nxt) {
                    tcp_set_state(sock, TCP_STATE_CLOSED);
                    tcp_socket_destroy(sock);
                }
            }
            break;

        case TCP_STATE_TIME_WAIT:
            /* In TIME_WAIT - respond to any FIN with ACK */
            if (flags & TCP_FLAG_FIN) {
                tcp_send_segment(sock, TCP_FLAG_ACK, NULL, 0);
            }
            break;

        default:
            kprintf("[TCP] Unexpected state: %d\n", sock->state);
            break;
    }

    return 0;
}

/* ============================================================================
 * Timer Processing
 * ============================================================================ */

/**
 * Process TCP timers (called periodically)
 */
void tcp_timer_tick(void) {
    uint32_t now = (uint32_t)pit_get_uptime_ms();

    tcp_socket_t *sock = tcp_socket_list;
    while (sock) {
        tcp_socket_t *next = sock->next;  /* Save next in case we destroy sock */

        switch (sock->state) {
            case TCP_STATE_TIME_WAIT:
                /* Check if TIME_WAIT has expired */
                if ((now - sock->time_wait_start) >= TCP_TIME_WAIT_TIMEOUT) {
                    kprintf("[TCP] TIME_WAIT expired\n");
                    tcp_set_state(sock, TCP_STATE_CLOSED);
                    tcp_socket_destroy(sock);
                }
                break;

            case TCP_STATE_SYN_SENT:
            case TCP_STATE_SYN_RECEIVED:
                /* Check for connection timeout */
                if ((now - sock->last_activity) >= (sock->rto * (sock->retries + 1))) {
                    if (sock->retries >= TCP_MAX_RETRIES) {
                        kprintf("[TCP] Connection timeout\n");
                        tcp_abort(sock);
                    } else {
                        /* Retransmit SYN or SYN-ACK */
                        sock->retries++;
                        tcp_stats.retransmissions++;
                        if (sock->state == TCP_STATE_SYN_SENT) {
                            sock->snd_nxt = sock->iss;  /* Reset seq for retransmit */
                            tcp_send_segment(sock, TCP_FLAG_SYN, NULL, 0);
                        } else {
                            sock->snd_nxt = sock->iss;
                            tcp_send_segment(sock, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
                        }
                        kprintf("[TCP] Retransmit #%d\n", sock->retries);
                    }
                }
                break;

            default:
                break;
        }

        sock = next;
    }
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Set socket to non-blocking mode
 */
void tcp_set_nonblock(tcp_socket_t *sock, bool nonblock) {
    if (!sock) {
        return;
    }
    if (nonblock) {
        sock->flags |= TCP_SOCK_FLAG_NONBLOCK;
    } else {
        sock->flags &= ~TCP_SOCK_FLAG_NONBLOCK;
    }
}

/**
 * Get socket error (placeholder - returns 0)
 */
int tcp_get_error(tcp_socket_t *sock) {
    UNUSED(sock);
    return 0;
}

/**
 * Print TCP socket information for debugging
 */
void tcp_debug_print_socket(const tcp_socket_t *sock) {
    if (!sock) {
        kprintf("[TCP] NULL socket\n");
        return;
    }

    kprintf("[TCP] Socket Debug Info:\n");
    kprintf("  Local:  %d.%d.%d.%d:%d\n",
            (sock->local_ip >> 24) & 0xFF, (sock->local_ip >> 16) & 0xFF,
            (sock->local_ip >> 8) & 0xFF, sock->local_ip & 0xFF,
            sock->local_port);
    kprintf("  Remote: %d.%d.%d.%d:%d\n",
            (sock->remote_ip >> 24) & 0xFF, (sock->remote_ip >> 16) & 0xFF,
            (sock->remote_ip >> 8) & 0xFF, sock->remote_ip & 0xFF,
            sock->remote_port);
    kprintf("  State:  %s\n", tcp_state_name(sock->state));
    kprintf("  Send:   UNA=%u NXT=%u WND=%u\n",
            sock->snd_una, sock->snd_nxt, sock->snd_wnd);
    kprintf("  Recv:   NXT=%u WND=%u\n",
            sock->rcv_nxt, sock->rcv_wnd);
    kprintf("  Buffers: send=%zu/%zu recv=%zu/%zu\n",
            ring_buffer_used(&sock->send_buf), sock->send_buf.size,
            ring_buffer_used(&sock->recv_buf), sock->recv_buf.size);
}
