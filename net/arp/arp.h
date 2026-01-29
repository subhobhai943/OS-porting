/**
 * AAAos Network Stack - Address Resolution Protocol (ARP)
 *
 * Implements RFC 826 ARP for IPv4 over Ethernet.
 * Maintains a cache mapping IP addresses to MAC addresses.
 */

#ifndef _AAAOS_NET_ARP_H
#define _AAAOS_NET_ARP_H

#include "../../kernel/include/types.h"
#include "../ethernet/ethernet.h"

/* ARP constants */
#define ARP_HRD_ETHERNET    1           /* Hardware type: Ethernet */
#define ARP_PRO_IPV4        0x0800      /* Protocol type: IPv4 */
#define ARP_HLEN            6           /* Hardware address length */
#define ARP_PLEN            4           /* Protocol address length */

/* ARP operation codes */
#define ARP_OP_REQUEST      1           /* ARP request */
#define ARP_OP_REPLY        2           /* ARP reply */

/* ARP cache constants */
#define ARP_CACHE_SIZE      64          /* Maximum cache entries */
#define ARP_CACHE_TIMEOUT   300         /* Entry timeout in seconds */
#define ARP_REQUEST_RETRIES 3           /* Number of request retries */
#define ARP_REQUEST_TIMEOUT 1           /* Timeout between retries (seconds) */

/**
 * ARP packet header (Ethernet + IPv4)
 */
typedef struct PACKED arp_packet {
    uint16_t hrd;                       /* Hardware type */
    uint16_t pro;                       /* Protocol type */
    uint8_t  hln;                       /* Hardware address length */
    uint8_t  pln;                       /* Protocol address length */
    uint16_t op;                        /* Operation */
    uint8_t  sha[ETH_ALEN];             /* Sender hardware address */
    uint32_t spa;                       /* Sender protocol address */
    uint8_t  tha[ETH_ALEN];             /* Target hardware address */
    uint32_t tpa;                       /* Target protocol address */
} arp_packet_t;

/* ARP cache entry states */
typedef enum arp_state {
    ARP_STATE_FREE = 0,                 /* Entry is unused */
    ARP_STATE_PENDING,                  /* Waiting for ARP reply */
    ARP_STATE_RESOLVED,                 /* MAC address known */
    ARP_STATE_STALE                     /* Entry needs refresh */
} arp_state_t;

/**
 * ARP cache entry
 */
typedef struct arp_entry {
    uint32_t    ip;                     /* IP address */
    uint8_t     mac[ETH_ALEN];          /* MAC address */
    arp_state_t state;                  /* Entry state */
    uint32_t    timestamp;              /* Last update time */
    uint8_t     retries;                /* Remaining retries for pending */
} arp_entry_t;

/**
 * Initialize the ARP subsystem
 * Clears the ARP cache and prepares for operation
 */
void arp_init(void);

/**
 * Look up MAC address for an IP address
 * @param ip IP address to look up (host byte order)
 * @param mac_out Buffer to store MAC address (6 bytes)
 * @return 0 if found, -1 if not in cache (sends ARP request)
 */
int arp_lookup(uint32_t ip, uint8_t mac_out[ETH_ALEN]);

/**
 * Send an ARP request for an IP address
 * @param ip Target IP address (host byte order)
 * @return 0 on success, negative on error
 */
int arp_request(uint32_t ip);

/**
 * Send an ARP reply
 * @param dest_ip Destination IP address
 * @param dest_mac Destination MAC address
 * @return 0 on success, negative on error
 */
int arp_reply(uint32_t dest_ip, const uint8_t dest_mac[ETH_ALEN]);

/**
 * Process a received ARP packet
 * @param packet ARP packet data (without Ethernet header)
 * @param len Packet length
 * @return 0 on success, negative on error
 */
int arp_receive(const void *packet, size_t len);

/**
 * Add or update an ARP cache entry
 * @param ip IP address (host byte order)
 * @param mac MAC address
 * @return 0 on success, negative on error
 */
int arp_cache_add(uint32_t ip, const uint8_t mac[ETH_ALEN]);

/**
 * Remove an entry from the ARP cache
 * @param ip IP address to remove
 */
void arp_cache_remove(uint32_t ip);

/**
 * Clear all entries from the ARP cache
 */
void arp_cache_clear(void);

/**
 * Print the ARP cache contents (for debugging)
 */
void arp_cache_dump(void);

/**
 * Perform periodic ARP cache maintenance
 * Call this function periodically (e.g., once per second)
 * to expire old entries and retry pending requests
 */
void arp_timer_tick(void);

/**
 * Convert IP address to string format
 * @param ip IP address (host byte order)
 * @param buf Buffer for string (at least 16 bytes)
 * @return Pointer to buf
 */
char *ip_to_string(uint32_t ip, char *buf);

/**
 * Convert string to IP address
 * @param str IP string (e.g., "192.168.1.1")
 * @param ip_out Output IP address (host byte order)
 * @return 0 on success, -1 on parse error
 */
int string_to_ip(const char *str, uint32_t *ip_out);

/**
 * Create IP address from octets
 * @return IP address in host byte order
 */
static inline uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)c << 8) | (uint32_t)d;
}

/**
 * Get octet from IP address
 */
static inline uint8_t ip_octet(uint32_t ip, int n) {
    return (ip >> (24 - n * 8)) & 0xFF;
}

#endif /* _AAAOS_NET_ARP_H */
