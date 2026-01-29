/**
 * AAAos Network Stack - DHCP Client Implementation
 *
 * Implements RFC 2131 Dynamic Host Configuration Protocol client.
 * Features:
 *   - Automatic IP address configuration
 *   - Lease management and renewal
 *   - Network parameter discovery (gateway, DNS, etc.)
 */

#ifndef _AAAOS_NET_DHCP_H
#define _AAAOS_NET_DHCP_H

#include "../../kernel/include/types.h"

/* DHCP Protocol Constants */
#define DHCP_SERVER_PORT        67          /* DHCP server port */
#define DHCP_CLIENT_PORT        68          /* DHCP client port */

/* DHCP Packet Constants */
#define DHCP_PACKET_MIN_SIZE    236         /* Minimum DHCP packet size (without options) */
#define DHCP_PACKET_MAX_SIZE    576         /* Maximum DHCP packet size for safety */
#define DHCP_OPTIONS_MAX_SIZE   340         /* Maximum options area size */
#define DHCP_CHADDR_LEN         16          /* Client hardware address length */
#define DHCP_SNAME_LEN          64          /* Server name field length */
#define DHCP_FILE_LEN           128         /* Boot file name length */

/* DHCP Operation Codes (op field) */
#define DHCP_OP_REQUEST         1           /* Client to server (BOOTREQUEST) */
#define DHCP_OP_REPLY           2           /* Server to client (BOOTREPLY) */

/* Hardware Types (htype field) */
#define DHCP_HTYPE_ETHERNET     1           /* Ethernet (10Mb) */
#define DHCP_HTYPE_IEEE802      6           /* IEEE 802 networks */

/* Hardware Address Length */
#define DHCP_HLEN_ETHERNET      6           /* Ethernet MAC address length */

/* DHCP Magic Cookie (options start marker) */
#define DHCP_MAGIC_COOKIE       0x63825363  /* Magic cookie value */

/* DHCP Flags */
#define DHCP_FLAG_BROADCAST     0x8000      /* Broadcast flag */

/* DHCP Message Types (option 53 values) */
#define DHCP_MSG_DISCOVER       1           /* Client broadcast to find servers */
#define DHCP_MSG_OFFER          2           /* Server response to DISCOVER */
#define DHCP_MSG_REQUEST        3           /* Client request for offered IP */
#define DHCP_MSG_DECLINE        4           /* Client declines offered IP */
#define DHCP_MSG_ACK            5           /* Server acknowledges request */
#define DHCP_MSG_NAK            6           /* Server negative acknowledge */
#define DHCP_MSG_RELEASE        7           /* Client releases IP address */
#define DHCP_MSG_INFORM         8           /* Client requests local config only */

/* DHCP Options */
#define DHCP_OPT_PAD            0           /* Padding (1 byte) */
#define DHCP_OPT_SUBNET_MASK    1           /* Subnet mask (4 bytes) */
#define DHCP_OPT_ROUTER         3           /* Default gateway(s) */
#define DHCP_OPT_DNS_SERVER     6           /* DNS server(s) */
#define DHCP_OPT_HOSTNAME       12          /* Hostname */
#define DHCP_OPT_DOMAIN_NAME    15          /* Domain name */
#define DHCP_OPT_BROADCAST      28          /* Broadcast address */
#define DHCP_OPT_REQUESTED_IP   50          /* Requested IP address */
#define DHCP_OPT_LEASE_TIME     51          /* IP address lease time */
#define DHCP_OPT_MSG_TYPE       53          /* DHCP message type */
#define DHCP_OPT_SERVER_ID      54          /* DHCP server identifier */
#define DHCP_OPT_PARAM_REQUEST  55          /* Parameter request list */
#define DHCP_OPT_RENEWAL_TIME   58          /* Renewal (T1) time */
#define DHCP_OPT_REBINDING_TIME 59          /* Rebinding (T2) time */
#define DHCP_OPT_CLIENT_ID      61          /* Client identifier */
#define DHCP_OPT_END            255         /* End of options marker */

/* DHCP Client States */
typedef enum dhcp_state {
    DHCP_STATE_INIT = 0,        /* Initial state, not configured */
    DHCP_STATE_SELECTING,       /* Sent DISCOVER, waiting for OFFER */
    DHCP_STATE_REQUESTING,      /* Sent REQUEST, waiting for ACK */
    DHCP_STATE_BOUND,           /* Lease acquired, IP configured */
    DHCP_STATE_RENEWING,        /* Renewing lease (unicast to server) */
    DHCP_STATE_REBINDING,       /* Rebinding (broadcast REQUEST) */
    DHCP_STATE_REBOOTING,       /* Rebooting, trying previous IP */
    DHCP_STATE_INIT_REBOOT      /* Init-reboot state */
} dhcp_state_t;

/* DHCP Error Codes */
#define DHCP_OK                 0           /* Success */
#define DHCP_ERR_NOMEM          -1          /* Out of memory */
#define DHCP_ERR_INVALID        -2          /* Invalid argument/packet */
#define DHCP_ERR_TIMEOUT        -3          /* Operation timed out */
#define DHCP_ERR_NAK            -4          /* Server sent NAK */
#define DHCP_ERR_NETWORK        -5          /* Network error */
#define DHCP_ERR_NO_LEASE       -6          /* No valid lease */
#define DHCP_ERR_NOT_INIT       -7          /* DHCP not initialized */

/* Timing Constants (in seconds) */
#define DHCP_DISCOVER_TIMEOUT   4           /* Initial DISCOVER timeout */
#define DHCP_REQUEST_TIMEOUT    4           /* Initial REQUEST timeout */
#define DHCP_MAX_RETRIES        4           /* Maximum retry count */
#define DHCP_DEFAULT_LEASE      86400       /* Default lease time (24 hours) */

/**
 * DHCP Packet Structure (236 bytes minimum)
 *
 * Wire format:
 *   +--------+--------+--------+--------+
 *   |   op   |  htype |  hlen  |  hops  |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |                xid                |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |  secs  |        flags             |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |              ciaddr               |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |              yiaddr               |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |              siaddr               |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |              giaddr               |   (4 bytes)
 *   +--------+--------+--------+--------+
 *   |              chaddr               |   (16 bytes)
 *   +--------+--------+--------+--------+
 *   |              sname                |   (64 bytes)
 *   +--------+--------+--------+--------+
 *   |               file                |   (128 bytes)
 *   +--------+--------+--------+--------+
 *   |   options (variable length)       |
 *   +--------+--------+--------+--------+
 */
typedef struct PACKED dhcp_packet {
    uint8_t  op;                            /* Message opcode: 1=request, 2=reply */
    uint8_t  htype;                         /* Hardware type: 1=Ethernet */
    uint8_t  hlen;                          /* Hardware address length: 6 for Ethernet */
    uint8_t  hops;                          /* Hops (set to 0 by client) */
    uint32_t xid;                           /* Transaction ID */
    uint16_t secs;                          /* Seconds since client started trying */
    uint16_t flags;                         /* Flags (bit 15 = broadcast) */
    uint32_t ciaddr;                        /* Client IP address (if known) */
    uint32_t yiaddr;                        /* "Your" IP address (offered by server) */
    uint32_t siaddr;                        /* Server IP address */
    uint32_t giaddr;                        /* Gateway IP address (relay agent) */
    uint8_t  chaddr[DHCP_CHADDR_LEN];       /* Client hardware address (MAC) */
    char     sname[DHCP_SNAME_LEN];         /* Server hostname (optional) */
    char     file[DHCP_FILE_LEN];           /* Boot file name (optional) */
    uint8_t  options[DHCP_OPTIONS_MAX_SIZE]; /* Options (variable, starts with magic cookie) */
} dhcp_packet_t;

/**
 * DHCP Lease Information
 *
 * Stores the current network configuration obtained via DHCP
 */
typedef struct dhcp_lease {
    uint32_t ip_addr;           /* Assigned IP address */
    uint32_t subnet_mask;       /* Subnet mask */
    uint32_t gateway;           /* Default gateway */
    uint32_t dns_server;        /* Primary DNS server */
    uint32_t dns_server2;       /* Secondary DNS server (optional) */
    uint32_t broadcast;         /* Broadcast address */
    uint32_t server_ip;         /* DHCP server IP */
    uint32_t lease_time;        /* Lease duration in seconds */
    uint32_t renewal_time;      /* T1 renewal time (default: lease_time/2) */
    uint32_t rebind_time;       /* T2 rebinding time (default: lease_time*0.875) */
    uint64_t obtained_time;     /* Timestamp when lease was obtained (ticks) */
    bool     valid;             /* Lease is valid */
} dhcp_lease_t;

/**
 * DHCP Client State
 */
typedef struct dhcp_client {
    dhcp_state_t state;         /* Current DHCP state */
    dhcp_lease_t lease;         /* Current lease information */
    uint32_t     xid;           /* Current transaction ID */
    uint8_t      mac[6];        /* Our MAC address */
    uint8_t      retries;       /* Current retry count */
    uint64_t     timeout_time;  /* When current operation times out */
    bool         initialized;   /* Client is initialized */
} dhcp_client_t;

/*
 * DHCP Client API Functions
 */

/**
 * Initialize the DHCP client
 *
 * Initializes the DHCP client state and prepares for network configuration.
 * Must be called before any other DHCP functions.
 *
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_init(void);

/**
 * Start DHCP discovery process
 *
 * Sends a DHCP DISCOVER broadcast to find available DHCP servers.
 * This begins the DHCP address acquisition process.
 *
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_discover(void);

/**
 * Send DHCP REQUEST for an offered IP address
 *
 * Called after receiving a DHCP OFFER to request the offered IP.
 * The offered_ip and server_ip should come from the OFFER packet.
 *
 * @param offered_ip IP address offered by server (network byte order)
 * @param server_ip  DHCP server IP address (network byte order)
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_request(uint32_t offered_ip, uint32_t server_ip);

/**
 * Release the current DHCP lease
 *
 * Sends a DHCP RELEASE to the server and clears local configuration.
 * Should be called before shutting down network interface.
 *
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_release(void);

/**
 * Renew the current DHCP lease
 *
 * Sends a DHCP REQUEST to extend the current lease.
 * Typically called when T1 (renewal) time is reached.
 *
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_renew(void);

/**
 * Get current DHCP lease information
 *
 * Returns a pointer to the current lease information structure.
 * Returns NULL if no valid lease exists.
 *
 * @return Pointer to dhcp_lease_t or NULL if no valid lease
 */
const dhcp_lease_t *dhcp_get_lease(void);

/**
 * Process incoming DHCP packet
 *
 * Called by the UDP layer when a packet is received on port 68.
 * Handles OFFER, ACK, and NAK messages.
 *
 * @param packet Pointer to received DHCP packet data
 * @param len    Length of the packet in bytes
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_input(const void *packet, size_t len);

/**
 * Get current DHCP client state
 *
 * @return Current DHCP state
 */
dhcp_state_t dhcp_get_state(void);

/**
 * Get DHCP state as string (for debugging)
 *
 * @param state DHCP state value
 * @return Static string describing the state
 */
const char *dhcp_state_string(dhcp_state_t state);

/**
 * DHCP timer tick handler
 *
 * Should be called periodically to handle timeouts, retransmissions,
 * and lease renewal. Call approximately once per second.
 */
void dhcp_timer_tick(void);

/**
 * Check if network is configured via DHCP
 *
 * @return true if we have a valid lease and IP is configured
 */
bool dhcp_is_configured(void);

/**
 * Perform full DHCP configuration (blocking)
 *
 * Performs complete DHCP negotiation: DISCOVER -> OFFER -> REQUEST -> ACK
 * This is a blocking function that waits for the full exchange.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_configure(uint32_t timeout_ms);

/*
 * Internal/Helper Functions (used by implementation)
 */

/**
 * Build a DHCP DISCOVER packet
 *
 * @param packet Buffer to store the packet (must be at least DHCP_PACKET_MIN_SIZE + options)
 * @return Size of the packet in bytes, or negative error code
 */
int dhcp_build_discover(dhcp_packet_t *packet);

/**
 * Build a DHCP REQUEST packet
 *
 * @param packet     Buffer to store the packet
 * @param offered_ip IP address being requested (network byte order)
 * @param server_ip  DHCP server IP address (network byte order)
 * @return Size of the packet in bytes, or negative error code
 */
int dhcp_build_request(dhcp_packet_t *packet, uint32_t offered_ip, uint32_t server_ip);

/**
 * Build a DHCP RELEASE packet
 *
 * @param packet Buffer to store the packet
 * @return Size of the packet in bytes, or negative error code
 */
int dhcp_build_release(dhcp_packet_t *packet);

/**
 * Parse DHCP options from a packet
 *
 * @param options    Pointer to options area (after magic cookie)
 * @param len        Length of options area
 * @param lease_out  Structure to populate with parsed options
 * @param msg_type   Pointer to store message type (can be NULL)
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_parse_options(const uint8_t *options, size_t len,
                       dhcp_lease_t *lease_out, uint8_t *msg_type);

/**
 * Apply DHCP lease to network interface
 *
 * Configures the network interface with the IP address, subnet mask,
 * gateway, and DNS server from the lease.
 *
 * @param lease Lease information to apply
 * @return DHCP_OK on success, negative error code on failure
 */
int dhcp_apply_lease(const dhcp_lease_t *lease);

/**
 * Generate a random transaction ID
 *
 * @return Random 32-bit transaction ID
 */
uint32_t dhcp_generate_xid(void);

/**
 * Debug: Dump DHCP packet contents
 *
 * @param packet Packet to dump
 * @param len    Packet length
 */
void dhcp_dump_packet(const dhcp_packet_t *packet, size_t len);

#endif /* _AAAOS_NET_DHCP_H */
