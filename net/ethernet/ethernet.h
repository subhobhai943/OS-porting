/**
 * AAAos Network Stack - Ethernet Frame Handling
 *
 * Implements IEEE 802.3 Ethernet II frame processing.
 */

#ifndef _AAAOS_NET_ETHERNET_H
#define _AAAOS_NET_ETHERNET_H

#include "../../kernel/include/types.h"
#include "../core/netbuf.h"

/* Ethernet constants */
#define ETH_ALEN            6           /* MAC address length */
#define ETH_HLEN            14          /* Ethernet header length */
#define ETH_DATA_MIN        46          /* Minimum payload */
#define ETH_DATA_MAX        1500        /* Maximum payload (MTU) */
#define ETH_FRAME_MIN       64          /* Minimum frame size */
#define ETH_FRAME_MAX       1518        /* Maximum frame size */
#define ETH_FCS_LEN         4           /* Frame check sequence length */

/* EtherType values */
#define ETH_TYPE_IPV4       0x0800      /* Internet Protocol v4 */
#define ETH_TYPE_ARP        0x0806      /* Address Resolution Protocol */
#define ETH_TYPE_VLAN       0x8100      /* 802.1Q VLAN tag */
#define ETH_TYPE_IPV6       0x86DD      /* Internet Protocol v6 */

/* Special MAC addresses */
#define ETH_BROADCAST_MAC   {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

/**
 * Ethernet frame header
 */
typedef struct PACKED eth_header {
    uint8_t  dest_mac[ETH_ALEN];    /* Destination MAC address */
    uint8_t  src_mac[ETH_ALEN];     /* Source MAC address */
    uint16_t ethertype;              /* EtherType (big-endian) */
} eth_header_t;

/**
 * Initialize the Ethernet layer
 * Sets the local MAC address and initializes state
 * @param mac Local MAC address (6 bytes)
 */
void eth_init(const uint8_t mac[ETH_ALEN]);

/**
 * Get our local MAC address
 * @param mac_out Buffer to store MAC (6 bytes)
 */
void eth_get_mac(uint8_t mac_out[ETH_ALEN]);

/**
 * Set our local MAC address
 * @param mac New MAC address (6 bytes)
 */
void eth_set_mac(const uint8_t mac[ETH_ALEN]);

/**
 * Send an Ethernet frame
 * @param dest_mac Destination MAC address
 * @param ethertype Protocol type (e.g., ETH_TYPE_IPV4)
 * @param data Payload data
 * @param len Payload length (must be <= ETH_DATA_MAX)
 * @return 0 on success, negative on error
 */
int eth_send(const uint8_t dest_mac[ETH_ALEN], uint16_t ethertype,
             const void *data, size_t len);

/**
 * Send an Ethernet frame using a netbuf
 * The netbuf must have room for the Ethernet header (14 bytes headroom)
 * @param buf Network buffer with payload
 * @param dest_mac Destination MAC address
 * @param ethertype Protocol type
 * @return 0 on success, negative on error
 */
int eth_send_buf(netbuf_t *buf, const uint8_t dest_mac[ETH_ALEN],
                 uint16_t ethertype);

/**
 * Process a received Ethernet frame
 * @param packet Raw frame data
 * @param len Frame length
 * @return 0 on success, negative on error
 */
int eth_receive(const void *packet, size_t len);

/**
 * Check if a MAC address is broadcast
 * @param mac MAC address to check
 * @return true if broadcast
 */
static inline bool eth_is_broadcast(const uint8_t mac[ETH_ALEN]) {
    return (mac[0] & mac[1] & mac[2] & mac[3] & mac[4] & mac[5]) == 0xFF;
}

/**
 * Check if a MAC address is multicast
 * @param mac MAC address to check
 * @return true if multicast (bit 0 of first byte is set)
 */
static inline bool eth_is_multicast(const uint8_t mac[ETH_ALEN]) {
    return (mac[0] & 0x01) != 0;
}

/**
 * Compare two MAC addresses
 * @param mac1 First MAC address
 * @param mac2 Second MAC address
 * @return true if equal
 */
static inline bool eth_mac_equal(const uint8_t mac1[ETH_ALEN],
                                  const uint8_t mac2[ETH_ALEN]) {
    return (mac1[0] == mac2[0] && mac1[1] == mac2[1] &&
            mac1[2] == mac2[2] && mac1[3] == mac2[3] &&
            mac1[4] == mac2[4] && mac1[5] == mac2[5]);
}

/**
 * Copy a MAC address
 * @param dest Destination buffer
 * @param src Source MAC address
 */
static inline void eth_mac_copy(uint8_t dest[ETH_ALEN],
                                 const uint8_t src[ETH_ALEN]) {
    dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2];
    dest[3] = src[3]; dest[4] = src[4]; dest[5] = src[5];
}

/**
 * Convert 16-bit value from host to network byte order (big-endian)
 */
static inline uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

/**
 * Convert 16-bit value from network to host byte order
 */
static inline uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);  /* Same operation */
}

/**
 * Convert 32-bit value from host to network byte order (big-endian)
 */
static inline uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong >> 8) & 0xFF00) |
           ((hostlong >> 24) & 0xFF);
}

/**
 * Convert 32-bit value from network to host byte order
 */
static inline uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);  /* Same operation */
}

/* Hardware driver callback - must be implemented by NIC driver */
extern int eth_hw_send(const void *frame, size_t len);

#endif /* _AAAOS_NET_ETHERNET_H */
