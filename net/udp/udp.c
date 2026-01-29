/**
 * AAAos Network Stack - UDP Protocol Implementation
 *
 * Implements the User Datagram Protocol (RFC 768).
 */

#include "udp.h"
#include "../ip/ip.h"
#include "../../kernel/include/serial.h"

/* Forward declarations for memory functions */
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

/*
 * UDP Statistics
 */
static struct {
    uint64_t packets_sent;      /* Total packets sent */
    uint64_t packets_recv;      /* Total packets received */
    uint64_t bytes_sent;        /* Total bytes sent */
    uint64_t bytes_recv;        /* Total bytes received */
    uint64_t checksum_errors;   /* Checksum validation failures */
    uint64_t port_unreachable;  /* No socket for destination port */
    uint64_t queue_full;        /* Receive queue full drops */
    uint64_t invalid_packets;   /* Malformed packets dropped */
} udp_stats;

/*
 * Socket Management
 */
static udp_socket_t *socket_list_head = NULL;
static udp_socket_t *socket_list_tail = NULL;
static uint32_t socket_count = 0;
static uint16_t next_ephemeral_port = UDP_PORT_EPHEMERAL_MIN;
static bool udp_initialized = false;

/*
 * Byte Order Conversion (Network byte order is big-endian)
 */
static inline uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);  /* Same operation */
}

static inline uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong >> 8) & 0xFF00) |
           ((hostlong >> 24) & 0xFF);
}

static inline uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);  /* Same operation */
}

/*
 * Internal Helper Functions
 */

/**
 * Allocate and initialize a new UDP socket
 */
static udp_socket_t *socket_alloc(void) {
    if (socket_count >= UDP_MAX_SOCKETS) {
        kprintf("[UDP] Maximum sockets reached (%u)\n", UDP_MAX_SOCKETS);
        return NULL;
    }

    udp_socket_t *sock = kmalloc(sizeof(udp_socket_t));
    if (!sock) {
        kprintf("[UDP] Failed to allocate socket\n");
        return NULL;
    }

    memset(sock, 0, sizeof(udp_socket_t));
    sock->bound = false;
    sock->recv_head = NULL;
    sock->recv_tail = NULL;
    sock->recv_count = 0;
    sock->flags = 0;
    sock->next = NULL;
    sock->prev = NULL;

    return sock;
}

/**
 * Free a UDP socket and all queued datagrams
 */
static void socket_free(udp_socket_t *sock) {
    if (!sock) return;

    /* Free all queued datagrams */
    udp_datagram_t *dgram = sock->recv_head;
    while (dgram) {
        udp_datagram_t *next = dgram->next;
        if (dgram->data) {
            kfree(dgram->data);
        }
        kfree(dgram);
        dgram = next;
    }

    kfree(sock);
}

/**
 * Add socket to the global list
 */
static void socket_list_add(udp_socket_t *sock) {
    sock->next = NULL;
    sock->prev = socket_list_tail;

    if (socket_list_tail) {
        socket_list_tail->next = sock;
    } else {
        socket_list_head = sock;
    }
    socket_list_tail = sock;
    socket_count++;
}

/**
 * Remove socket from the global list
 */
static void socket_list_remove(udp_socket_t *sock) {
    if (sock->prev) {
        sock->prev->next = sock->next;
    } else {
        socket_list_head = sock->next;
    }

    if (sock->next) {
        sock->next->prev = sock->prev;
    } else {
        socket_list_tail = sock->prev;
    }

    sock->next = NULL;
    sock->prev = NULL;
    socket_count--;
}

/**
 * Allocate an ephemeral port
 */
static uint16_t allocate_ephemeral_port(void) {
    uint16_t start = next_ephemeral_port;

    do {
        if (!udp_port_in_use(next_ephemeral_port)) {
            uint16_t port = next_ephemeral_port;
            next_ephemeral_port++;
            if (next_ephemeral_port > UDP_PORT_EPHEMERAL_MAX) {
                next_ephemeral_port = UDP_PORT_EPHEMERAL_MIN;
            }
            return port;
        }

        next_ephemeral_port++;
        if (next_ephemeral_port > UDP_PORT_EPHEMERAL_MAX) {
            next_ephemeral_port = UDP_PORT_EPHEMERAL_MIN;
        }
    } while (next_ephemeral_port != start);

    return 0;  /* No port available */
}

/**
 * Queue a received datagram for a socket
 */
static int queue_datagram(udp_socket_t *sock, const void *data, size_t len,
                          uint32_t src_ip, uint16_t src_port) {
    if (sock->recv_count >= UDP_RECV_QUEUE_SIZE) {
        udp_stats.queue_full++;
        kprintf("[UDP] Receive queue full for port %u, dropping packet\n",
                sock->local_port);
        return UDP_ERR_NOBUFS;
    }

    udp_datagram_t *dgram = kmalloc(sizeof(udp_datagram_t));
    if (!dgram) {
        return UDP_ERR_NOMEM;
    }

    dgram->data = kmalloc(len);
    if (!dgram->data) {
        kfree(dgram);
        return UDP_ERR_NOMEM;
    }

    memcpy(dgram->data, data, len);
    dgram->len = len;
    dgram->src_ip = src_ip;
    dgram->src_port = src_port;
    dgram->next = NULL;

    /* Add to tail of queue */
    if (sock->recv_tail) {
        sock->recv_tail->next = dgram;
    } else {
        sock->recv_head = dgram;
    }
    sock->recv_tail = dgram;
    sock->recv_count++;

    return UDP_OK;
}

/**
 * Dequeue a datagram from a socket
 */
static udp_datagram_t *dequeue_datagram(udp_socket_t *sock) {
    if (!sock->recv_head) {
        return NULL;
    }

    udp_datagram_t *dgram = sock->recv_head;
    sock->recv_head = dgram->next;

    if (!sock->recv_head) {
        sock->recv_tail = NULL;
    }

    dgram->next = NULL;
    sock->recv_count--;

    return dgram;
}

/*
 * Public API Implementation
 */

/**
 * Initialize the UDP subsystem
 */
void udp_init(void) {
    if (udp_initialized) {
        kprintf("[UDP] Already initialized\n");
        return;
    }

    /* Clear statistics */
    memset(&udp_stats, 0, sizeof(udp_stats));

    /* Initialize socket list */
    socket_list_head = NULL;
    socket_list_tail = NULL;
    socket_count = 0;
    next_ephemeral_port = UDP_PORT_EPHEMERAL_MIN;

    udp_initialized = true;
    kprintf("[UDP] UDP subsystem initialized\n");
}

/**
 * Find socket bound to a port
 */
udp_socket_t *udp_find_socket(uint16_t port) {
    udp_socket_t *sock = socket_list_head;

    while (sock) {
        if (sock->bound && sock->local_port == port) {
            return sock;
        }
        sock = sock->next;
    }

    return NULL;
}

/**
 * Check if a port is in use
 */
bool udp_port_in_use(uint16_t port) {
    return udp_find_socket(port) != NULL;
}

/**
 * Bind to a local port
 */
udp_socket_t *udp_bind(uint16_t port) {
    if (!udp_initialized) {
        kprintf("[UDP] Error: UDP not initialized\n");
        return NULL;
    }

    /* Allocate ephemeral port if requested */
    if (port == 0) {
        port = allocate_ephemeral_port();
        if (port == 0) {
            kprintf("[UDP] No ephemeral ports available\n");
            return NULL;
        }
    }

    /* Check if port is already in use */
    if (udp_port_in_use(port)) {
        kprintf("[UDP] Port %u already in use\n", port);
        return NULL;
    }

    /* Create and initialize socket */
    udp_socket_t *sock = socket_alloc();
    if (!sock) {
        return NULL;
    }

    sock->local_port = port;
    sock->local_ip = 0;  /* Bind to any address */
    sock->bound = true;

    /* Add to socket list */
    socket_list_add(sock);

    kprintf("[UDP] Bound to port %u\n", port);
    return sock;
}

/**
 * Unbind and release a port
 */
int udp_unbind(uint16_t port) {
    if (!udp_initialized) {
        return UDP_ERR_INVALID;
    }

    udp_socket_t *sock = udp_find_socket(port);
    if (!sock) {
        kprintf("[UDP] Port %u not bound\n", port);
        return UDP_ERR_NOTBOUND;
    }

    /* Remove from list and free */
    socket_list_remove(sock);
    socket_free(sock);

    kprintf("[UDP] Unbound port %u\n", port);
    return UDP_OK;
}

/**
 * Close and destroy a UDP socket
 */
void udp_close(udp_socket_t *sock) {
    if (!sock) return;

    if (sock->bound) {
        socket_list_remove(sock);
    }
    socket_free(sock);
}

/**
 * Calculate UDP checksum using pseudo-header
 */
uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                      const void *udp_data, size_t udp_len) {
    uint32_t sum = 0;
    const uint16_t *ptr;

    /* Pseudo-header */
    udp_pseudo_header_t pseudo;
    pseudo.src_ip = htonl(src_ip);
    pseudo.dst_ip = htonl(dst_ip);
    pseudo.zero = 0;
    pseudo.protocol = UDP_PROTOCOL;
    pseudo.udp_length = htons((uint16_t)udp_len);

    /* Sum pseudo-header */
    ptr = (const uint16_t *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo) / 2; i++) {
        sum += ptr[i];
    }

    /* Sum UDP header and data */
    ptr = (const uint16_t *)udp_data;
    size_t len = udp_len;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    /* Handle odd byte */
    if (len == 1) {
        sum += *((const uint8_t *)ptr);
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/**
 * Send a UDP datagram
 */
ssize_t udp_send(uint32_t dest_ip, uint16_t dest_port, uint16_t src_port,
                 const void *data, size_t len) {
    if (!udp_initialized) {
        kprintf("[UDP] Error: UDP not initialized\n");
        return UDP_ERR_INVALID;
    }

    if (!data && len > 0) {
        return UDP_ERR_INVALID;
    }

    if (len > UDP_MAX_PAYLOAD) {
        kprintf("[UDP] Datagram too large: %zu bytes (max %d)\n",
                len, UDP_MAX_PAYLOAD);
        return UDP_ERR_TOOLARGE;
    }

    /* Calculate total UDP packet size */
    size_t udp_len = UDP_HEADER_LEN + len;

    /* Allocate buffer for UDP packet */
    uint8_t *packet = kmalloc(udp_len);
    if (!packet) {
        return UDP_ERR_NOMEM;
    }

    /* Build UDP header */
    udp_header_t *hdr = (udp_header_t *)packet;
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dest_port);
    hdr->length = htons((uint16_t)udp_len);
    hdr->checksum = 0;  /* Will calculate below */

    /* Copy payload */
    if (len > 0) {
        memcpy(packet + UDP_HEADER_LEN, data, len);
    }

    /* Calculate checksum */
    uint32_t local_ip = ip_get_addr();
    hdr->checksum = udp_checksum(local_ip, dest_ip, packet, udp_len);

    /* If checksum is 0, set to 0xFFFF (per RFC 768) */
    if (hdr->checksum == 0) {
        hdr->checksum = 0xFFFF;
    }

    kprintf("[UDP] Sending %zu bytes to %u.%u.%u.%u:%u from port %u\n",
            len,
            (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
            (dest_ip >> 8) & 0xFF, dest_ip & 0xFF,
            dest_port, src_port);

    /* Send via IP layer */
    int result = ip_send(dest_ip, IP_PROTO_UDP, packet, udp_len);

    kfree(packet);

    if (result < 0) {
        kprintf("[UDP] Failed to send packet: %d\n", result);
        return result;
    }

    /* Update statistics */
    udp_stats.packets_sent++;
    udp_stats.bytes_sent += len;

    return (ssize_t)len;
}

/**
 * Send a UDP datagram using a socket
 */
ssize_t udp_sendto(udp_socket_t *sock, uint32_t dest_ip, uint16_t dest_port,
                   const void *data, size_t len) {
    if (!sock || !sock->bound) {
        return UDP_ERR_NOTBOUND;
    }

    return udp_send(dest_ip, dest_port, sock->local_port, data, len);
}

/**
 * Receive a UDP datagram (non-blocking)
 */
ssize_t udp_recv(uint16_t port, void *buf, size_t max_len,
                 uint32_t *src_ip, uint16_t *src_port) {
    if (!udp_initialized) {
        return UDP_ERR_INVALID;
    }

    udp_socket_t *sock = udp_find_socket(port);
    if (!sock) {
        return UDP_ERR_NOTBOUND;
    }

    return udp_recvfrom(sock, buf, max_len, src_ip, src_port);
}

/**
 * Receive a UDP datagram using a socket (non-blocking)
 */
ssize_t udp_recvfrom(udp_socket_t *sock, void *buf, size_t max_len,
                     uint32_t *src_ip, uint16_t *src_port) {
    if (!sock || !sock->bound) {
        return UDP_ERR_NOTBOUND;
    }

    if (!buf || max_len == 0) {
        return UDP_ERR_INVALID;
    }

    /* Get next datagram from queue */
    udp_datagram_t *dgram = dequeue_datagram(sock);
    if (!dgram) {
        /* No data available */
        return 0;
    }

    /* Copy data to user buffer */
    size_t copy_len = (dgram->len < max_len) ? dgram->len : max_len;
    memcpy(buf, dgram->data, copy_len);

    /* Return source information if requested */
    if (src_ip) {
        *src_ip = dgram->src_ip;
    }
    if (src_port) {
        *src_port = dgram->src_port;
    }

    ssize_t result = (ssize_t)dgram->len;

    /* Free datagram */
    kfree(dgram->data);
    kfree(dgram);

    return result;
}

/**
 * Process incoming UDP packet from IP layer
 */
int udp_input(uint32_t src_ip, const void *packet, size_t len) {
    if (!udp_initialized) {
        kprintf("[UDP] Error: UDP not initialized\n");
        return UDP_ERR_INVALID;
    }

    if (!packet || len < UDP_HEADER_LEN) {
        kprintf("[UDP] Invalid packet: too short (%zu bytes)\n", len);
        udp_stats.invalid_packets++;
        return UDP_ERR_INVALID;
    }

    const udp_header_t *hdr = (const udp_header_t *)packet;

    /* Extract header fields (convert from network byte order) */
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t udp_len = ntohs(hdr->length);
    uint16_t checksum = hdr->checksum;

    /* Validate length */
    if (udp_len < UDP_HEADER_LEN || udp_len > len) {
        kprintf("[UDP] Invalid length: header says %u, got %zu\n", udp_len, len);
        udp_stats.invalid_packets++;
        return UDP_ERR_INVALID;
    }

    /* Validate checksum if present (0 means no checksum) */
    if (checksum != 0) {
        uint32_t local_ip = ip_get_addr();
        uint16_t calc_checksum = udp_checksum(src_ip, local_ip, packet, udp_len);

        /* Checksum should be 0 or 0xFFFF if correct */
        if (calc_checksum != 0 && calc_checksum != 0xFFFF) {
            kprintf("[UDP] Checksum error: expected 0, got 0x%04x\n", calc_checksum);
            udp_stats.checksum_errors++;
            return UDP_ERR_INVALID;
        }
    }

    kprintf("[UDP] Received %u bytes from %u.%u.%u.%u:%u to port %u\n",
            udp_len - UDP_HEADER_LEN,
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >> 8) & 0xFF, src_ip & 0xFF,
            src_port, dst_port);

    /* Find socket for destination port */
    udp_socket_t *sock = udp_find_socket(dst_port);
    if (!sock) {
        kprintf("[UDP] No socket bound to port %u\n", dst_port);
        udp_stats.port_unreachable++;
        /* Could send ICMP Port Unreachable here */
        return UDP_ERR_NOTBOUND;
    }

    /* Queue the datagram payload */
    const uint8_t *payload = (const uint8_t *)packet + UDP_HEADER_LEN;
    size_t payload_len = udp_len - UDP_HEADER_LEN;

    int result = queue_datagram(sock, payload, payload_len, src_ip, src_port);
    if (result != UDP_OK) {
        return result;
    }

    /* Update statistics */
    udp_stats.packets_recv++;
    udp_stats.bytes_recv += payload_len;

    return UDP_OK;
}

/**
 * Get number of queued datagrams for a port
 */
uint32_t udp_recv_queue_count(uint16_t port) {
    udp_socket_t *sock = udp_find_socket(port);
    if (!sock) {
        return 0;
    }
    return sock->recv_count;
}

/**
 * Set socket to non-blocking mode
 */
void udp_set_nonblock(udp_socket_t *sock, bool nonblock) {
    if (!sock) return;

    if (nonblock) {
        sock->flags |= UDP_SOCK_FLAG_NONBLOCK;
    } else {
        sock->flags &= ~UDP_SOCK_FLAG_NONBLOCK;
    }
}

/**
 * Enable/disable broadcast on socket
 */
void udp_set_broadcast(udp_socket_t *sock, bool enable) {
    if (!sock) return;

    if (enable) {
        sock->flags |= UDP_SOCK_FLAG_BROADCAST;
    } else {
        sock->flags &= ~UDP_SOCK_FLAG_BROADCAST;
    }
}

/**
 * Print UDP statistics for debugging
 */
void udp_debug_stats(void) {
    kprintf("[UDP] Statistics:\n");
    kprintf("  Packets sent:      %llu\n", udp_stats.packets_sent);
    kprintf("  Packets received:  %llu\n", udp_stats.packets_recv);
    kprintf("  Bytes sent:        %llu\n", udp_stats.bytes_sent);
    kprintf("  Bytes received:    %llu\n", udp_stats.bytes_recv);
    kprintf("  Checksum errors:   %llu\n", udp_stats.checksum_errors);
    kprintf("  Port unreachable:  %llu\n", udp_stats.port_unreachable);
    kprintf("  Queue full drops:  %llu\n", udp_stats.queue_full);
    kprintf("  Invalid packets:   %llu\n", udp_stats.invalid_packets);
    kprintf("  Active sockets:    %u\n", socket_count);
}
