/**
 * AAAos Network Stack - Ethernet Frame Implementation
 */

#include "ethernet.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"
#include "../arp/arp.h"
#include "../ip/ip.h"

/* Local MAC address */
static uint8_t local_mac[ETH_ALEN] = {0};
static bool eth_initialized = false;

/* Weak symbol for hardware send - allows linking without driver */
__attribute__((weak))
int eth_hw_send(const void *frame, size_t len) {
    UNUSED(frame);
    UNUSED(len);
    kprintf("[ETH] Warning: No hardware driver, packet dropped\n");
    return -1;
}

void eth_init(const uint8_t mac[ETH_ALEN]) {
    eth_mac_copy(local_mac, mac);
    eth_initialized = true;

    kprintf("[ETH] Initialized with MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void eth_get_mac(uint8_t mac_out[ETH_ALEN]) {
    eth_mac_copy(mac_out, local_mac);
}

void eth_set_mac(const uint8_t mac[ETH_ALEN]) {
    eth_mac_copy(local_mac, mac);
    kprintf("[ETH] MAC address changed to %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int eth_send(const uint8_t dest_mac[ETH_ALEN], uint16_t ethertype,
             const void *data, size_t len) {
    uint8_t frame[ETH_FRAME_MAX];
    eth_header_t *hdr;
    size_t frame_len;

    if (!eth_initialized) {
        kprintf("[ETH] Error: Not initialized\n");
        return -1;
    }

    if (data == NULL && len > 0) {
        kprintf("[ETH] Error: NULL data with non-zero length\n");
        return -1;
    }

    if (len > ETH_DATA_MAX) {
        kprintf("[ETH] Error: Payload too large (%u > %u)\n",
                (uint32_t)len, ETH_DATA_MAX);
        return -1;
    }

    /* Build frame header */
    hdr = (eth_header_t *)frame;
    eth_mac_copy(hdr->dest_mac, dest_mac);
    eth_mac_copy(hdr->src_mac, local_mac);
    hdr->ethertype = htons(ethertype);

    /* Copy payload */
    if (len > 0) {
        memcpy(frame + ETH_HLEN, data, len);
    }

    /* Calculate total frame length (pad if needed) */
    frame_len = ETH_HLEN + len;
    if (frame_len < ETH_FRAME_MIN) {
        /* Pad with zeros */
        memset(frame + frame_len, 0, ETH_FRAME_MIN - frame_len);
        frame_len = ETH_FRAME_MIN;
    }

    kprintf("[ETH] TX: %02x:%02x:%02x:%02x:%02x:%02x -> "
            "%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%u\n",
            local_mac[0], local_mac[1], local_mac[2],
            local_mac[3], local_mac[4], local_mac[5],
            dest_mac[0], dest_mac[1], dest_mac[2],
            dest_mac[3], dest_mac[4], dest_mac[5],
            ethertype, (uint32_t)frame_len);

    /* Send via hardware driver */
    return eth_hw_send(frame, frame_len);
}

int eth_send_buf(netbuf_t *buf, const uint8_t dest_mac[ETH_ALEN],
                 uint16_t ethertype) {
    eth_header_t *hdr;

    if (!eth_initialized) {
        kprintf("[ETH] Error: Not initialized\n");
        return -1;
    }

    if (buf == NULL) {
        kprintf("[ETH] Error: NULL buffer\n");
        return -1;
    }

    if (buf->len > ETH_DATA_MAX) {
        kprintf("[ETH] Error: Payload too large (%u > %u)\n",
                (uint32_t)buf->len, ETH_DATA_MAX);
        return -1;
    }

    /* Push space for Ethernet header */
    hdr = (eth_header_t *)netbuf_push(buf, ETH_HLEN);
    if (hdr == NULL) {
        kprintf("[ETH] Error: Insufficient headroom for header\n");
        return -1;
    }

    /* Fill in header */
    eth_mac_copy(hdr->dest_mac, dest_mac);
    eth_mac_copy(hdr->src_mac, local_mac);
    hdr->ethertype = htons(ethertype);

    /* Store in buffer metadata */
    eth_mac_copy(buf->src_mac, local_mac);
    eth_mac_copy(buf->dst_mac, dest_mac);
    buf->protocol = ethertype;

    kprintf("[ETH] TX: %02x:%02x:%02x:%02x:%02x:%02x -> "
            "%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%u\n",
            local_mac[0], local_mac[1], local_mac[2],
            local_mac[3], local_mac[4], local_mac[5],
            dest_mac[0], dest_mac[1], dest_mac[2],
            dest_mac[3], dest_mac[4], dest_mac[5],
            ethertype, (uint32_t)buf->len);

    /* Send via hardware driver */
    return eth_hw_send(buf->data, buf->len);
}

int eth_receive(const void *packet, size_t len) {
    const eth_header_t *hdr;
    uint16_t ethertype;
    const uint8_t *payload;
    size_t payload_len;

    if (!eth_initialized) {
        kprintf("[ETH] Error: Not initialized\n");
        return -1;
    }

    if (packet == NULL || len < ETH_HLEN) {
        kprintf("[ETH] Error: Invalid packet (len=%u)\n", (uint32_t)len);
        return -1;
    }

    hdr = (const eth_header_t *)packet;
    ethertype = ntohs(hdr->ethertype);
    payload = (const uint8_t *)packet + ETH_HLEN;
    payload_len = len - ETH_HLEN;

    kprintf("[ETH] RX: %02x:%02x:%02x:%02x:%02x:%02x -> "
            "%02x:%02x:%02x:%02x:%02x:%02x type=0x%04x len=%u\n",
            hdr->src_mac[0], hdr->src_mac[1], hdr->src_mac[2],
            hdr->src_mac[3], hdr->src_mac[4], hdr->src_mac[5],
            hdr->dest_mac[0], hdr->dest_mac[1], hdr->dest_mac[2],
            hdr->dest_mac[3], hdr->dest_mac[4], hdr->dest_mac[5],
            ethertype, (uint32_t)len);

    /* Check if frame is for us */
    if (!eth_is_broadcast(hdr->dest_mac) &&
        !eth_mac_equal(hdr->dest_mac, local_mac)) {
        /* Not for us - in promiscuous mode we might still process it */
        kprintf("[ETH] Frame not for us, dropping\n");
        return 0;
    }

    /* Dispatch based on EtherType */
    switch (ethertype) {
        case ETH_TYPE_ARP:
            return arp_receive(payload, payload_len);

        case ETH_TYPE_IPV4:
            return ip_receive(payload, payload_len);

        case ETH_TYPE_IPV6:
            kprintf("[ETH] IPv6 not supported\n");
            return -1;

        default:
            kprintf("[ETH] Unknown EtherType 0x%04x\n", ethertype);
            return -1;
    }
}
