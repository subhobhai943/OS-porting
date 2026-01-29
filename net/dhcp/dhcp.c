/**
 * AAAos Network Stack - DHCP Client Implementation
 *
 * Implements RFC 2131 Dynamic Host Configuration Protocol client.
 */

#include "dhcp.h"
#include "../ethernet/ethernet.h"
#include "../ip/ip.h"
#include "../udp/udp.h"
#include "../../kernel/include/types.h"

/* Forward declaration for kernel logging */
extern void kprintf(const char *fmt, ...);

/* Forward declaration for time functions */
extern uint64_t timer_get_ticks(void);
extern void timer_sleep_ms(uint32_t ms);

/* Forward declaration for memory functions */
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);

/* Global DHCP client state */
static dhcp_client_t g_dhcp_client;

/* UDP socket for DHCP communication */
static udp_socket_t *g_dhcp_socket = NULL;

/* Simple pseudo-random number generator for XID */
static uint32_t g_rand_seed = 0x12345678;

/**
 * Simple PRNG for transaction ID generation
 */
static uint32_t dhcp_rand(void) {
    g_rand_seed = g_rand_seed * 1103515245 + 12345;
    return (g_rand_seed >> 16) & 0x7FFFFFFF;
}

/**
 * Generate a random transaction ID
 */
uint32_t dhcp_generate_xid(void) {
    /* Mix in timer ticks for more entropy */
    g_rand_seed ^= (uint32_t)timer_get_ticks();
    return dhcp_rand();
}

/**
 * Get DHCP state as string
 */
const char *dhcp_state_string(dhcp_state_t state) {
    switch (state) {
        case DHCP_STATE_INIT:       return "INIT";
        case DHCP_STATE_SELECTING:  return "SELECTING";
        case DHCP_STATE_REQUESTING: return "REQUESTING";
        case DHCP_STATE_BOUND:      return "BOUND";
        case DHCP_STATE_RENEWING:   return "RENEWING";
        case DHCP_STATE_REBINDING:  return "REBINDING";
        case DHCP_STATE_REBOOTING:  return "REBOOTING";
        case DHCP_STATE_INIT_REBOOT: return "INIT_REBOOT";
        default:                    return "UNKNOWN";
    }
}

/**
 * Initialize the DHCP client
 */
int dhcp_init(void) {
    kprintf("DHCP: Initializing DHCP client\n");

    /* Clear client state */
    memset(&g_dhcp_client, 0, sizeof(g_dhcp_client));

    /* Get our MAC address */
    eth_get_mac(g_dhcp_client.mac);

    /* Bind to DHCP client port (68) */
    g_dhcp_socket = udp_bind(DHCP_CLIENT_PORT);
    if (!g_dhcp_socket) {
        kprintf("DHCP: Failed to bind to port %d\n", DHCP_CLIENT_PORT);
        return DHCP_ERR_NETWORK;
    }

    /* Enable broadcast on socket */
    udp_set_broadcast(g_dhcp_socket, true);

    g_dhcp_client.state = DHCP_STATE_INIT;
    g_dhcp_client.initialized = true;

    kprintf("DHCP: Client initialized, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            g_dhcp_client.mac[0], g_dhcp_client.mac[1],
            g_dhcp_client.mac[2], g_dhcp_client.mac[3],
            g_dhcp_client.mac[4], g_dhcp_client.mac[5]);

    return DHCP_OK;
}

/**
 * Add an option to the options buffer
 * Returns pointer to next position after the option
 */
static uint8_t *dhcp_add_option(uint8_t *ptr, uint8_t code, uint8_t len, const void *data) {
    *ptr++ = code;
    if (code != DHCP_OPT_PAD && code != DHCP_OPT_END) {
        *ptr++ = len;
        if (data && len > 0) {
            memcpy(ptr, data, len);
            ptr += len;
        }
    }
    return ptr;
}

/**
 * Add a single-byte option
 */
static uint8_t *dhcp_add_option_byte(uint8_t *ptr, uint8_t code, uint8_t value) {
    return dhcp_add_option(ptr, code, 1, &value);
}

/**
 * Add a 4-byte (IP address) option
 */
static uint8_t *dhcp_add_option_ip(uint8_t *ptr, uint8_t code, uint32_t ip) {
    return dhcp_add_option(ptr, code, 4, &ip);
}

/**
 * Build common DHCP packet header
 */
static void dhcp_build_header(dhcp_packet_t *packet, uint8_t msg_type) {
    memset(packet, 0, sizeof(dhcp_packet_t));

    packet->op = DHCP_OP_REQUEST;
    packet->htype = DHCP_HTYPE_ETHERNET;
    packet->hlen = DHCP_HLEN_ETHERNET;
    packet->hops = 0;
    packet->xid = htonl(g_dhcp_client.xid);
    packet->secs = 0;
    packet->flags = htons(DHCP_FLAG_BROADCAST);  /* Request broadcast reply */
    packet->ciaddr = 0;
    packet->yiaddr = 0;
    packet->siaddr = 0;
    packet->giaddr = 0;

    /* Copy our MAC address */
    memcpy(packet->chaddr, g_dhcp_client.mac, 6);
}

/**
 * Build a DHCP DISCOVER packet
 */
int dhcp_build_discover(dhcp_packet_t *packet) {
    uint8_t *opt;

    dhcp_build_header(packet, DHCP_MSG_DISCOVER);

    /* Build options */
    opt = packet->options;

    /* Magic cookie */
    *opt++ = 0x63;
    *opt++ = 0x82;
    *opt++ = 0x53;
    *opt++ = 0x63;

    /* DHCP Message Type: DISCOVER */
    opt = dhcp_add_option_byte(opt, DHCP_OPT_MSG_TYPE, DHCP_MSG_DISCOVER);

    /* Client Identifier (Type 1 = Ethernet, followed by MAC) */
    {
        uint8_t client_id[7];
        client_id[0] = DHCP_HTYPE_ETHERNET;
        memcpy(&client_id[1], g_dhcp_client.mac, 6);
        opt = dhcp_add_option(opt, DHCP_OPT_CLIENT_ID, 7, client_id);
    }

    /* Parameter Request List - request common options */
    {
        uint8_t params[] = {
            DHCP_OPT_SUBNET_MASK,
            DHCP_OPT_ROUTER,
            DHCP_OPT_DNS_SERVER,
            DHCP_OPT_DOMAIN_NAME,
            DHCP_OPT_BROADCAST,
            DHCP_OPT_LEASE_TIME,
            DHCP_OPT_RENEWAL_TIME,
            DHCP_OPT_REBINDING_TIME
        };
        opt = dhcp_add_option(opt, DHCP_OPT_PARAM_REQUEST, sizeof(params), params);
    }

    /* End option */
    *opt++ = DHCP_OPT_END;

    /* Return total packet size */
    return DHCP_PACKET_MIN_SIZE + (opt - packet->options);
}

/**
 * Build a DHCP REQUEST packet
 */
int dhcp_build_request(dhcp_packet_t *packet, uint32_t offered_ip, uint32_t server_ip) {
    uint8_t *opt;

    dhcp_build_header(packet, DHCP_MSG_REQUEST);

    /* If renewing, set ciaddr to our current IP */
    if (g_dhcp_client.state == DHCP_STATE_RENEWING ||
        g_dhcp_client.state == DHCP_STATE_REBINDING) {
        packet->ciaddr = g_dhcp_client.lease.ip_addr;
        packet->flags = 0;  /* No broadcast flag when renewing */
    }

    /* Build options */
    opt = packet->options;

    /* Magic cookie */
    *opt++ = 0x63;
    *opt++ = 0x82;
    *opt++ = 0x53;
    *opt++ = 0x63;

    /* DHCP Message Type: REQUEST */
    opt = dhcp_add_option_byte(opt, DHCP_OPT_MSG_TYPE, DHCP_MSG_REQUEST);

    /* Client Identifier */
    {
        uint8_t client_id[7];
        client_id[0] = DHCP_HTYPE_ETHERNET;
        memcpy(&client_id[1], g_dhcp_client.mac, 6);
        opt = dhcp_add_option(opt, DHCP_OPT_CLIENT_ID, 7, client_id);
    }

    /* Requested IP Address (only in SELECTING/INIT_REBOOT states) */
    if (g_dhcp_client.state == DHCP_STATE_SELECTING ||
        g_dhcp_client.state == DHCP_STATE_INIT_REBOOT ||
        g_dhcp_client.state == DHCP_STATE_REBOOTING) {
        opt = dhcp_add_option_ip(opt, DHCP_OPT_REQUESTED_IP, offered_ip);
    }

    /* Server Identifier (only in SELECTING state) */
    if (g_dhcp_client.state == DHCP_STATE_SELECTING) {
        opt = dhcp_add_option_ip(opt, DHCP_OPT_SERVER_ID, server_ip);
    }

    /* Parameter Request List */
    {
        uint8_t params[] = {
            DHCP_OPT_SUBNET_MASK,
            DHCP_OPT_ROUTER,
            DHCP_OPT_DNS_SERVER,
            DHCP_OPT_DOMAIN_NAME,
            DHCP_OPT_BROADCAST,
            DHCP_OPT_LEASE_TIME,
            DHCP_OPT_RENEWAL_TIME,
            DHCP_OPT_REBINDING_TIME
        };
        opt = dhcp_add_option(opt, DHCP_OPT_PARAM_REQUEST, sizeof(params), params);
    }

    /* End option */
    *opt++ = DHCP_OPT_END;

    return DHCP_PACKET_MIN_SIZE + (opt - packet->options);
}

/**
 * Build a DHCP RELEASE packet
 */
int dhcp_build_release(dhcp_packet_t *packet) {
    uint8_t *opt;

    dhcp_build_header(packet, DHCP_MSG_RELEASE);

    /* Set our current IP */
    packet->ciaddr = g_dhcp_client.lease.ip_addr;
    packet->flags = 0;  /* No broadcast */

    /* Build options */
    opt = packet->options;

    /* Magic cookie */
    *opt++ = 0x63;
    *opt++ = 0x82;
    *opt++ = 0x53;
    *opt++ = 0x63;

    /* DHCP Message Type: RELEASE */
    opt = dhcp_add_option_byte(opt, DHCP_OPT_MSG_TYPE, DHCP_MSG_RELEASE);

    /* Server Identifier */
    opt = dhcp_add_option_ip(opt, DHCP_OPT_SERVER_ID, g_dhcp_client.lease.server_ip);

    /* Client Identifier */
    {
        uint8_t client_id[7];
        client_id[0] = DHCP_HTYPE_ETHERNET;
        memcpy(&client_id[1], g_dhcp_client.mac, 6);
        opt = dhcp_add_option(opt, DHCP_OPT_CLIENT_ID, 7, client_id);
    }

    /* End option */
    *opt++ = DHCP_OPT_END;

    return DHCP_PACKET_MIN_SIZE + (opt - packet->options);
}

/**
 * Parse DHCP options from a packet
 */
int dhcp_parse_options(const uint8_t *options, size_t len,
                       dhcp_lease_t *lease_out, uint8_t *msg_type) {
    const uint8_t *ptr = options;
    const uint8_t *end = options + len;

    /* Verify magic cookie */
    if (len < 4 || ptr[0] != 0x63 || ptr[1] != 0x82 ||
        ptr[2] != 0x53 || ptr[3] != 0x63) {
        kprintf("DHCP: Invalid magic cookie\n");
        return DHCP_ERR_INVALID;
    }
    ptr += 4;

    /* Parse options */
    while (ptr < end) {
        uint8_t code = *ptr++;

        /* Handle PAD and END specially */
        if (code == DHCP_OPT_PAD) {
            continue;
        }
        if (code == DHCP_OPT_END) {
            break;
        }

        /* Get option length */
        if (ptr >= end) {
            break;
        }
        uint8_t opt_len = *ptr++;

        /* Validate we have enough data */
        if (ptr + opt_len > end) {
            kprintf("DHCP: Option %d truncated\n", code);
            break;
        }

        /* Process option */
        switch (code) {
            case DHCP_OPT_MSG_TYPE:
                if (opt_len >= 1 && msg_type) {
                    *msg_type = ptr[0];
                }
                break;

            case DHCP_OPT_SUBNET_MASK:
                if (opt_len >= 4 && lease_out) {
                    memcpy(&lease_out->subnet_mask, ptr, 4);
                }
                break;

            case DHCP_OPT_ROUTER:
                if (opt_len >= 4 && lease_out) {
                    memcpy(&lease_out->gateway, ptr, 4);
                }
                break;

            case DHCP_OPT_DNS_SERVER:
                if (opt_len >= 4 && lease_out) {
                    memcpy(&lease_out->dns_server, ptr, 4);
                    if (opt_len >= 8) {
                        memcpy(&lease_out->dns_server2, ptr + 4, 4);
                    }
                }
                break;

            case DHCP_OPT_BROADCAST:
                if (opt_len >= 4 && lease_out) {
                    memcpy(&lease_out->broadcast, ptr, 4);
                }
                break;

            case DHCP_OPT_LEASE_TIME:
                if (opt_len >= 4 && lease_out) {
                    uint32_t time;
                    memcpy(&time, ptr, 4);
                    lease_out->lease_time = ntohl(time);
                }
                break;

            case DHCP_OPT_SERVER_ID:
                if (opt_len >= 4 && lease_out) {
                    memcpy(&lease_out->server_ip, ptr, 4);
                }
                break;

            case DHCP_OPT_RENEWAL_TIME:
                if (opt_len >= 4 && lease_out) {
                    uint32_t time;
                    memcpy(&time, ptr, 4);
                    lease_out->renewal_time = ntohl(time);
                }
                break;

            case DHCP_OPT_REBINDING_TIME:
                if (opt_len >= 4 && lease_out) {
                    uint32_t time;
                    memcpy(&time, ptr, 4);
                    lease_out->rebind_time = ntohl(time);
                }
                break;

            default:
                /* Unknown option, skip it */
                break;
        }

        ptr += opt_len;
    }

    return DHCP_OK;
}

/**
 * Send a DHCP packet
 */
static int dhcp_send_packet(const dhcp_packet_t *packet, size_t len, uint32_t dest_ip) {
    ssize_t sent;

    kprintf("DHCP: Sending %zu bytes to %u.%u.%u.%u\n",
            len,
            (dest_ip >> 24) & 0xFF,
            (dest_ip >> 16) & 0xFF,
            (dest_ip >> 8) & 0xFF,
            dest_ip & 0xFF);

    /* Send via UDP */
    sent = udp_send(dest_ip, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, packet, len);
    if (sent < 0) {
        kprintf("DHCP: Failed to send packet, error=%zd\n", sent);
        return DHCP_ERR_NETWORK;
    }

    return DHCP_OK;
}

/**
 * Start DHCP discovery process
 */
int dhcp_discover(void) {
    dhcp_packet_t packet;
    int pkt_len;
    int ret;

    if (!g_dhcp_client.initialized) {
        kprintf("DHCP: Client not initialized\n");
        return DHCP_ERR_NOT_INIT;
    }

    kprintf("DHCP: Starting discovery\n");

    /* Generate new transaction ID */
    g_dhcp_client.xid = dhcp_generate_xid();

    /* Build DISCOVER packet */
    pkt_len = dhcp_build_discover(&packet);
    if (pkt_len < 0) {
        return pkt_len;
    }

    /* Send broadcast DISCOVER */
    ret = dhcp_send_packet(&packet, pkt_len, IP_ADDR_BROADCAST);
    if (ret < 0) {
        return ret;
    }

    /* Update state */
    g_dhcp_client.state = DHCP_STATE_SELECTING;
    g_dhcp_client.retries = 0;
    g_dhcp_client.timeout_time = timer_get_ticks() +
                                  (DHCP_DISCOVER_TIMEOUT * 1000);  /* Convert to ms/ticks */

    kprintf("DHCP: DISCOVER sent, xid=0x%08x\n", g_dhcp_client.xid);
    return DHCP_OK;
}

/**
 * Send DHCP REQUEST for an offered IP address
 */
int dhcp_request(uint32_t offered_ip, uint32_t server_ip) {
    dhcp_packet_t packet;
    int pkt_len;
    int ret;
    uint32_t dest_ip;

    if (!g_dhcp_client.initialized) {
        return DHCP_ERR_NOT_INIT;
    }

    kprintf("DHCP: Requesting IP %u.%u.%u.%u from server %u.%u.%u.%u\n",
            (ntohl(offered_ip) >> 24) & 0xFF,
            (ntohl(offered_ip) >> 16) & 0xFF,
            (ntohl(offered_ip) >> 8) & 0xFF,
            ntohl(offered_ip) & 0xFF,
            (ntohl(server_ip) >> 24) & 0xFF,
            (ntohl(server_ip) >> 16) & 0xFF,
            (ntohl(server_ip) >> 8) & 0xFF,
            ntohl(server_ip) & 0xFF);

    /* Build REQUEST packet */
    pkt_len = dhcp_build_request(&packet, offered_ip, server_ip);
    if (pkt_len < 0) {
        return pkt_len;
    }

    /* Determine destination:
     * - In SELECTING state: broadcast
     * - In RENEWING state: unicast to server
     * - In REBINDING state: broadcast
     */
    if (g_dhcp_client.state == DHCP_STATE_RENEWING) {
        dest_ip = ntohl(server_ip);
    } else {
        dest_ip = IP_ADDR_BROADCAST;
    }

    /* Send REQUEST */
    ret = dhcp_send_packet(&packet, pkt_len, dest_ip);
    if (ret < 0) {
        return ret;
    }

    /* Update state */
    if (g_dhcp_client.state == DHCP_STATE_SELECTING) {
        g_dhcp_client.state = DHCP_STATE_REQUESTING;
    }
    g_dhcp_client.timeout_time = timer_get_ticks() +
                                  (DHCP_REQUEST_TIMEOUT * 1000);

    kprintf("DHCP: REQUEST sent\n");
    return DHCP_OK;
}

/**
 * Release the current DHCP lease
 */
int dhcp_release(void) {
    dhcp_packet_t packet;
    int pkt_len;

    if (!g_dhcp_client.initialized) {
        return DHCP_ERR_NOT_INIT;
    }

    if (!g_dhcp_client.lease.valid) {
        kprintf("DHCP: No lease to release\n");
        return DHCP_ERR_NO_LEASE;
    }

    kprintf("DHCP: Releasing lease for IP %u.%u.%u.%u\n",
            (ntohl(g_dhcp_client.lease.ip_addr) >> 24) & 0xFF,
            (ntohl(g_dhcp_client.lease.ip_addr) >> 16) & 0xFF,
            (ntohl(g_dhcp_client.lease.ip_addr) >> 8) & 0xFF,
            ntohl(g_dhcp_client.lease.ip_addr) & 0xFF);

    /* Build RELEASE packet */
    pkt_len = dhcp_build_release(&packet);
    if (pkt_len < 0) {
        return pkt_len;
    }

    /* Send RELEASE unicast to server */
    dhcp_send_packet(&packet, pkt_len, ntohl(g_dhcp_client.lease.server_ip));

    /* Clear lease and reset state */
    memset(&g_dhcp_client.lease, 0, sizeof(g_dhcp_client.lease));
    g_dhcp_client.state = DHCP_STATE_INIT;

    /* Reset network interface */
    ip_set_addr(0);
    ip_set_netmask(0);
    ip_set_gateway(0);

    kprintf("DHCP: Lease released\n");
    return DHCP_OK;
}

/**
 * Renew the current DHCP lease
 */
int dhcp_renew(void) {
    if (!g_dhcp_client.initialized) {
        return DHCP_ERR_NOT_INIT;
    }

    if (!g_dhcp_client.lease.valid) {
        kprintf("DHCP: No lease to renew\n");
        return DHCP_ERR_NO_LEASE;
    }

    kprintf("DHCP: Renewing lease\n");

    g_dhcp_client.state = DHCP_STATE_RENEWING;
    g_dhcp_client.retries = 0;

    return dhcp_request(g_dhcp_client.lease.ip_addr,
                        g_dhcp_client.lease.server_ip);
}

/**
 * Get current DHCP lease information
 */
const dhcp_lease_t *dhcp_get_lease(void) {
    if (!g_dhcp_client.initialized || !g_dhcp_client.lease.valid) {
        return NULL;
    }
    return &g_dhcp_client.lease;
}

/**
 * Apply DHCP lease to network interface
 */
int dhcp_apply_lease(const dhcp_lease_t *lease) {
    uint32_t ip_host, mask_host, gw_host;

    if (!lease || !lease->valid) {
        return DHCP_ERR_INVALID;
    }

    /* Convert from network to host byte order */
    ip_host = ntohl(lease->ip_addr);
    mask_host = ntohl(lease->subnet_mask);
    gw_host = ntohl(lease->gateway);

    kprintf("DHCP: Configuring network interface:\n");
    kprintf("  IP Address:  %u.%u.%u.%u\n",
            (ip_host >> 24) & 0xFF, (ip_host >> 16) & 0xFF,
            (ip_host >> 8) & 0xFF, ip_host & 0xFF);
    kprintf("  Subnet Mask: %u.%u.%u.%u\n",
            (mask_host >> 24) & 0xFF, (mask_host >> 16) & 0xFF,
            (mask_host >> 8) & 0xFF, mask_host & 0xFF);
    kprintf("  Gateway:     %u.%u.%u.%u\n",
            (gw_host >> 24) & 0xFF, (gw_host >> 16) & 0xFF,
            (gw_host >> 8) & 0xFF, gw_host & 0xFF);
    kprintf("  Lease Time:  %u seconds\n", lease->lease_time);

    /* Apply to IP layer */
    ip_set_addr(ip_host);
    ip_set_netmask(mask_host);
    ip_set_gateway(gw_host);

    return DHCP_OK;
}

/**
 * Process incoming DHCP packet
 */
int dhcp_input(const void *packet, size_t len) {
    const dhcp_packet_t *dhcp = (const dhcp_packet_t *)packet;
    uint8_t msg_type = 0;
    dhcp_lease_t lease;
    int ret;

    if (!g_dhcp_client.initialized) {
        return DHCP_ERR_NOT_INIT;
    }

    /* Validate packet size */
    if (len < DHCP_PACKET_MIN_SIZE) {
        kprintf("DHCP: Packet too short (%zu bytes)\n", len);
        return DHCP_ERR_INVALID;
    }

    /* Check if this is a reply */
    if (dhcp->op != DHCP_OP_REPLY) {
        return DHCP_ERR_INVALID;
    }

    /* Check transaction ID */
    if (ntohl(dhcp->xid) != g_dhcp_client.xid) {
        kprintf("DHCP: XID mismatch (got 0x%08x, expected 0x%08x)\n",
                ntohl(dhcp->xid), g_dhcp_client.xid);
        return DHCP_ERR_INVALID;
    }

    /* Verify hardware address matches ours */
    if (memcmp(dhcp->chaddr, g_dhcp_client.mac, 6) != 0) {
        kprintf("DHCP: MAC address mismatch\n");
        return DHCP_ERR_INVALID;
    }

    /* Initialize lease structure */
    memset(&lease, 0, sizeof(lease));

    /* Parse options */
    size_t opt_len = len - DHCP_PACKET_MIN_SIZE;
    if (opt_len > DHCP_OPTIONS_MAX_SIZE) {
        opt_len = DHCP_OPTIONS_MAX_SIZE;
    }
    ret = dhcp_parse_options(dhcp->options, opt_len + 4, &lease, &msg_type);
    if (ret < 0) {
        return ret;
    }

    kprintf("DHCP: Received message type %d in state %s\n",
            msg_type, dhcp_state_string(g_dhcp_client.state));

    /* Handle message based on type and current state */
    switch (msg_type) {
        case DHCP_MSG_OFFER:
            if (g_dhcp_client.state == DHCP_STATE_SELECTING) {
                kprintf("DHCP: Received OFFER for IP %u.%u.%u.%u\n",
                        (ntohl(dhcp->yiaddr) >> 24) & 0xFF,
                        (ntohl(dhcp->yiaddr) >> 16) & 0xFF,
                        (ntohl(dhcp->yiaddr) >> 8) & 0xFF,
                        ntohl(dhcp->yiaddr) & 0xFF);

                /* Store offered IP and server IP */
                lease.ip_addr = dhcp->yiaddr;

                /* Send REQUEST for this offer */
                ret = dhcp_request(dhcp->yiaddr, lease.server_ip);
                if (ret < 0) {
                    kprintf("DHCP: Failed to send REQUEST\n");
                    return ret;
                }
            }
            break;

        case DHCP_MSG_ACK:
            if (g_dhcp_client.state == DHCP_STATE_REQUESTING ||
                g_dhcp_client.state == DHCP_STATE_RENEWING ||
                g_dhcp_client.state == DHCP_STATE_REBINDING ||
                g_dhcp_client.state == DHCP_STATE_REBOOTING) {

                kprintf("DHCP: Received ACK\n");

                /* Fill in lease information */
                lease.ip_addr = dhcp->yiaddr;
                lease.obtained_time = timer_get_ticks();
                lease.valid = true;

                /* Set default renewal/rebind times if not provided */
                if (lease.lease_time == 0) {
                    lease.lease_time = DHCP_DEFAULT_LEASE;
                }
                if (lease.renewal_time == 0) {
                    lease.renewal_time = lease.lease_time / 2;
                }
                if (lease.rebind_time == 0) {
                    lease.rebind_time = (lease.lease_time * 7) / 8;
                }

                /* Store the lease */
                memcpy(&g_dhcp_client.lease, &lease, sizeof(lease));
                g_dhcp_client.state = DHCP_STATE_BOUND;

                /* Apply to network interface */
                dhcp_apply_lease(&g_dhcp_client.lease);

                kprintf("DHCP: Lease acquired successfully\n");
            }
            break;

        case DHCP_MSG_NAK:
            kprintf("DHCP: Received NAK from server\n");

            /* Clear any previous lease */
            memset(&g_dhcp_client.lease, 0, sizeof(g_dhcp_client.lease));
            g_dhcp_client.state = DHCP_STATE_INIT;

            /* Reset network interface */
            ip_set_addr(0);
            ip_set_netmask(0);
            ip_set_gateway(0);

            return DHCP_ERR_NAK;

        default:
            kprintf("DHCP: Unexpected message type %d\n", msg_type);
            break;
    }

    return DHCP_OK;
}

/**
 * Get current DHCP client state
 */
dhcp_state_t dhcp_get_state(void) {
    return g_dhcp_client.state;
}

/**
 * Check if network is configured via DHCP
 */
bool dhcp_is_configured(void) {
    return g_dhcp_client.initialized &&
           g_dhcp_client.state == DHCP_STATE_BOUND &&
           g_dhcp_client.lease.valid;
}

/**
 * DHCP timer tick handler
 */
void dhcp_timer_tick(void) {
    uint64_t now;
    uint64_t lease_elapsed;

    if (!g_dhcp_client.initialized) {
        return;
    }

    now = timer_get_ticks();

    switch (g_dhcp_client.state) {
        case DHCP_STATE_SELECTING:
        case DHCP_STATE_REQUESTING:
        case DHCP_STATE_RENEWING:
        case DHCP_STATE_REBINDING:
            /* Check for timeout */
            if (now >= g_dhcp_client.timeout_time) {
                g_dhcp_client.retries++;
                if (g_dhcp_client.retries >= DHCP_MAX_RETRIES) {
                    kprintf("DHCP: Max retries exceeded, restarting\n");
                    g_dhcp_client.state = DHCP_STATE_INIT;

                    /* If we were renewing/rebinding, we might still have a valid lease */
                    if (g_dhcp_client.lease.valid) {
                        g_dhcp_client.state = DHCP_STATE_REBINDING;
                    }
                } else {
                    /* Retry current operation */
                    kprintf("DHCP: Timeout, retry %d\n", g_dhcp_client.retries);
                    if (g_dhcp_client.state == DHCP_STATE_SELECTING) {
                        dhcp_discover();
                    } else if (g_dhcp_client.state == DHCP_STATE_REQUESTING ||
                               g_dhcp_client.state == DHCP_STATE_RENEWING ||
                               g_dhcp_client.state == DHCP_STATE_REBINDING) {
                        dhcp_request(g_dhcp_client.lease.ip_addr,
                                     g_dhcp_client.lease.server_ip);
                    }
                }
            }
            break;

        case DHCP_STATE_BOUND:
            /* Check if we need to renew or rebind */
            if (g_dhcp_client.lease.valid) {
                lease_elapsed = (now - g_dhcp_client.lease.obtained_time) / 1000;

                if (lease_elapsed >= g_dhcp_client.lease.lease_time) {
                    /* Lease expired! */
                    kprintf("DHCP: Lease expired\n");
                    memset(&g_dhcp_client.lease, 0, sizeof(g_dhcp_client.lease));
                    g_dhcp_client.state = DHCP_STATE_INIT;
                    ip_set_addr(0);
                    ip_set_netmask(0);
                    ip_set_gateway(0);
                } else if (lease_elapsed >= g_dhcp_client.lease.rebind_time) {
                    /* Time to rebind (broadcast) */
                    kprintf("DHCP: Rebind time reached\n");
                    g_dhcp_client.state = DHCP_STATE_REBINDING;
                    g_dhcp_client.retries = 0;
                    dhcp_request(g_dhcp_client.lease.ip_addr,
                                 g_dhcp_client.lease.server_ip);
                } else if (lease_elapsed >= g_dhcp_client.lease.renewal_time) {
                    /* Time to renew (unicast to server) */
                    kprintf("DHCP: Renewal time reached\n");
                    dhcp_renew();
                }
            }
            break;

        default:
            break;
    }
}

/**
 * Perform full DHCP configuration (blocking)
 */
int dhcp_configure(uint32_t timeout_ms) {
    uint64_t start_time;
    uint64_t deadline;
    int ret;
    uint8_t recv_buf[DHCP_PACKET_MAX_SIZE];
    ssize_t recv_len;
    uint32_t src_ip;
    uint16_t src_port;

    if (!g_dhcp_client.initialized) {
        ret = dhcp_init();
        if (ret < 0) {
            return ret;
        }
    }

    kprintf("DHCP: Starting configuration (timeout=%u ms)\n", timeout_ms);

    start_time = timer_get_ticks();
    deadline = start_time + timeout_ms;

    /* Start discovery */
    ret = dhcp_discover();
    if (ret < 0) {
        return ret;
    }

    /* Wait for completion or timeout */
    while (timer_get_ticks() < deadline) {
        /* Check for incoming packets */
        recv_len = udp_recvfrom(g_dhcp_socket, recv_buf, sizeof(recv_buf),
                                &src_ip, &src_port);
        if (recv_len > 0) {
            /* Process the received packet */
            dhcp_input(recv_buf, recv_len);
        }

        /* Check if we're done */
        if (g_dhcp_client.state == DHCP_STATE_BOUND) {
            kprintf("DHCP: Configuration complete\n");
            return DHCP_OK;
        }

        /* Handle timeouts and retries */
        dhcp_timer_tick();

        /* If we went back to INIT state, restart discovery */
        if (g_dhcp_client.state == DHCP_STATE_INIT) {
            ret = dhcp_discover();
            if (ret < 0) {
                return ret;
            }
        }

        /* Small delay to avoid busy-waiting */
        timer_sleep_ms(100);
    }

    kprintf("DHCP: Configuration timed out\n");
    return DHCP_ERR_TIMEOUT;
}

/**
 * Debug: Dump DHCP packet contents
 */
void dhcp_dump_packet(const dhcp_packet_t *packet, size_t len) {
    kprintf("DHCP Packet Dump (%zu bytes):\n", len);
    kprintf("  op:     %u (%s)\n", packet->op,
            packet->op == DHCP_OP_REQUEST ? "REQUEST" : "REPLY");
    kprintf("  htype:  %u\n", packet->htype);
    kprintf("  hlen:   %u\n", packet->hlen);
    kprintf("  hops:   %u\n", packet->hops);
    kprintf("  xid:    0x%08x\n", ntohl(packet->xid));
    kprintf("  secs:   %u\n", ntohs(packet->secs));
    kprintf("  flags:  0x%04x\n", ntohs(packet->flags));

    kprintf("  ciaddr: %u.%u.%u.%u\n",
            (ntohl(packet->ciaddr) >> 24) & 0xFF,
            (ntohl(packet->ciaddr) >> 16) & 0xFF,
            (ntohl(packet->ciaddr) >> 8) & 0xFF,
            ntohl(packet->ciaddr) & 0xFF);
    kprintf("  yiaddr: %u.%u.%u.%u\n",
            (ntohl(packet->yiaddr) >> 24) & 0xFF,
            (ntohl(packet->yiaddr) >> 16) & 0xFF,
            (ntohl(packet->yiaddr) >> 8) & 0xFF,
            ntohl(packet->yiaddr) & 0xFF);
    kprintf("  siaddr: %u.%u.%u.%u\n",
            (ntohl(packet->siaddr) >> 24) & 0xFF,
            (ntohl(packet->siaddr) >> 16) & 0xFF,
            (ntohl(packet->siaddr) >> 8) & 0xFF,
            ntohl(packet->siaddr) & 0xFF);
    kprintf("  giaddr: %u.%u.%u.%u\n",
            (ntohl(packet->giaddr) >> 24) & 0xFF,
            (ntohl(packet->giaddr) >> 16) & 0xFF,
            (ntohl(packet->giaddr) >> 8) & 0xFF,
            ntohl(packet->giaddr) & 0xFF);

    kprintf("  chaddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
            packet->chaddr[0], packet->chaddr[1],
            packet->chaddr[2], packet->chaddr[3],
            packet->chaddr[4], packet->chaddr[5]);
}
