/**
 * AAAos Network Stack - BSD Sockets API Implementation
 *
 * Provides a standard BSD sockets interface for network programming.
 * Maps socket operations to the underlying TCP/UDP protocol implementations.
 */

#include "socket.h"
#include "../tcp/tcp.h"
#include "../ip/ip.h"
#include "../core/netbuf.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Socket table - array of all sockets */
static socket_t socket_table[SOCKET_MAX_COUNT];

/* Next file descriptor to assign */
static int next_socket_fd = SOCKET_FD_BASE;

/* Global socket errno */
int socket_errno = 0;

/* Initialization flag */
static bool socket_initialized = false;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Convert socket fd to table index
 */
static int fd_to_index(int sockfd) {
    int index = sockfd - SOCKET_FD_BASE;
    if (index < 0 || index >= SOCKET_MAX_COUNT) {
        return -1;
    }
    return index;
}

/**
 * Allocate a socket buffer
 */
static int socket_buffer_init(socket_buffer_t *buf, size_t capacity) {
    buf->data = (uint8_t *)kmalloc(capacity);
    if (!buf->data) {
        return -1;
    }
    buf->capacity = capacity;
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
    return 0;
}

/**
 * Free a socket buffer
 */
static void socket_buffer_free(socket_buffer_t *buf) {
    if (buf->data) {
        kfree(buf->data);
        buf->data = NULL;
    }
    buf->capacity = 0;
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

/**
 * Write data to socket buffer (ring buffer)
 */
static ssize_t socket_buffer_write(socket_buffer_t *buf, const void *data, size_t len) {
    if (!buf->data || len == 0) {
        return 0;
    }

    const uint8_t *src = (const uint8_t *)data;
    size_t written = 0;
    size_t available = buf->capacity - buf->count;

    if (len > available) {
        len = available;
    }

    while (written < len) {
        buf->data[buf->tail] = src[written];
        buf->tail = (buf->tail + 1) % buf->capacity;
        buf->count++;
        written++;
    }

    return (ssize_t)written;
}

/**
 * Read data from socket buffer (ring buffer)
 */
static ssize_t socket_buffer_read(socket_buffer_t *buf, void *data, size_t len, bool peek) {
    if (!buf->data || buf->count == 0 || len == 0) {
        return 0;
    }

    uint8_t *dst = (uint8_t *)data;
    size_t to_read = (len < buf->count) ? len : buf->count;
    size_t read_count = 0;
    size_t head = buf->head;

    while (read_count < to_read) {
        dst[read_count] = buf->data[head];
        head = (head + 1) % buf->capacity;
        read_count++;
    }

    if (!peek) {
        buf->head = head;
        buf->count -= read_count;
    }

    return (ssize_t)read_count;
}

/**
 * Check if port is already in use
 */
static bool port_in_use(uint16_t port, int type) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (socket_table[i].in_use &&
            socket_table[i].bound &&
            socket_table[i].type == type &&
            ntohs(socket_table[i].local_addr.sin_port) == port) {
            return true;
        }
    }
    return false;
}

/**
 * Allocate an ephemeral port
 */
static uint16_t allocate_ephemeral_port(int type) {
    static uint16_t next_ephemeral = 49152;  /* Start of ephemeral port range */

    for (int attempts = 0; attempts < 16384; attempts++) {
        uint16_t port = next_ephemeral++;
        if (next_ephemeral > 65535) {
            next_ephemeral = 49152;
        }
        if (!port_in_use(port, type)) {
            return port;
        }
    }
    return 0;  /* No ports available */
}

/**
 * Find socket by local address (for incoming packets)
 */
static socket_t *find_socket_by_local(uint16_t port, int type) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (socket_table[i].in_use &&
            socket_table[i].type == type &&
            socket_table[i].bound &&
            socket_table[i].local_addr.sin_port == htons(port)) {
            return &socket_table[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * Socket Initialization
 * ============================================================================ */

void socket_init(void) {
    if (socket_initialized) {
        return;
    }

    kprintf("[SOCKET] Initializing BSD socket layer\n");

    /* Clear socket table */
    memset(socket_table, 0, sizeof(socket_table));

    /* Mark all sockets as not in use */
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        socket_table[i].in_use = false;
        socket_table[i].fd = -1;
    }

    next_socket_fd = SOCKET_FD_BASE;
    socket_errno = 0;
    socket_initialized = true;

    kprintf("[SOCKET] Socket layer initialized (max %d sockets)\n", SOCKET_MAX_COUNT);
}

/* ============================================================================
 * Socket Allocation/Deallocation
 * ============================================================================ */

socket_t *socket_alloc(void) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (!socket_table[i].in_use) {
            socket_t *sock = &socket_table[i];
            memset(sock, 0, sizeof(socket_t));
            sock->in_use = true;
            sock->fd = next_socket_fd++;
            sock->state = SOCKET_STATE_UNBOUND;
            return sock;
        }
    }
    return NULL;
}

void socket_free(socket_t *sock) {
    if (!sock) {
        return;
    }

    /* Free buffers */
    socket_buffer_free(&sock->send_buffer);
    socket_buffer_free(&sock->recv_buffer);

    /* Free accept queue if present */
    if (sock->accept_queue) {
        kfree(sock->accept_queue);
        sock->accept_queue = NULL;
    }

    /* Free protocol-specific data */
    if (sock->proto_data) {
        if (sock->type == SOCK_STREAM) {
            tcp_socket_destroy((tcp_socket_t *)sock->proto_data);
        }
        sock->proto_data = NULL;
    }

    /* Mark as free */
    sock->in_use = false;
    sock->fd = -1;
}

socket_t *socket_get(int sockfd) {
    int index = fd_to_index(sockfd);
    if (index < 0) {
        return NULL;
    }

    socket_t *sock = &socket_table[index];
    if (!sock->in_use) {
        return NULL;
    }

    return sock;
}

/* ============================================================================
 * BSD Socket API Implementation
 * ============================================================================ */

int socket(int domain, int type, int protocol) {
    /* Validate domain */
    if (domain != AF_INET) {
        kprintf("[SOCKET] socket: unsupported domain %d\n", domain);
        socket_set_errno(EAFNOSUPPORT);
        return -1;
    }

    /* Validate type */
    if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) {
        kprintf("[SOCKET] socket: unsupported type %d\n", type);
        socket_set_errno(ESOCKTNOSUPPORT);
        return -1;
    }

    /* Auto-select protocol if not specified */
    if (protocol == 0) {
        if (type == SOCK_STREAM) {
            protocol = IPPROTO_TCP;
        } else if (type == SOCK_DGRAM) {
            protocol = IPPROTO_UDP;
        } else if (type == SOCK_RAW) {
            protocol = IPPROTO_RAW;
        }
    }

    /* Validate protocol matches type */
    if (type == SOCK_STREAM && protocol != IPPROTO_TCP) {
        socket_set_errno(EPROTONOSUPPORT);
        return -1;
    }
    if (type == SOCK_DGRAM && protocol != IPPROTO_UDP) {
        socket_set_errno(EPROTONOSUPPORT);
        return -1;
    }

    /* Allocate socket structure */
    socket_t *sock = socket_alloc();
    if (!sock) {
        kprintf("[SOCKET] socket: no free sockets\n");
        socket_set_errno(ENOMEM);
        return -1;
    }

    /* Initialize socket */
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_UNBOUND;
    sock->error = 0;
    sock->flags = 0;
    sock->bound = false;

    /* Initialize buffers */
    if (socket_buffer_init(&sock->send_buffer, SOCKET_BUFFER_SIZE) < 0) {
        socket_free(sock);
        socket_set_errno(ENOMEM);
        return -1;
    }
    if (socket_buffer_init(&sock->recv_buffer, SOCKET_BUFFER_SIZE) < 0) {
        socket_free(sock);
        socket_set_errno(ENOMEM);
        return -1;
    }

    /* Create protocol-specific data */
    if (type == SOCK_STREAM) {
        tcp_socket_t *tcp_sock = tcp_socket_create();
        if (!tcp_sock) {
            socket_free(sock);
            socket_set_errno(ENOMEM);
            return -1;
        }
        sock->proto_data = tcp_sock;
    }
    /* UDP doesn't need a separate control block for this simple implementation */

    kprintf("[SOCKET] Created socket fd=%d type=%d protocol=%d\n",
            sock->fd, type, protocol);

    return sock->fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (addrlen < sizeof(struct sockaddr_in)) {
        socket_set_errno(EINVAL);
        return -1;
    }

    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) {
        socket_set_errno(EAFNOSUPPORT);
        return -1;
    }

    uint16_t port = ntohs(sin->sin_port);

    /* Check if already bound */
    if (sock->bound) {
        socket_set_errno(EINVAL);
        return -1;
    }

    /* Check if port is in use (unless SO_REUSEADDR is set) */
    if (port != 0 && !sock->reuse_addr && port_in_use(port, sock->type)) {
        kprintf("[SOCKET] bind: port %d already in use\n", port);
        socket_set_errno(EADDRINUSE);
        return -1;
    }

    /* If port is 0, allocate ephemeral port */
    if (port == 0) {
        port = allocate_ephemeral_port(sock->type);
        if (port == 0) {
            socket_set_errno(EADDRINUSE);
            return -1;
        }
    }

    /* Store local address */
    memcpy(&sock->local_addr, sin, sizeof(struct sockaddr_in));
    sock->local_addr.sin_port = htons(port);
    sock->bound = true;
    sock->state = SOCKET_STATE_BOUND;

    /* Bind TCP socket if applicable */
    if (sock->type == SOCK_STREAM && sock->proto_data) {
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        int ret = tcp_bind(tcp_sock, port);
        if (ret != TCP_OK) {
            sock->bound = false;
            sock->state = SOCKET_STATE_UNBOUND;
            socket_set_errno(EADDRINUSE);
            return -1;
        }
    }

    kprintf("[SOCKET] Bound socket fd=%d to port %d\n", sockfd, port);
    return 0;
}

int listen(int sockfd, int backlog) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    /* Only TCP supports listen */
    if (sock->type != SOCK_STREAM) {
        socket_set_errno(EOPNOTSUPP);
        return -1;
    }

    /* Must be bound first */
    if (!sock->bound) {
        socket_set_errno(EINVAL);
        return -1;
    }

    /* Clamp backlog */
    if (backlog <= 0) {
        backlog = 1;
    }
    if (backlog > SOCKET_BACKLOG_MAX) {
        backlog = SOCKET_BACKLOG_MAX;
    }

    /* Allocate accept queue */
    sock->accept_queue = (socket_t **)kmalloc(sizeof(socket_t *) * backlog);
    if (!sock->accept_queue) {
        socket_set_errno(ENOMEM);
        return -1;
    }
    memset(sock->accept_queue, 0, sizeof(socket_t *) * backlog);
    sock->backlog = backlog;
    sock->accept_queue_len = 0;

    /* Set TCP to listen mode */
    if (sock->proto_data) {
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        int ret = tcp_listen(tcp_sock, backlog);
        if (ret != TCP_OK) {
            kfree(sock->accept_queue);
            sock->accept_queue = NULL;
            socket_set_errno(EINVAL);
            return -1;
        }
    }

    sock->state = SOCKET_STATE_LISTENING;

    kprintf("[SOCKET] Socket fd=%d listening with backlog %d\n", sockfd, backlog);
    return 0;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    /* Only listening sockets can accept */
    if (sock->state != SOCKET_STATE_LISTENING) {
        socket_set_errno(EINVAL);
        return -1;
    }

    /* Try to accept from TCP layer */
    tcp_socket_t *tcp_listen = (tcp_socket_t *)sock->proto_data;
    tcp_socket_t *tcp_client = tcp_accept(tcp_listen);

    if (!tcp_client) {
        /* No pending connections - would block */
        if (sock->flags & SOCKET_FLAG_NONBLOCK) {
            socket_set_errno(EWOULDBLOCK);
            return -1;
        }
        /* In blocking mode, this would wait - for now return error */
        socket_set_errno(EAGAIN);
        return -1;
    }

    /* Create new socket for the accepted connection */
    socket_t *new_sock = socket_alloc();
    if (!new_sock) {
        tcp_socket_destroy(tcp_client);
        socket_set_errno(ENOMEM);
        return -1;
    }

    /* Initialize new socket */
    new_sock->domain = AF_INET;
    new_sock->type = SOCK_STREAM;
    new_sock->protocol = IPPROTO_TCP;
    new_sock->state = SOCKET_STATE_CONNECTED;
    new_sock->bound = true;
    new_sock->proto_data = tcp_client;
    new_sock->parent = sock;

    /* Set up addresses */
    new_sock->local_addr.sin_family = AF_INET;
    new_sock->local_addr.sin_port = htons(tcp_client->local_port);
    new_sock->local_addr.sin_addr = htonl(tcp_client->local_ip);

    new_sock->remote_addr.sin_family = AF_INET;
    new_sock->remote_addr.sin_port = htons(tcp_client->remote_port);
    new_sock->remote_addr.sin_addr = htonl(tcp_client->remote_ip);

    /* Initialize buffers */
    if (socket_buffer_init(&new_sock->send_buffer, SOCKET_BUFFER_SIZE) < 0 ||
        socket_buffer_init(&new_sock->recv_buffer, SOCKET_BUFFER_SIZE) < 0) {
        socket_free(new_sock);
        socket_set_errno(ENOMEM);
        return -1;
    }

    /* Return client address if requested */
    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        memcpy(addr, &new_sock->remote_addr, sizeof(struct sockaddr_in));
        *addrlen = sizeof(struct sockaddr_in);
    }

    kprintf("[SOCKET] Accepted connection on fd=%d, new fd=%d\n", sockfd, new_sock->fd);
    return new_sock->fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (addrlen < sizeof(struct sockaddr_in)) {
        socket_set_errno(EINVAL);
        return -1;
    }

    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) {
        socket_set_errno(EAFNOSUPPORT);
        return -1;
    }

    /* Check if already connected */
    if (sock->state == SOCKET_STATE_CONNECTED) {
        socket_set_errno(EISCONN);
        return -1;
    }

    /* Auto-bind if not bound */
    if (!sock->bound) {
        uint16_t ephemeral = allocate_ephemeral_port(sock->type);
        if (ephemeral == 0) {
            socket_set_errno(EADDRINUSE);
            return -1;
        }
        sock->local_addr.sin_family = AF_INET;
        sock->local_addr.sin_port = htons(ephemeral);
        sock->local_addr.sin_addr = htonl(ip_get_addr());
        sock->bound = true;
    }

    /* Store remote address */
    memcpy(&sock->remote_addr, sin, sizeof(struct sockaddr_in));

    if (sock->type == SOCK_STREAM) {
        /* TCP connect */
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        if (!tcp_sock) {
            socket_set_errno(EINVAL);
            return -1;
        }

        /* Bind TCP socket if needed */
        if (!(tcp_sock->flags & TCP_SOCK_FLAG_BOUND)) {
            tcp_bind(tcp_sock, ntohs(sock->local_addr.sin_port));
        }

        uint32_t remote_ip = ntohl(sin->sin_addr);
        uint16_t remote_port = ntohs(sin->sin_port);

        sock->state = SOCKET_STATE_CONNECTING;
        int ret = tcp_connect(tcp_sock, remote_ip, remote_port);

        if (ret != TCP_OK) {
            sock->state = SOCKET_STATE_UNBOUND;
            if (ret == TCP_ERR_TIMEOUT) {
                socket_set_errno(ETIMEDOUT);
            } else if (ret == TCP_ERR_CONNREFUSED) {
                socket_set_errno(ECONNREFUSED);
            } else {
                socket_set_errno(ECONNREFUSED);
            }
            return -1;
        }

        sock->state = SOCKET_STATE_CONNECTED;
        kprintf("[SOCKET] Connected fd=%d to %08x:%d\n", sockfd, remote_ip, remote_port);
    } else if (sock->type == SOCK_DGRAM) {
        /* UDP "connect" just sets the default destination */
        sock->state = SOCKET_STATE_CONNECTED;
        kprintf("[SOCKET] UDP socket fd=%d connected to %08x:%d\n",
                sockfd, ntohl(sin->sin_addr), ntohs(sin->sin_port));
    }

    return 0;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    /* Must be connected for send() */
    if (sock->state != SOCKET_STATE_CONNECTED) {
        socket_set_errno(ENOTCONN);
        return -1;
    }

    ssize_t sent = 0;

    if (sock->type == SOCK_STREAM) {
        /* TCP send */
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        if (!tcp_sock) {
            socket_set_errno(ENOTCONN);
            return -1;
        }

        sent = tcp_send(tcp_sock, buf, len);
        if (sent < 0) {
            if (sent == TCP_ERR_WOULDBLOCK) {
                socket_set_errno(EWOULDBLOCK);
            } else if (sent == TCP_ERR_NOTCONN) {
                socket_set_errno(ENOTCONN);
            } else {
                socket_set_errno(ECONNRESET);
            }
            return -1;
        }
    } else if (sock->type == SOCK_DGRAM) {
        /* UDP send - use sendto with stored remote address */
        return sendto(sockfd, buf, len, flags,
                      (struct sockaddr *)&sock->remote_addr,
                      sizeof(sock->remote_addr));
    }

    sock->bytes_sent += sent;
    sock->packets_sent++;
    return sent;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    /* Must be connected for recv() */
    if (sock->state != SOCKET_STATE_CONNECTED && sock->type == SOCK_STREAM) {
        socket_set_errno(ENOTCONN);
        return -1;
    }

    ssize_t received = 0;
    bool peek = (flags & MSG_PEEK) != 0;

    if (sock->type == SOCK_STREAM) {
        /* TCP receive */
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        if (!tcp_sock) {
            socket_set_errno(ENOTCONN);
            return -1;
        }

        /* First check our local buffer */
        received = socket_buffer_read(&sock->recv_buffer, buf, len, peek);

        if (received == 0) {
            /* Try to get data from TCP layer */
            received = tcp_recv(tcp_sock, buf, len);
            if (received < 0) {
                if (received == TCP_ERR_WOULDBLOCK) {
                    socket_set_errno(EWOULDBLOCK);
                    return -1;
                } else if (received == TCP_ERR_CLOSED) {
                    return 0;  /* Connection closed */
                } else {
                    socket_set_errno(ECONNRESET);
                    return -1;
                }
            }
        }
    } else if (sock->type == SOCK_DGRAM) {
        /* UDP receive - use recvfrom */
        return recvfrom(sockfd, buf, len, flags, NULL, NULL);
    }

    if (received > 0) {
        sock->bytes_recv += received;
        sock->packets_recv++;
    }
    return received;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    UNUSED(flags);

    if (!buf || len == 0) {
        return 0;
    }

    const struct sockaddr_in *dest;

    if (dest_addr) {
        if (addrlen < sizeof(struct sockaddr_in)) {
            socket_set_errno(EINVAL);
            return -1;
        }
        dest = (const struct sockaddr_in *)dest_addr;
    } else if (sock->state == SOCKET_STATE_CONNECTED) {
        dest = &sock->remote_addr;
    } else {
        socket_set_errno(EDESTADDRREQ);
        return -1;
    }

    /* Auto-bind if not bound */
    if (!sock->bound) {
        uint16_t ephemeral = allocate_ephemeral_port(sock->type);
        if (ephemeral == 0) {
            socket_set_errno(EADDRINUSE);
            return -1;
        }
        sock->local_addr.sin_family = AF_INET;
        sock->local_addr.sin_port = htons(ephemeral);
        sock->local_addr.sin_addr = htonl(ip_get_addr());
        sock->bound = true;
        sock->state = SOCKET_STATE_BOUND;
    }

    if (sock->type == SOCK_DGRAM) {
        /* Build and send UDP packet */
        /* UDP header: src_port(2) + dst_port(2) + length(2) + checksum(2) = 8 bytes */
        size_t udp_len = 8 + len;
        uint8_t *packet = (uint8_t *)kmalloc(udp_len);
        if (!packet) {
            socket_set_errno(ENOMEM);
            return -1;
        }

        /* UDP header */
        uint16_t *hdr = (uint16_t *)packet;
        hdr[0] = sock->local_addr.sin_port;           /* Source port (already network order) */
        hdr[1] = dest->sin_port;                       /* Dest port (already network order) */
        hdr[2] = htons((uint16_t)udp_len);            /* Length */
        hdr[3] = 0;                                    /* Checksum (optional for IPv4) */

        /* Copy payload */
        memcpy(packet + 8, buf, len);

        /* Send via IP layer */
        uint32_t dest_ip = ntohl(dest->sin_addr);
        int ret = ip_send(dest_ip, IP_PROTO_UDP, packet, udp_len);
        kfree(packet);

        if (ret < 0) {
            socket_set_errno(ENETUNREACH);
            return -1;
        }

        sock->bytes_sent += len;
        sock->packets_sent++;
        return (ssize_t)len;
    } else if (sock->type == SOCK_STREAM) {
        /* TCP doesn't use sendto, redirect to send */
        return send(sockfd, buf, len, flags);
    }

    socket_set_errno(EOPNOTSUPP);
    return -1;
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    bool peek = (flags & MSG_PEEK) != 0;
    ssize_t received = 0;

    if (sock->type == SOCK_DGRAM) {
        /* Read from receive buffer */
        received = socket_buffer_read(&sock->recv_buffer, buf, len, peek);

        if (received == 0) {
            /* No data available */
            if (sock->flags & SOCKET_FLAG_NONBLOCK) {
                socket_set_errno(EWOULDBLOCK);
                return -1;
            }
            /* Would block - return 0 for now */
            socket_set_errno(EAGAIN);
            return -1;
        }

        /* Return source address if requested */
        if (src_addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
            /* Note: For a full implementation, we'd store per-packet source info */
            memcpy(src_addr, &sock->remote_addr, sizeof(struct sockaddr_in));
            *addrlen = sizeof(struct sockaddr_in);
        }
    } else if (sock->type == SOCK_STREAM) {
        /* TCP doesn't really use recvfrom, redirect to recv */
        received = recv(sockfd, buf, len, flags);
        if (received > 0 && src_addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
            memcpy(src_addr, &sock->remote_addr, sizeof(struct sockaddr_in));
            *addrlen = sizeof(struct sockaddr_in);
        }
        return received;
    }

    if (received > 0) {
        sock->bytes_recv += received;
        sock->packets_recv++;
    }
    return received;
}

int socket_close(int sockfd) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    kprintf("[SOCKET] Closing socket fd=%d\n", sockfd);

    /* Close protocol-specific resources */
    if (sock->type == SOCK_STREAM && sock->proto_data) {
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        tcp_close(tcp_sock);
    }

    sock->state = SOCKET_STATE_CLOSED;
    socket_free(sock);
    return 0;
}

int shutdown(int sockfd, int how) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        socket_set_errno(EINVAL);
        return -1;
    }

    if (sock->type != SOCK_STREAM) {
        socket_set_errno(EOPNOTSUPP);
        return -1;
    }

    kprintf("[SOCKET] Shutdown socket fd=%d how=%d\n", sockfd, how);

    /* For TCP, initiate graceful close */
    if (sock->proto_data && (how == SHUT_WR || how == SHUT_RDWR)) {
        tcp_socket_t *tcp_sock = (tcp_socket_t *)sock->proto_data;
        tcp_close(tcp_sock);
    }

    if (how == SHUT_RDWR) {
        sock->state = SOCKET_STATE_CLOSING;
    }

    return 0;
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!optval) {
        socket_set_errno(EINVAL);
        return -1;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
            case SO_REUSEADDR:
                if (optlen >= sizeof(int)) {
                    sock->reuse_addr = (*(const int *)optval != 0);
                    kprintf("[SOCKET] fd=%d SO_REUSEADDR=%d\n", sockfd, sock->reuse_addr);
                    return 0;
                }
                break;

            case SO_BROADCAST:
                if (optlen >= sizeof(int)) {
                    sock->broadcast = (*(const int *)optval != 0);
                    kprintf("[SOCKET] fd=%d SO_BROADCAST=%d\n", sockfd, sock->broadcast);
                    return 0;
                }
                break;

            case SO_KEEPALIVE:
                if (optlen >= sizeof(int)) {
                    sock->keepalive = (*(const int *)optval != 0);
                    kprintf("[SOCKET] fd=%d SO_KEEPALIVE=%d\n", sockfd, sock->keepalive);
                    return 0;
                }
                break;

            case SO_RCVTIMEO:
                if (optlen >= sizeof(uint32_t)) {
                    sock->recv_timeout = *(const uint32_t *)optval;
                    return 0;
                }
                break;

            case SO_SNDTIMEO:
                if (optlen >= sizeof(uint32_t)) {
                    sock->send_timeout = *(const uint32_t *)optval;
                    return 0;
                }
                break;

            case SO_SNDBUF:
                /* Buffer size changes not supported after creation */
                return 0;

            case SO_RCVBUF:
                /* Buffer size changes not supported after creation */
                return 0;

            default:
                kprintf("[SOCKET] setsockopt: unknown option %d\n", optname);
                socket_set_errno(ENOPROTOOPT);
                return -1;
        }
    } else if (level == SOL_TCP && sock->type == SOCK_STREAM) {
        /* TCP-specific options could go here */
        socket_set_errno(ENOPROTOOPT);
        return -1;
    }

    socket_set_errno(EINVAL);
    return -1;
}

int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!optval || !optlen) {
        socket_set_errno(EINVAL);
        return -1;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
            case SO_TYPE:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->type;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_ERROR:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->error;
                    sock->error = 0;  /* Clear error after reading */
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_REUSEADDR:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->reuse_addr ? 1 : 0;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_BROADCAST:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->broadcast ? 1 : 0;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_KEEPALIVE:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->keepalive ? 1 : 0;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_SNDBUF:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = (int)sock->send_buffer.capacity;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            case SO_RCVBUF:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = (int)sock->recv_buffer.capacity;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;

            default:
                socket_set_errno(ENOPROTOOPT);
                return -1;
        }
    }

    socket_set_errno(EINVAL);
    return -1;
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!addr || !addrlen) {
        socket_set_errno(EINVAL);
        return -1;
    }

    if (*addrlen < sizeof(struct sockaddr_in)) {
        socket_set_errno(EINVAL);
        return -1;
    }

    memcpy(addr, &sock->local_addr, sizeof(struct sockaddr_in));
    *addrlen = sizeof(struct sockaddr_in);
    return 0;
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        socket_set_errno(EBADF);
        return -1;
    }

    if (!addr || !addrlen) {
        socket_set_errno(EINVAL);
        return -1;
    }

    if (sock->state != SOCKET_STATE_CONNECTED) {
        socket_set_errno(ENOTCONN);
        return -1;
    }

    if (*addrlen < sizeof(struct sockaddr_in)) {
        socket_set_errno(EINVAL);
        return -1;
    }

    memcpy(addr, &sock->remote_addr, sizeof(struct sockaddr_in));
    *addrlen = sizeof(struct sockaddr_in);
    return 0;
}

/* ============================================================================
 * Data Delivery (called by protocol layers)
 * ============================================================================ */

ssize_t socket_deliver(socket_t *sock, const void *data, size_t len,
                       const struct sockaddr_in *src_addr) {
    if (!sock || !data || len == 0) {
        return -1;
    }

    /* Store source address for UDP */
    if (src_addr && sock->type == SOCK_DGRAM) {
        memcpy(&sock->remote_addr, src_addr, sizeof(struct sockaddr_in));
    }

    /* Write to receive buffer */
    ssize_t written = socket_buffer_write(&sock->recv_buffer, data, len);
    if (written > 0) {
        sock->bytes_recv += written;
        sock->packets_recv++;
    }

    return written;
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

int socket_get_errno(void) {
    return socket_errno;
}

void socket_set_errno(int err) {
    socket_errno = err;
}

const char *socket_strerror(int err) {
    switch (err) {
        case 0:              return "Success";
        case EBADF:          return "Bad file descriptor";
        case EINVAL:         return "Invalid argument";
        case ENOMEM:         return "Out of memory";
        case EAGAIN:         return "Resource temporarily unavailable";
        case ENOTSOCK:       return "Socket operation on non-socket";
        case EDESTADDRREQ:   return "Destination address required";
        case EMSGSIZE:       return "Message too long";
        case EPROTOTYPE:     return "Protocol wrong type for socket";
        case ENOPROTOOPT:    return "Protocol not available";
        case EPROTONOSUPPORT:return "Protocol not supported";
        case ESOCKTNOSUPPORT:return "Socket type not supported";
        case EOPNOTSUPP:     return "Operation not supported";
        case EPFNOSUPPORT:   return "Protocol family not supported";
        case EAFNOSUPPORT:   return "Address family not supported";
        case EADDRINUSE:     return "Address already in use";
        case EADDRNOTAVAIL:  return "Cannot assign requested address";
        case ENETDOWN:       return "Network is down";
        case ENETUNREACH:    return "Network is unreachable";
        case ENETRESET:      return "Network dropped connection on reset";
        case ECONNABORTED:   return "Software caused connection abort";
        case ECONNRESET:     return "Connection reset by peer";
        case ENOBUFS:        return "No buffer space available";
        case EISCONN:        return "Transport endpoint is already connected";
        case ENOTCONN:       return "Transport endpoint is not connected";
        case ESHUTDOWN:      return "Cannot send after shutdown";
        case ETIMEDOUT:      return "Connection timed out";
        case ECONNREFUSED:   return "Connection refused";
        case EHOSTDOWN:      return "Host is down";
        case EHOSTUNREACH:   return "No route to host";
        case EALREADY:       return "Operation already in progress";
        case EINPROGRESS:    return "Operation now in progress";
        default:             return "Unknown error";
    }
}

/* ============================================================================
 * Debug/Utility Functions
 * ============================================================================ */

const char *socket_state_string(socket_state_t state) {
    switch (state) {
        case SOCKET_STATE_UNBOUND:    return "UNBOUND";
        case SOCKET_STATE_BOUND:      return "BOUND";
        case SOCKET_STATE_LISTENING:  return "LISTENING";
        case SOCKET_STATE_CONNECTING: return "CONNECTING";
        case SOCKET_STATE_CONNECTED:  return "CONNECTED";
        case SOCKET_STATE_CLOSING:    return "CLOSING";
        case SOCKET_STATE_CLOSED:     return "CLOSED";
        default:                      return "UNKNOWN";
    }
}

void socket_dump(const socket_t *sock) {
    if (!sock) {
        kprintf("[SOCKET] socket_dump: NULL socket\n");
        return;
    }

    char local_ip[16], remote_ip[16];
    inet_ntoa(sock->local_addr.sin_addr, local_ip);
    inet_ntoa(sock->remote_addr.sin_addr, remote_ip);

    kprintf("[SOCKET] === Socket fd=%d ===\n", sock->fd);
    kprintf("  Type: %s, Protocol: %d\n",
            sock->type == SOCK_STREAM ? "STREAM" :
            sock->type == SOCK_DGRAM ? "DGRAM" : "RAW",
            sock->protocol);
    kprintf("  State: %s\n", socket_state_string(sock->state));
    kprintf("  Local:  %s:%d\n", local_ip, ntohs(sock->local_addr.sin_port));
    kprintf("  Remote: %s:%d\n", remote_ip, ntohs(sock->remote_addr.sin_port));
    kprintf("  Bytes sent: %llu, recv: %llu\n", sock->bytes_sent, sock->bytes_recv);
    kprintf("  Send buffer: %zu/%zu bytes\n", sock->send_buffer.count, sock->send_buffer.capacity);
    kprintf("  Recv buffer: %zu/%zu bytes\n", sock->recv_buffer.count, sock->recv_buffer.capacity);
}

void socket_dump_all(void) {
    kprintf("[SOCKET] === All Active Sockets ===\n");
    int count = 0;
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (socket_table[i].in_use) {
            socket_dump(&socket_table[i]);
            count++;
        }
    }
    kprintf("[SOCKET] Total active sockets: %d\n", count);
}

/* ============================================================================
 * Address Manipulation Functions
 * ============================================================================ */

int inet_aton(const char *cp, uint32_t *addr) {
    if (!cp || !addr) {
        return 0;
    }

    uint32_t result = 0;
    uint32_t octet = 0;
    int dots = 0;
    int digits = 0;

    while (*cp) {
        if (*cp >= '0' && *cp <= '9') {
            octet = octet * 10 + (*cp - '0');
            if (octet > 255) {
                return 0;
            }
            digits++;
        } else if (*cp == '.') {
            if (digits == 0 || dots >= 3) {
                return 0;
            }
            result = (result << 8) | octet;
            octet = 0;
            digits = 0;
            dots++;
        } else {
            return 0;
        }
        cp++;
    }

    if (digits == 0 || dots != 3) {
        return 0;
    }

    result = (result << 8) | octet;
    *addr = htonl(result);
    return 1;
}

char *inet_ntoa(uint32_t addr, char *buf) {
    if (!buf) {
        return NULL;
    }

    uint32_t host_addr = ntohl(addr);
    uint8_t *bytes = (uint8_t *)&host_addr;

    /* Format: a.b.c.d (big-endian after ntohl) */
    int pos = 0;
    for (int i = 3; i >= 0; i--) {
        uint8_t b = bytes[i];
        if (b >= 100) {
            buf[pos++] = '0' + (b / 100);
            b %= 100;
            buf[pos++] = '0' + (b / 10);
            buf[pos++] = '0' + (b % 10);
        } else if (b >= 10) {
            buf[pos++] = '0' + (b / 10);
            buf[pos++] = '0' + (b % 10);
        } else {
            buf[pos++] = '0' + b;
        }
        if (i > 0) {
            buf[pos++] = '.';
        }
    }
    buf[pos] = '\0';
    return buf;
}

uint32_t inet_addr(const char *cp) {
    uint32_t addr;
    if (inet_aton(cp, &addr)) {
        return addr;
    }
    return INADDR_NONE;
}
