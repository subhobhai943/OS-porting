/**
 * AAAos Network Stack - BSD Sockets API
 *
 * Provides a standard BSD sockets interface for network programming.
 * Supports SOCK_STREAM (TCP), SOCK_DGRAM (UDP), and SOCK_RAW (Raw IP).
 */

#ifndef _AAAOS_NET_SOCKET_H
#define _AAAOS_NET_SOCKET_H

#include "../../kernel/include/types.h"

/* ============================================================================
 * Socket Types and Constants
 * ============================================================================ */

/* Address families */
#define AF_UNSPEC       0       /* Unspecified */
#define AF_INET         2       /* IPv4 Internet protocols */
#define AF_INET6        10      /* IPv6 Internet protocols */

/* Protocol families (same as address families) */
#define PF_UNSPEC       AF_UNSPEC
#define PF_INET         AF_INET
#define PF_INET6        AF_INET6

/* Socket types */
#define SOCK_STREAM     1       /* TCP - Reliable, connection-oriented */
#define SOCK_DGRAM      2       /* UDP - Unreliable, connectionless */
#define SOCK_RAW        3       /* Raw IP - Direct IP packet access */

/* Protocol numbers */
#define IPPROTO_IP      0       /* Dummy protocol for IP */
#define IPPROTO_ICMP    1       /* Internet Control Message Protocol */
#define IPPROTO_TCP     6       /* Transmission Control Protocol */
#define IPPROTO_UDP     17      /* User Datagram Protocol */
#define IPPROTO_RAW     255     /* Raw IP packets */

/* Socket options levels */
#define SOL_SOCKET      1       /* Socket level options */
#define SOL_IP          0       /* IP level options */
#define SOL_TCP         6       /* TCP level options */
#define SOL_UDP         17      /* UDP level options */

/* Socket options (SOL_SOCKET level) */
#define SO_DEBUG        1       /* Enable debugging */
#define SO_REUSEADDR    2       /* Allow local address reuse */
#define SO_TYPE         3       /* Get socket type */
#define SO_ERROR        4       /* Get and clear pending error */
#define SO_DONTROUTE    5       /* Bypass routing */
#define SO_BROADCAST    6       /* Allow broadcast */
#define SO_SNDBUF       7       /* Send buffer size */
#define SO_RCVBUF       8       /* Receive buffer size */
#define SO_KEEPALIVE    9       /* Keep connections alive */
#define SO_LINGER       13      /* Linger on close */
#define SO_RCVTIMEO     20      /* Receive timeout */
#define SO_SNDTIMEO     21      /* Send timeout */

/* Flags for send/recv */
#define MSG_OOB         0x01    /* Out-of-band data */
#define MSG_PEEK        0x02    /* Peek at incoming data */
#define MSG_DONTROUTE   0x04    /* Don't route */
#define MSG_DONTWAIT    0x40    /* Non-blocking operation */
#define MSG_WAITALL     0x100   /* Wait for full request */

/* Shutdown how values */
#define SHUT_RD         0       /* Stop receiving */
#define SHUT_WR         1       /* Stop sending */
#define SHUT_RDWR       2       /* Stop both */

/* Special addresses */
#define INADDR_ANY          0x00000000UL    /* 0.0.0.0 */
#define INADDR_BROADCAST    0xFFFFFFFFUL    /* 255.255.255.255 */
#define INADDR_LOOPBACK     0x7F000001UL    /* 127.0.0.1 */
#define INADDR_NONE         0xFFFFFFFFUL    /* Invalid address */

/* Socket table configuration */
#define SOCKET_MAX_COUNT        256         /* Maximum number of sockets */
#define SOCKET_BACKLOG_MAX      128         /* Maximum listen backlog */
#define SOCKET_BUFFER_SIZE      65536       /* Default socket buffer size */

/* Socket file descriptor base (to distinguish from VFS file descriptors) */
#define SOCKET_FD_BASE          1000

/* ============================================================================
 * Socket Address Structures
 * ============================================================================ */

/* Generic socket address length type */
typedef uint32_t socklen_t;

/* Generic socket address structure */
struct sockaddr {
    uint16_t    sa_family;      /* Address family (AF_INET, etc.) */
    char        sa_data[14];    /* Protocol-specific address data */
} PACKED;

/* IPv4 socket address structure */
struct sockaddr_in {
    uint16_t    sin_family;     /* AF_INET */
    uint16_t    sin_port;       /* Port number (network byte order) */
    uint32_t    sin_addr;       /* IPv4 address (network byte order) */
    uint8_t     sin_zero[8];    /* Padding to match sizeof(struct sockaddr) */
} PACKED;

/* ============================================================================
 * Socket States
 * ============================================================================ */

typedef enum {
    SOCKET_STATE_UNBOUND = 0,   /* Socket created but not bound */
    SOCKET_STATE_BOUND,         /* Socket bound to address */
    SOCKET_STATE_LISTENING,     /* Socket listening for connections (TCP) */
    SOCKET_STATE_CONNECTING,    /* Connection in progress (TCP) */
    SOCKET_STATE_CONNECTED,     /* Socket connected (TCP) */
    SOCKET_STATE_CLOSING,       /* Connection closing */
    SOCKET_STATE_CLOSED         /* Socket closed */
} socket_state_t;

/* ============================================================================
 * Socket Buffer Structure
 * ============================================================================ */

/**
 * Ring buffer for socket data
 */
typedef struct socket_buffer {
    uint8_t     *data;          /* Buffer data */
    size_t      capacity;       /* Total buffer capacity */
    size_t      head;           /* Read position */
    size_t      tail;           /* Write position */
    size_t      count;          /* Bytes currently in buffer */
} socket_buffer_t;

/* ============================================================================
 * Socket Structure
 * ============================================================================ */

/**
 * Socket structure - represents a network endpoint
 */
typedef struct socket {
    /* Socket identity */
    int             fd;             /* Socket file descriptor */
    int             domain;         /* Address family (AF_INET, etc.) */
    int             type;           /* Socket type (SOCK_STREAM, etc.) */
    int             protocol;       /* Protocol (IPPROTO_TCP, etc.) */

    /* Socket state */
    socket_state_t  state;          /* Current socket state */
    int             error;          /* Last error code */
    uint32_t        flags;          /* Socket flags */

    /* Addresses */
    struct sockaddr_in  local_addr;     /* Local address */
    struct sockaddr_in  remote_addr;    /* Remote address (for connected sockets) */
    bool            bound;              /* Socket is bound to local address */

    /* Data buffers */
    socket_buffer_t send_buffer;    /* Outgoing data buffer */
    socket_buffer_t recv_buffer;    /* Incoming data buffer */

    /* TCP-specific fields */
    int             backlog;        /* Listen backlog size */
    struct socket   **accept_queue; /* Queue of pending connections */
    int             accept_queue_len;   /* Current pending connections */
    struct socket   *parent;        /* Parent listening socket */

    /* Options */
    bool            reuse_addr;     /* SO_REUSEADDR */
    bool            broadcast;      /* SO_BROADCAST */
    bool            keepalive;      /* SO_KEEPALIVE */
    uint32_t        send_timeout;   /* Send timeout (ms) */
    uint32_t        recv_timeout;   /* Receive timeout (ms) */

    /* Statistics */
    uint64_t        bytes_sent;     /* Total bytes sent */
    uint64_t        bytes_recv;     /* Total bytes received */
    uint64_t        packets_sent;   /* Total packets sent */
    uint64_t        packets_recv;   /* Total packets received */

    /* Ownership */
    uint32_t        owner_pid;      /* Owning process PID */

    /* Protocol-specific data (TCP control block, etc.) */
    void            *proto_data;

    /* In use flag */
    bool            in_use;
} socket_t;

/* Socket flags */
#define SOCKET_FLAG_NONBLOCK    BIT(0)  /* Non-blocking mode */
#define SOCKET_FLAG_CLOEXEC     BIT(1)  /* Close on exec */

/* ============================================================================
 * BSD Socket API Functions
 * ============================================================================ */

/**
 * Initialize the socket subsystem
 * Call this during network stack initialization
 */
void socket_init(void);

/**
 * Create a socket
 * @param domain Address family (AF_INET)
 * @param type Socket type (SOCK_STREAM, SOCK_DGRAM, SOCK_RAW)
 * @param protocol Protocol (0 for default, or IPPROTO_*)
 * @return Socket file descriptor on success, -1 on error
 */
int socket(int domain, int type, int protocol);

/**
 * Bind socket to a local address
 * @param sockfd Socket file descriptor
 * @param addr Local address to bind to
 * @param addrlen Size of address structure
 * @return 0 on success, -1 on error
 */
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * Listen for incoming connections (TCP)
 * @param sockfd Socket file descriptor
 * @param backlog Maximum pending connection queue length
 * @return 0 on success, -1 on error
 */
int listen(int sockfd, int backlog);

/**
 * Accept an incoming connection (TCP)
 * @param sockfd Listening socket file descriptor
 * @param addr Buffer to receive client address (can be NULL)
 * @param addrlen Size of address buffer (updated with actual size)
 * @return New socket file descriptor on success, -1 on error
 */
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Connect to a remote address (TCP/UDP)
 * @param sockfd Socket file descriptor
 * @param addr Remote address to connect to
 * @param addrlen Size of address structure
 * @return 0 on success, -1 on error
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * Send data on a connected socket
 * @param sockfd Socket file descriptor
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @param flags Send flags (MSG_*)
 * @return Number of bytes sent on success, -1 on error
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags);

/**
 * Receive data from a connected socket
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data into
 * @param len Maximum bytes to receive
 * @param flags Receive flags (MSG_*)
 * @return Number of bytes received on success, -1 on error, 0 if connection closed
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags);

/**
 * Send data to a specific destination (UDP)
 * @param sockfd Socket file descriptor
 * @param buf Data buffer to send
 * @param len Length of data to send
 * @param flags Send flags (MSG_*)
 * @param dest_addr Destination address
 * @param addrlen Size of destination address
 * @return Number of bytes sent on success, -1 on error
 */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * Receive data and source address (UDP)
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data into
 * @param len Maximum bytes to receive
 * @param flags Receive flags (MSG_*)
 * @param src_addr Buffer to receive source address (can be NULL)
 * @param addrlen Size of source address buffer (updated with actual size)
 * @return Number of bytes received on success, -1 on error
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * Close a socket
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int socket_close(int sockfd);

/**
 * Shutdown part of a full-duplex connection
 * @param sockfd Socket file descriptor
 * @param how SHUT_RD, SHUT_WR, or SHUT_RDWR
 * @return 0 on success, -1 on error
 */
int shutdown(int sockfd, int how);

/**
 * Set socket option
 * @param sockfd Socket file descriptor
 * @param level Option level (SOL_SOCKET, etc.)
 * @param optname Option name (SO_*, etc.)
 * @param optval Option value buffer
 * @param optlen Size of option value
 * @return 0 on success, -1 on error
 */
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);

/**
 * Get socket option
 * @param sockfd Socket file descriptor
 * @param level Option level (SOL_SOCKET, etc.)
 * @param optname Option name (SO_*, etc.)
 * @param optval Buffer to receive option value
 * @param optlen Size of buffer (updated with actual size)
 * @return 0 on success, -1 on error
 */
int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen);

/**
 * Get local address of socket
 * @param sockfd Socket file descriptor
 * @param addr Buffer to receive local address
 * @param addrlen Size of buffer (updated with actual size)
 * @return 0 on success, -1 on error
 */
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * Get remote address of connected socket
 * @param sockfd Socket file descriptor
 * @param addr Buffer to receive remote address
 * @param addrlen Size of buffer (updated with actual size)
 * @return 0 on success, -1 on error
 */
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/* ============================================================================
 * Internal Socket Functions
 * ============================================================================ */

/**
 * Get socket structure by file descriptor
 * @param sockfd Socket file descriptor
 * @return Socket pointer or NULL if invalid
 */
socket_t *socket_get(int sockfd);

/**
 * Allocate a new socket
 * @return Socket pointer or NULL on failure
 */
socket_t *socket_alloc(void);

/**
 * Free a socket
 * @param sock Socket to free
 */
void socket_free(socket_t *sock);

/**
 * Deliver received data to socket
 * @param sock Socket to deliver to
 * @param data Data buffer
 * @param len Data length
 * @param src_addr Source address (for UDP)
 * @return Bytes delivered, or -1 on error
 */
ssize_t socket_deliver(socket_t *sock, const void *data, size_t len,
                       const struct sockaddr_in *src_addr);

/**
 * Get socket state as string
 * @param state Socket state
 * @return Static string describing state
 */
const char *socket_state_string(socket_state_t state);

/**
 * Dump socket information for debugging
 * @param sock Socket to dump
 */
void socket_dump(const socket_t *sock);

/**
 * Dump all sockets for debugging
 */
void socket_dump_all(void);

/* ============================================================================
 * Byte Order Conversion (Network byte order is big-endian)
 * ============================================================================ */

/**
 * Convert 16-bit value from host to network byte order
 */
static inline uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0x00FF) << 8) | ((hostshort & 0xFF00) >> 8);
}

/**
 * Convert 16-bit value from network to host byte order
 */
static inline uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);  /* Same operation for little-endian host */
}

/**
 * Convert 32-bit value from host to network byte order
 */
static inline uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0x000000FF) << 24) |
           ((hostlong & 0x0000FF00) << 8)  |
           ((hostlong & 0x00FF0000) >> 8)  |
           ((hostlong & 0xFF000000) >> 24);
}

/**
 * Convert 32-bit value from network to host byte order
 */
static inline uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);  /* Same operation for little-endian host */
}

/* ============================================================================
 * Address Manipulation Functions
 * ============================================================================ */

/**
 * Convert IPv4 address from dotted-decimal string to binary
 * @param cp Address string (e.g., "192.168.1.1")
 * @param addr Buffer to receive binary address
 * @return 1 on success, 0 on invalid format
 */
int inet_aton(const char *cp, uint32_t *addr);

/**
 * Convert IPv4 address from binary to dotted-decimal string
 * @param addr Binary address (network byte order)
 * @param buf Buffer to receive string (at least 16 bytes)
 * @return Pointer to buf
 */
char *inet_ntoa(uint32_t addr, char *buf);

/**
 * Convert IPv4 address string to binary (network byte order)
 * @param cp Address string
 * @return Binary address, or INADDR_NONE on error
 */
uint32_t inet_addr(const char *cp);

/* ============================================================================
 * Error Codes
 * ============================================================================ */

/* Socket error codes (subset of POSIX errno values) */
#define ENOTSOCK        88      /* Socket operation on non-socket */
#define EDESTADDRREQ    89      /* Destination address required */
#define EMSGSIZE        90      /* Message too long */
#define EPROTOTYPE      91      /* Protocol wrong type for socket */
#define ENOPROTOOPT     92      /* Protocol not available */
#define EPROTONOSUPPORT 93      /* Protocol not supported */
#define ESOCKTNOSUPPORT 94      /* Socket type not supported */
#define EOPNOTSUPP      95      /* Operation not supported */
#define EPFNOSUPPORT    96      /* Protocol family not supported */
#define EAFNOSUPPORT    97      /* Address family not supported */
#define EADDRINUSE      98      /* Address already in use */
#define EADDRNOTAVAIL   99      /* Cannot assign requested address */
#define ENETDOWN        100     /* Network is down */
#define ENETUNREACH     101     /* Network is unreachable */
#define ENETRESET       102     /* Network dropped connection on reset */
#define ECONNABORTED    103     /* Software caused connection abort */
#define ECONNRESET      104     /* Connection reset by peer */
#define ENOBUFS         105     /* No buffer space available */
#define EISCONN         106     /* Transport endpoint is already connected */
#define ENOTCONN        107     /* Transport endpoint is not connected */
#define ESHUTDOWN       108     /* Cannot send after shutdown */
#define ETIMEDOUT       110     /* Connection timed out */
#define ECONNREFUSED    111     /* Connection refused */
#define EHOSTDOWN       112     /* Host is down */
#define EHOSTUNREACH    113     /* No route to host */
#define EALREADY        114     /* Operation already in progress */
#define EINPROGRESS     115     /* Operation now in progress */
#define EINVAL          22      /* Invalid argument */
#define ENOMEM          12      /* Out of memory */
#define EBADF           9       /* Bad file descriptor */
#define EAGAIN          11      /* Try again */
#define EWOULDBLOCK     EAGAIN  /* Operation would block */

/* Global socket errno */
extern int socket_errno;

/**
 * Get last socket error
 * @return Last error code
 */
int socket_get_errno(void);

/**
 * Set socket error
 * @param err Error code
 */
void socket_set_errno(int err);

/**
 * Get error string for error code
 * @param err Error code
 * @return Static string describing error
 */
const char *socket_strerror(int err);

#endif /* _AAAOS_NET_SOCKET_H */
