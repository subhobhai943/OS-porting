/**
 * AAAos Network Stack - ARP Implementation
 */

#include "arp.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"
#include "../ip/ip.h"

/* ARP cache */
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static bool arp_initialized = false;

/* Current time counter (incremented by timer) */
static uint32_t arp_time = 0;

/* Broadcast MAC address */
static const uint8_t broadcast_mac[ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t zero_mac[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

/* Forward declaration */
static arp_entry_t *arp_cache_find(uint32_t ip);
static arp_entry_t *arp_cache_alloc(void);

void arp_init(void) {
    /* Clear the cache */
    memset(arp_cache, 0, sizeof(arp_cache));
    arp_time = 0;
    arp_initialized = true;

    kprintf("[ARP] Initialized with cache size %u\n", ARP_CACHE_SIZE);
}

int arp_lookup(uint32_t ip, uint8_t mac_out[ETH_ALEN]) {
    arp_entry_t *entry;

    if (!arp_initialized) {
        kprintf("[ARP] Error: Not initialized\n");
        return -1;
    }

    /* Look for existing entry */
    entry = arp_cache_find(ip);

    if (entry != NULL) {
        if (entry->state == ARP_STATE_RESOLVED) {
            /* Found it! */
            eth_mac_copy(mac_out, entry->mac);
            return 0;
        } else if (entry->state == ARP_STATE_PENDING) {
            /* Already waiting for this IP */
            return -1;
        }
    }

    /* Not found - send ARP request */
    kprintf("[ARP] Cache miss for %u.%u.%u.%u, sending request\n",
            ip_octet(ip, 0), ip_octet(ip, 1),
            ip_octet(ip, 2), ip_octet(ip, 3));

    /* Create pending entry */
    if (entry == NULL) {
        entry = arp_cache_alloc();
        if (entry != NULL) {
            entry->ip = ip;
            entry->state = ARP_STATE_PENDING;
            entry->timestamp = arp_time;
            entry->retries = ARP_REQUEST_RETRIES;
        }
    }

    /* Send the request */
    arp_request(ip);

    return -1;  /* Not resolved yet */
}

int arp_request(uint32_t ip) {
    arp_packet_t pkt;
    uint8_t our_mac[ETH_ALEN];
    uint32_t our_ip;

    if (!arp_initialized) {
        return -1;
    }

    /* Get our addresses */
    eth_get_mac(our_mac);
    our_ip = ip_get_addr();

    /* Build ARP request packet */
    pkt.hrd = htons(ARP_HRD_ETHERNET);
    pkt.pro = htons(ARP_PRO_IPV4);
    pkt.hln = ARP_HLEN;
    pkt.pln = ARP_PLEN;
    pkt.op = htons(ARP_OP_REQUEST);

    /* Sender: us */
    eth_mac_copy(pkt.sha, our_mac);
    pkt.spa = htonl(our_ip);

    /* Target: who we're looking for */
    memset(pkt.tha, 0, ETH_ALEN);  /* Unknown */
    pkt.tpa = htonl(ip);

    kprintf("[ARP] Request: Who has %u.%u.%u.%u? Tell %u.%u.%u.%u\n",
            ip_octet(ip, 0), ip_octet(ip, 1),
            ip_octet(ip, 2), ip_octet(ip, 3),
            ip_octet(our_ip, 0), ip_octet(our_ip, 1),
            ip_octet(our_ip, 2), ip_octet(our_ip, 3));

    /* Send as broadcast */
    return eth_send(broadcast_mac, ETH_TYPE_ARP, &pkt, sizeof(pkt));
}

int arp_reply(uint32_t dest_ip, const uint8_t dest_mac[ETH_ALEN]) {
    arp_packet_t pkt;
    uint8_t our_mac[ETH_ALEN];
    uint32_t our_ip;

    if (!arp_initialized) {
        return -1;
    }

    /* Get our addresses */
    eth_get_mac(our_mac);
    our_ip = ip_get_addr();

    /* Build ARP reply packet */
    pkt.hrd = htons(ARP_HRD_ETHERNET);
    pkt.pro = htons(ARP_PRO_IPV4);
    pkt.hln = ARP_HLEN;
    pkt.pln = ARP_PLEN;
    pkt.op = htons(ARP_OP_REPLY);

    /* Sender: us */
    eth_mac_copy(pkt.sha, our_mac);
    pkt.spa = htonl(our_ip);

    /* Target: who asked */
    eth_mac_copy(pkt.tha, dest_mac);
    pkt.tpa = htonl(dest_ip);

    kprintf("[ARP] Reply: %u.%u.%u.%u is at %02x:%02x:%02x:%02x:%02x:%02x\n",
            ip_octet(our_ip, 0), ip_octet(our_ip, 1),
            ip_octet(our_ip, 2), ip_octet(our_ip, 3),
            our_mac[0], our_mac[1], our_mac[2],
            our_mac[3], our_mac[4], our_mac[5]);

    /* Send directly to requester */
    return eth_send(dest_mac, ETH_TYPE_ARP, &pkt, sizeof(pkt));
}

int arp_receive(const void *packet, size_t len) {
    const arp_packet_t *pkt;
    uint16_t op;
    uint32_t spa, tpa;
    uint32_t our_ip;

    if (!arp_initialized) {
        kprintf("[ARP] Error: Not initialized\n");
        return -1;
    }

    if (packet == NULL || len < sizeof(arp_packet_t)) {
        kprintf("[ARP] Error: Packet too small (%u bytes)\n", (uint32_t)len);
        return -1;
    }

    pkt = (const arp_packet_t *)packet;

    /* Validate ARP packet */
    if (ntohs(pkt->hrd) != ARP_HRD_ETHERNET ||
        ntohs(pkt->pro) != ARP_PRO_IPV4 ||
        pkt->hln != ARP_HLEN ||
        pkt->pln != ARP_PLEN) {
        kprintf("[ARP] Error: Invalid ARP format\n");
        return -1;
    }

    op = ntohs(pkt->op);
    spa = ntohl(pkt->spa);
    tpa = ntohl(pkt->tpa);
    our_ip = ip_get_addr();

    kprintf("[ARP] RX: op=%u spa=%u.%u.%u.%u sha=%02x:%02x:%02x:%02x:%02x:%02x\n",
            op,
            ip_octet(spa, 0), ip_octet(spa, 1),
            ip_octet(spa, 2), ip_octet(spa, 3),
            pkt->sha[0], pkt->sha[1], pkt->sha[2],
            pkt->sha[3], pkt->sha[4], pkt->sha[5]);

    /* Update cache with sender's info (if we already have an entry) */
    arp_entry_t *entry = arp_cache_find(spa);
    if (entry != NULL || tpa == our_ip) {
        /* Either we had an entry, or this is addressed to us */
        arp_cache_add(spa, pkt->sha);
    }

    /* Handle based on operation */
    switch (op) {
        case ARP_OP_REQUEST:
            /* Is this for us? */
            if (tpa == our_ip) {
                kprintf("[ARP] Request for our IP, sending reply\n");
                return arp_reply(spa, pkt->sha);
            }
            break;

        case ARP_OP_REPLY:
            kprintf("[ARP] Reply received: %u.%u.%u.%u is at "
                    "%02x:%02x:%02x:%02x:%02x:%02x\n",
                    ip_octet(spa, 0), ip_octet(spa, 1),
                    ip_octet(spa, 2), ip_octet(spa, 3),
                    pkt->sha[0], pkt->sha[1], pkt->sha[2],
                    pkt->sha[3], pkt->sha[4], pkt->sha[5]);
            /* Cache was already updated above */
            break;

        default:
            kprintf("[ARP] Unknown operation %u\n", op);
            return -1;
    }

    return 0;
}

int arp_cache_add(uint32_t ip, const uint8_t mac[ETH_ALEN]) {
    arp_entry_t *entry;

    if (!arp_initialized) {
        return -1;
    }

    /* Don't cache zero MAC or broadcast */
    if (memcmp(mac, zero_mac, ETH_ALEN) == 0 ||
        memcmp(mac, broadcast_mac, ETH_ALEN) == 0) {
        return -1;
    }

    /* Look for existing entry */
    entry = arp_cache_find(ip);

    if (entry == NULL) {
        /* Allocate new entry */
        entry = arp_cache_alloc();
        if (entry == NULL) {
            kprintf("[ARP] Cache full, cannot add entry\n");
            return -1;
        }
    }

    /* Update entry */
    entry->ip = ip;
    eth_mac_copy(entry->mac, mac);
    entry->state = ARP_STATE_RESOLVED;
    entry->timestamp = arp_time;
    entry->retries = 0;

    kprintf("[ARP] Cache updated: %u.%u.%u.%u -> "
            "%02x:%02x:%02x:%02x:%02x:%02x\n",
            ip_octet(ip, 0), ip_octet(ip, 1),
            ip_octet(ip, 2), ip_octet(ip, 3),
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return 0;
}

void arp_cache_remove(uint32_t ip) {
    arp_entry_t *entry = arp_cache_find(ip);
    if (entry != NULL) {
        entry->state = ARP_STATE_FREE;
        kprintf("[ARP] Removed cache entry for %u.%u.%u.%u\n",
                ip_octet(ip, 0), ip_octet(ip, 1),
                ip_octet(ip, 2), ip_octet(ip, 3));
    }
}

void arp_cache_clear(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    kprintf("[ARP] Cache cleared\n");
}

void arp_cache_dump(void) {
    kprintf("[ARP] Cache contents:\n");
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *e = &arp_cache[i];
        if (e->state != ARP_STATE_FREE) {
            const char *state_str;
            switch (e->state) {
                case ARP_STATE_PENDING:  state_str = "PENDING"; break;
                case ARP_STATE_RESOLVED: state_str = "RESOLVED"; break;
                case ARP_STATE_STALE:    state_str = "STALE"; break;
                default:                 state_str = "UNKNOWN"; break;
            }
            kprintf("  %u.%u.%u.%u -> %02x:%02x:%02x:%02x:%02x:%02x [%s]\n",
                    ip_octet(e->ip, 0), ip_octet(e->ip, 1),
                    ip_octet(e->ip, 2), ip_octet(e->ip, 3),
                    e->mac[0], e->mac[1], e->mac[2],
                    e->mac[3], e->mac[4], e->mac[5],
                    state_str);
        }
    }
}

void arp_timer_tick(void) {
    arp_time++;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        arp_entry_t *e = &arp_cache[i];

        switch (e->state) {
            case ARP_STATE_PENDING:
                /* Check for retry timeout */
                if (arp_time - e->timestamp >= ARP_REQUEST_TIMEOUT) {
                    if (e->retries > 0) {
                        e->retries--;
                        e->timestamp = arp_time;
                        arp_request(e->ip);
                    } else {
                        /* Give up */
                        kprintf("[ARP] Request timeout for %u.%u.%u.%u\n",
                                ip_octet(e->ip, 0), ip_octet(e->ip, 1),
                                ip_octet(e->ip, 2), ip_octet(e->ip, 3));
                        e->state = ARP_STATE_FREE;
                    }
                }
                break;

            case ARP_STATE_RESOLVED:
                /* Check for expiration */
                if (arp_time - e->timestamp >= ARP_CACHE_TIMEOUT) {
                    e->state = ARP_STATE_STALE;
                }
                break;

            case ARP_STATE_STALE:
                /* Will be refreshed on next lookup or eventually freed */
                if (arp_time - e->timestamp >= ARP_CACHE_TIMEOUT * 2) {
                    e->state = ARP_STATE_FREE;
                }
                break;

            default:
                break;
        }
    }
}

/* Internal: Find entry in cache */
static arp_entry_t *arp_cache_find(uint32_t ip) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state != ARP_STATE_FREE &&
            arp_cache[i].ip == ip) {
            return &arp_cache[i];
        }
    }
    return NULL;
}

/* Internal: Allocate a new cache entry */
static arp_entry_t *arp_cache_alloc(void) {
    arp_entry_t *oldest = NULL;
    uint32_t oldest_time = UINT32_MAX;

    /* First, look for a free entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_FREE) {
            return &arp_cache[i];
        }
    }

    /* No free entry - find oldest stale entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].state == ARP_STATE_STALE) {
            if (arp_cache[i].timestamp < oldest_time) {
                oldest_time = arp_cache[i].timestamp;
                oldest = &arp_cache[i];
            }
        }
    }

    if (oldest != NULL) {
        return oldest;
    }

    /* No stale entries - evict oldest resolved entry */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].timestamp < oldest_time) {
            oldest_time = arp_cache[i].timestamp;
            oldest = &arp_cache[i];
        }
    }

    return oldest;
}

/* Utility functions */

char *ip_to_string(uint32_t ip, char *buf) {
    int i = 0;
    for (int octet = 0; octet < 4; octet++) {
        uint8_t val = ip_octet(ip, octet);
        if (val >= 100) {
            buf[i++] = '0' + val / 100;
            val %= 100;
            buf[i++] = '0' + val / 10;
            buf[i++] = '0' + val % 10;
        } else if (val >= 10) {
            buf[i++] = '0' + val / 10;
            buf[i++] = '0' + val % 10;
        } else {
            buf[i++] = '0' + val;
        }
        if (octet < 3) {
            buf[i++] = '.';
        }
    }
    buf[i] = '\0';
    return buf;
}

int string_to_ip(const char *str, uint32_t *ip_out) {
    uint32_t ip = 0;
    uint32_t octet = 0;
    int octets = 0;

    for (const char *p = str; ; p++) {
        if (*p >= '0' && *p <= '9') {
            octet = octet * 10 + (*p - '0');
            if (octet > 255) return -1;
        } else if (*p == '.' || *p == '\0') {
            ip = (ip << 8) | octet;
            octets++;
            octet = 0;
            if (*p == '\0') break;
            if (octets >= 4) return -1;
        } else {
            return -1;
        }
    }

    if (octets != 4) return -1;
    *ip_out = ip;
    return 0;
}
