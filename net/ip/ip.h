/**
 * AAAos Network Stack - Internet Protocol Version 4 (IPv4)
 *
 * Implements RFC 791 IPv4 packet handling.
 */

#ifndef _AAAOS_NET_IP_H
#define _AAAOS_NET_IP_H

#include "../../kernel/include/types.h"
#include "../core/netbuf.h"

/* IP constants */
#define IP_VERSION          4           /* IPv4 */
#define IP_HEADER_MIN       20          /* Minimum header size */
#define IP_HEADER_MAX       60          /* Maximum header size (with options) */
#define IP_MTU_DEFAULT      1500        /* Default MTU */
#define IP_TTL_DEFAULT      64          /* Default time-to-live */

/* IP protocol numbers */
#define IP_PROTO_ICMP       1           /* Internet Control Message Protocol */
#define IP_PROTO_TCP        6           /* Transmission Control Protocol */
#define IP_PROTO_UDP        17          /* User Datagram Protocol */

/* IP header flags */
#define IP_FLAG_DF          0x4000      /* Don't Fragment */
#define IP_FLAG_MF          0x2000      /* More Fragments */
#define IP_FRAG_OFFSET_MASK 0x1FFF      /* Fragment offset mask */

/* Special IP addresses */
#define IP_ADDR_ANY         0x00000000  /* 0.0.0.0 */
#define IP_ADDR_BROADCAST   0xFFFFFFFF  /* 255.255.255.255 */
#define IP_ADDR_LOOPBACK    0x7F000001  /* 127.0.0.1 */

/**
 * IPv4 header (20 bytes minimum, up to 60 with options)
 */
typedef struct PACKED ip_header {
    uint8_t  version_ihl;       /* Version (4 bits) + IHL (4 bits) */
    uint8_t  tos;               /* Type of service */
    uint16_t total_length;      /* Total packet length */
    uint16_t identification;    /* Identification */
    uint16_t flags_fragment;    /* Flags (3 bits) + Fragment offset (13 bits) */
    uint8_t  ttl;               /* Time to live */
    uint8_t  protocol;          /* Upper layer protocol */
    uint16_t checksum;          /* Header checksum */
    uint32_t src_addr;          /* Source IP address */
    uint32_t dst_addr;          /* Destination IP address */
    /* Options may follow */
} ip_header_t;

/**
 * Initialize the IP layer
 * @param local_ip Our IP address (host byte order)
 * @param netmask Network mask (host byte order)
 * @param gateway Default gateway IP (host byte order)
 */
void ip_init(uint32_t local_ip, uint32_t netmask, uint32_t gateway);

/**
 * Get our local IP address
 * @return IP address in host byte order
 */
uint32_t ip_get_addr(void);

/**
 * Set our local IP address
 * @param ip New IP address (host byte order)
 */
void ip_set_addr(uint32_t ip);

/**
 * Get network mask
 * @return Netmask in host byte order
 */
uint32_t ip_get_netmask(void);

/**
 * Set network mask
 * @param netmask New netmask (host byte order)
 */
void ip_set_netmask(uint32_t netmask);

/**
 * Get default gateway
 * @return Gateway IP in host byte order
 */
uint32_t ip_get_gateway(void);

/**
 * Set default gateway
 * @param gateway New gateway IP (host byte order)
 */
void ip_set_gateway(uint32_t gateway);

/**
 * Send an IP packet
 * @param dest_ip Destination IP address (host byte order)
 * @param protocol Upper layer protocol (e.g., IP_PROTO_ICMP)
 * @param data Payload data
 * @param len Payload length
 * @return 0 on success, negative on error
 */
int ip_send(uint32_t dest_ip, uint8_t protocol, const void *data, size_t len);

/**
 * Send an IP packet using a netbuf
 * The netbuf should have room for IP + Ethernet headers
 * @param buf Network buffer with payload
 * @param dest_ip Destination IP address (host byte order)
 * @param protocol Upper layer protocol
 * @return 0 on success, negative on error
 */
int ip_send_buf(netbuf_t *buf, uint32_t dest_ip, uint8_t protocol);

/**
 * Process a received IP packet
 * @param packet IP packet data (without Ethernet header)
 * @param len Packet length
 * @return 0 on success, negative on error
 */
int ip_receive(const void *packet, size_t len);

/**
 * Calculate IP header checksum
 * @param header Pointer to IP header
 * @param len Header length in bytes
 * @return Checksum value
 */
uint16_t ip_checksum(const void *header, size_t len);

/**
 * Calculate checksum over a buffer (for ICMP, TCP, UDP)
 * @param data Data to checksum
 * @param len Length in bytes
 * @return Checksum value
 */
uint16_t ip_checksum_data(const void *data, size_t len);

/**
 * Check if an IP address is in our local network
 * @param ip IP address to check (host byte order)
 * @return true if local, false if needs routing
 */
bool ip_is_local(uint32_t ip);

/**
 * Check if an IP address is a broadcast address
 * @param ip IP address to check (host byte order)
 * @return true if broadcast
 */
bool ip_is_broadcast(uint32_t ip);

/**
 * Check if an IP address is multicast
 * @param ip IP address to check (host byte order)
 * @return true if multicast (224.0.0.0 - 239.255.255.255)
 */
static inline bool ip_is_multicast(uint32_t ip) {
    return (ip & 0xF0000000) == 0xE0000000;
}

/**
 * Get header length from version_ihl field
 */
static inline uint8_t ip_header_len(const ip_header_t *hdr) {
    return (hdr->version_ihl & 0x0F) * 4;
}

/**
 * Get IP version from version_ihl field
 */
static inline uint8_t ip_version(const ip_header_t *hdr) {
    return (hdr->version_ihl >> 4) & 0x0F;
}

/**
 * Create version_ihl field
 */
static inline uint8_t ip_make_version_ihl(uint8_t version, uint8_t ihl) {
    return ((version & 0x0F) << 4) | ((ihl / 4) & 0x0F);
}

#endif /* _AAAOS_NET_IP_H */
