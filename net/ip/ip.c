/**
 * AAAos Network Stack - IPv4 Implementation
 */

#include "ip.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"
#include "../ethernet/ethernet.h"
#include "../arp/arp.h"
#include "../icmp/icmp.h"

/* Local IP configuration */
static uint32_t local_ip = 0;
static uint32_t local_netmask = 0;
static uint32_t local_gateway = 0;
static bool ip_initialized = false;

/* IP identification counter */
static uint16_t ip_id_counter = 0;

void ip_init(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    local_ip = ip;
    local_netmask = netmask;
    local_gateway = gateway;
    ip_initialized = true;

    kprintf("[IP] Initialized: addr=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u\n",
            ip_octet(ip, 0), ip_octet(ip, 1),
            ip_octet(ip, 2), ip_octet(ip, 3),
            ip_octet(netmask, 0), ip_octet(netmask, 1),
            ip_octet(netmask, 2), ip_octet(netmask, 3),
            ip_octet(gateway, 0), ip_octet(gateway, 1),
            ip_octet(gateway, 2), ip_octet(gateway, 3));
}

uint32_t ip_get_addr(void) {
    return local_ip;
}

void ip_set_addr(uint32_t ip) {
    local_ip = ip;
    kprintf("[IP] Address changed to %u.%u.%u.%u\n",
            ip_octet(ip, 0), ip_octet(ip, 1),
            ip_octet(ip, 2), ip_octet(ip, 3));
}

uint32_t ip_get_netmask(void) {
    return local_netmask;
}

void ip_set_netmask(uint32_t netmask) {
    local_netmask = netmask;
}

uint32_t ip_get_gateway(void) {
    return local_gateway;
}

void ip_set_gateway(uint32_t gateway) {
    local_gateway = gateway;
}

uint16_t ip_checksum(const void *header, size_t len) {
    const uint16_t *data = (const uint16_t *)header;
    uint32_t sum = 0;

    /* Sum all 16-bit words */
    while (len > 1) {
        sum += *data++;
        len -= 2;
    }

    /* Add odd byte if present */
    if (len > 0) {
        sum += *(const uint8_t *)data;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    /* Return one's complement */
    return (uint16_t)~sum;
}

uint16_t ip_checksum_data(const void *data, size_t len) {
    return ip_checksum(data, len);
}

bool ip_is_local(uint32_t ip) {
    /* Check if in same network */
    return (ip & local_netmask) == (local_ip & local_netmask);
}

bool ip_is_broadcast(uint32_t ip) {
    /* Global broadcast */
    if (ip == IP_ADDR_BROADCAST) {
        return true;
    }

    /* Local network broadcast */
    uint32_t broadcast = (local_ip & local_netmask) | ~local_netmask;
    return ip == broadcast;
}

int ip_send(uint32_t dest_ip, uint8_t protocol, const void *data, size_t len) {
    uint8_t packet[IP_MTU_DEFAULT + IP_HEADER_MAX];
    ip_header_t *hdr;
    size_t total_len;
    uint32_t next_hop;
    uint8_t dest_mac[ETH_ALEN];

    if (!ip_initialized) {
        kprintf("[IP] Error: Not initialized\n");
        return -1;
    }

    if (len > IP_MTU_DEFAULT - IP_HEADER_MIN) {
        kprintf("[IP] Error: Payload too large (%u > %u)\n",
                (uint32_t)len, IP_MTU_DEFAULT - IP_HEADER_MIN);
        return -1;
    }

    /* Build IP header */
    hdr = (ip_header_t *)packet;
    memset(hdr, 0, IP_HEADER_MIN);

    hdr->version_ihl = ip_make_version_ihl(IP_VERSION, IP_HEADER_MIN);
    hdr->tos = 0;
    total_len = IP_HEADER_MIN + len;
    hdr->total_length = htons(total_len);
    hdr->identification = htons(ip_id_counter++);
    hdr->flags_fragment = htons(IP_FLAG_DF);  /* Don't fragment */
    hdr->ttl = IP_TTL_DEFAULT;
    hdr->protocol = protocol;
    hdr->checksum = 0;  /* Calculate after filling header */
    hdr->src_addr = htonl(local_ip);
    hdr->dst_addr = htonl(dest_ip);

    /* Calculate header checksum */
    hdr->checksum = ip_checksum(hdr, IP_HEADER_MIN);

    /* Copy payload */
    if (len > 0 && data != NULL) {
        memcpy(packet + IP_HEADER_MIN, data, len);
    }

    kprintf("[IP] TX: %u.%u.%u.%u -> %u.%u.%u.%u proto=%u len=%u\n",
            ip_octet(local_ip, 0), ip_octet(local_ip, 1),
            ip_octet(local_ip, 2), ip_octet(local_ip, 3),
            ip_octet(dest_ip, 0), ip_octet(dest_ip, 1),
            ip_octet(dest_ip, 2), ip_octet(dest_ip, 3),
            protocol, (uint32_t)total_len);

    /* Determine next hop */
    if (ip_is_broadcast(dest_ip)) {
        /* Broadcast - use broadcast MAC */
        uint8_t bcast_mac[ETH_ALEN] = ETH_BROADCAST_MAC;
        return eth_send(bcast_mac, ETH_TYPE_IPV4, packet, total_len);
    } else if (ip_is_local(dest_ip)) {
        /* Local network - send directly */
        next_hop = dest_ip;
    } else {
        /* Remote network - use gateway */
        next_hop = local_gateway;
        if (next_hop == 0) {
            kprintf("[IP] Error: No gateway configured for remote address\n");
            return -1;
        }
    }

    /* Look up MAC address */
    if (arp_lookup(next_hop, dest_mac) != 0) {
        /* ARP not resolved yet - packet will need to be retried */
        kprintf("[IP] Waiting for ARP resolution of %u.%u.%u.%u\n",
                ip_octet(next_hop, 0), ip_octet(next_hop, 1),
                ip_octet(next_hop, 2), ip_octet(next_hop, 3));
        return -1;
    }

    /* Send via Ethernet */
    return eth_send(dest_mac, ETH_TYPE_IPV4, packet, total_len);
}

int ip_send_buf(netbuf_t *buf, uint32_t dest_ip, uint8_t protocol) {
    ip_header_t *hdr;
    size_t total_len;
    uint32_t next_hop;
    uint8_t dest_mac[ETH_ALEN];

    if (!ip_initialized) {
        kprintf("[IP] Error: Not initialized\n");
        return -1;
    }

    if (buf == NULL) {
        kprintf("[IP] Error: NULL buffer\n");
        return -1;
    }

    /* Push space for IP header */
    hdr = (ip_header_t *)netbuf_push(buf, IP_HEADER_MIN);
    if (hdr == NULL) {
        kprintf("[IP] Error: Insufficient headroom for IP header\n");
        return -1;
    }

    /* Build IP header */
    memset(hdr, 0, IP_HEADER_MIN);
    total_len = buf->len;

    hdr->version_ihl = ip_make_version_ihl(IP_VERSION, IP_HEADER_MIN);
    hdr->tos = 0;
    hdr->total_length = htons(total_len);
    hdr->identification = htons(ip_id_counter++);
    hdr->flags_fragment = htons(IP_FLAG_DF);
    hdr->ttl = IP_TTL_DEFAULT;
    hdr->protocol = protocol;
    hdr->checksum = 0;
    hdr->src_addr = htonl(local_ip);
    hdr->dst_addr = htonl(dest_ip);

    /* Calculate header checksum */
    hdr->checksum = ip_checksum(hdr, IP_HEADER_MIN);

    /* Store in buffer metadata */
    buf->src_ip = local_ip;
    buf->dst_ip = dest_ip;
    buf->protocol = protocol;

    kprintf("[IP] TX: %u.%u.%u.%u -> %u.%u.%u.%u proto=%u len=%u\n",
            ip_octet(local_ip, 0), ip_octet(local_ip, 1),
            ip_octet(local_ip, 2), ip_octet(local_ip, 3),
            ip_octet(dest_ip, 0), ip_octet(dest_ip, 1),
            ip_octet(dest_ip, 2), ip_octet(dest_ip, 3),
            protocol, (uint32_t)total_len);

    /* Determine next hop */
    if (ip_is_broadcast(dest_ip)) {
        uint8_t bcast_mac[ETH_ALEN] = ETH_BROADCAST_MAC;
        return eth_send_buf(buf, bcast_mac, ETH_TYPE_IPV4);
    } else if (ip_is_local(dest_ip)) {
        next_hop = dest_ip;
    } else {
        next_hop = local_gateway;
        if (next_hop == 0) {
            kprintf("[IP] Error: No gateway configured for remote address\n");
            return -1;
        }
    }

    /* Look up MAC address */
    if (arp_lookup(next_hop, dest_mac) != 0) {
        kprintf("[IP] Waiting for ARP resolution of %u.%u.%u.%u\n",
                ip_octet(next_hop, 0), ip_octet(next_hop, 1),
                ip_octet(next_hop, 2), ip_octet(next_hop, 3));
        return -1;
    }

    /* Send via Ethernet */
    return eth_send_buf(buf, dest_mac, ETH_TYPE_IPV4);
}

int ip_receive(const void *packet, size_t len) {
    const ip_header_t *hdr;
    uint8_t header_len;
    uint16_t total_len;
    uint32_t src_ip, dst_ip;
    const uint8_t *payload;
    size_t payload_len;

    if (!ip_initialized) {
        kprintf("[IP] Error: Not initialized\n");
        return -1;
    }

    if (packet == NULL || len < IP_HEADER_MIN) {
        kprintf("[IP] Error: Packet too small (%u bytes)\n", (uint32_t)len);
        return -1;
    }

    hdr = (const ip_header_t *)packet;

    /* Validate IP version */
    if (ip_version(hdr) != IP_VERSION) {
        kprintf("[IP] Error: Invalid version %u\n", ip_version(hdr));
        return -1;
    }

    /* Get and validate header length */
    header_len = ip_header_len(hdr);
    if (header_len < IP_HEADER_MIN || header_len > len) {
        kprintf("[IP] Error: Invalid header length %u\n", header_len);
        return -1;
    }

    /* Validate total length */
    total_len = ntohs(hdr->total_length);
    if (total_len < header_len || total_len > len) {
        kprintf("[IP] Error: Invalid total length %u\n", total_len);
        return -1;
    }

    /* Verify checksum */
    if (ip_checksum(hdr, header_len) != 0) {
        kprintf("[IP] Error: Invalid header checksum\n");
        return -1;
    }

    /* Extract addresses */
    src_ip = ntohl(hdr->src_addr);
    dst_ip = ntohl(hdr->dst_addr);

    kprintf("[IP] RX: %u.%u.%u.%u -> %u.%u.%u.%u proto=%u len=%u\n",
            ip_octet(src_ip, 0), ip_octet(src_ip, 1),
            ip_octet(src_ip, 2), ip_octet(src_ip, 3),
            ip_octet(dst_ip, 0), ip_octet(dst_ip, 1),
            ip_octet(dst_ip, 2), ip_octet(dst_ip, 3),
            hdr->protocol, total_len);

    /* Check if packet is for us */
    if (dst_ip != local_ip &&
        !ip_is_broadcast(dst_ip) &&
        !ip_is_multicast(dst_ip)) {
        kprintf("[IP] Packet not for us, dropping\n");
        return 0;
    }

    /* Check TTL */
    if (hdr->ttl == 0) {
        kprintf("[IP] TTL expired\n");
        /* Should send ICMP Time Exceeded */
        return -1;
    }

    /* Get payload */
    payload = (const uint8_t *)packet + header_len;
    payload_len = total_len - header_len;

    /* Dispatch based on protocol */
    switch (hdr->protocol) {
        case IP_PROTO_ICMP:
            return icmp_receive(src_ip, payload, payload_len);

        case IP_PROTO_TCP:
            kprintf("[IP] TCP not yet implemented\n");
            return -1;

        case IP_PROTO_UDP:
            kprintf("[IP] UDP not yet implemented\n");
            return -1;

        default:
            kprintf("[IP] Unknown protocol %u\n", hdr->protocol);
            return -1;
    }
}
