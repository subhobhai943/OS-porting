/**
 * AAAos Network Stack - DNS Resolver Implementation
 *
 * Implements DNS resolution with caching support.
 * Uses UDP port 53 for queries.
 */

#include "dns.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"
#include "../core/netbuf.h"

/* Global DNS resolver state */
static dns_resolver_t dns_resolver;

/* Simple tick counter for cache expiration (should be replaced with real timer) */
static uint64_t dns_get_ticks(void) {
    /* In a real implementation, this would return system ticks/time */
    /* For now, we use a simple incrementing counter */
    static uint64_t ticks = 0;
    return ticks++;
}

/*
 * Byte order conversion utilities
 * Network byte order is big-endian
 */
static inline uint16_t htons(uint16_t h) {
    return ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t n) {
    return ((n & 0xFF) << 8) | ((n >> 8) & 0xFF);
}

static inline uint32_t htonl(uint32_t h) {
    return ((h & 0xFF) << 24) |
           ((h & 0xFF00) << 8) |
           ((h >> 8) & 0xFF00) |
           ((h >> 24) & 0xFF);
}

static inline uint32_t ntohl(uint32_t n) {
    return ((n & 0xFF) << 24) |
           ((n & 0xFF00) << 8) |
           ((n >> 8) & 0xFF00) |
           ((n >> 24) & 0xFF);
}

/**
 * Generate a random query ID
 * In a real implementation, this should use a proper PRNG
 */
static uint16_t dns_generate_id(void) {
    dns_resolver.next_id++;
    /* Simple LFSR-like mixing */
    uint16_t id = dns_resolver.next_id;
    id ^= (id << 7);
    id ^= (id >> 9);
    id ^= (id << 8);
    return id;
}

/**
 * Case-insensitive string comparison for hostnames
 */
static int dns_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        /* Convert to lowercase */
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int dns_init(void) {
    kprintf("[DNS] Initializing DNS resolver\n");

    /* Clear resolver state */
    memset(&dns_resolver, 0, sizeof(dns_resolver));

    /* Set default DNS server (Google DNS: 8.8.8.8) */
    dns_resolver.server_ip = DNS_DEFAULT_SERVER;

    /* Initialize query ID with some entropy */
    dns_resolver.next_id = (uint16_t)(dns_get_ticks() & 0xFFFF);

    /* Clear cache */
    dns_cache_clear();

    dns_resolver.initialized = true;

    kprintf("[DNS] Resolver initialized, server: 8.8.8.8\n");
    return DNS_OK;
}

int dns_set_server(uint32_t server_ip) {
    if (!dns_resolver.initialized) {
        kprintf("[DNS] Error: resolver not initialized\n");
        return DNS_ERR_INVALID_NAME;
    }

    if (server_ip == 0) {
        kprintf("[DNS] Error: invalid server IP address\n");
        return DNS_ERR_INVALID_NAME;
    }

    dns_resolver.server_ip = server_ip;

    char ip_str[16];
    dns_ip_to_string(server_ip, ip_str, sizeof(ip_str));
    kprintf("[DNS] Server set to %s\n", ip_str);

    return DNS_OK;
}

uint32_t dns_get_server(void) {
    return dns_resolver.server_ip;
}

bool dns_validate_hostname(const char *hostname) {
    if (hostname == NULL || *hostname == '\0') {
        return false;
    }

    size_t len = strlen(hostname);
    if (len > DNS_MAX_NAME_LEN) {
        return false;
    }

    size_t label_len = 0;
    bool prev_was_dot = true;  /* Start as if previous was dot */

    for (size_t i = 0; i < len; i++) {
        char c = hostname[i];

        if (c == '.') {
            /* Check for empty label or consecutive dots */
            if (prev_was_dot && i > 0) {
                return false;
            }
            /* Check label length */
            if (label_len > DNS_MAX_LABEL_LEN) {
                return false;
            }
            label_len = 0;
            prev_was_dot = true;
        } else {
            /* Valid characters: a-z, A-Z, 0-9, hyphen */
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  (c == '-'))) {
                return false;
            }
            /* Labels cannot start or end with hyphen */
            if (c == '-' && (prev_was_dot || hostname[i+1] == '.' || hostname[i+1] == '\0')) {
                return false;
            }
            label_len++;
            prev_was_dot = false;
        }
    }

    /* Check final label length */
    if (label_len > DNS_MAX_LABEL_LEN) {
        return false;
    }

    return true;
}

int dns_encode_name(const char *hostname, uint8_t *buf, size_t bufsize) {
    if (hostname == NULL || buf == NULL || bufsize == 0) {
        return DNS_ERR_INVALID_NAME;
    }

    if (!dns_validate_hostname(hostname)) {
        kprintf("[DNS] Invalid hostname: %s\n", hostname);
        return DNS_ERR_INVALID_NAME;
    }

    size_t hostname_len = strlen(hostname);
    /* Encoded name needs: hostname_len + 1 (leading length) + 1 (null terminator) */
    if (bufsize < hostname_len + 2) {
        return DNS_ERR_NO_MEMORY;
    }

    size_t pos = 0;
    const char *label_start = hostname;

    while (*label_start) {
        /* Find the end of this label */
        const char *label_end = label_start;
        while (*label_end && *label_end != '.') {
            label_end++;
        }

        /* Calculate label length */
        size_t label_len = label_end - label_start;
        if (label_len == 0) {
            /* Skip empty labels (trailing dots) */
            label_start = label_end + 1;
            continue;
        }
        if (label_len > DNS_MAX_LABEL_LEN) {
            return DNS_ERR_INVALID_NAME;
        }

        /* Check buffer space */
        if (pos + label_len + 1 >= bufsize) {
            return DNS_ERR_NO_MEMORY;
        }

        /* Write length prefix */
        buf[pos++] = (uint8_t)label_len;

        /* Write label characters */
        for (size_t i = 0; i < label_len; i++) {
            buf[pos++] = (uint8_t)label_start[i];
        }

        /* Move to next label */
        label_start = (*label_end == '.') ? label_end + 1 : label_end;
    }

    /* Write terminating zero */
    buf[pos++] = 0;

    return (int)pos;
}

int dns_decode_name(const uint8_t *packet, size_t pkt_len, const uint8_t *data,
                    char *name_out, size_t name_size) {
    if (packet == NULL || data == NULL || name_out == NULL || name_size == 0) {
        return DNS_ERR_FORMAT;
    }

    size_t name_pos = 0;
    size_t data_offset = data - packet;
    int bytes_consumed = 0;
    bool jumped = false;
    int jump_count = 0;
    const int max_jumps = 128;  /* Prevent infinite loops */

    while (data_offset < pkt_len) {
        uint8_t len = packet[data_offset];

        /* Check for compression pointer (top 2 bits set) */
        if ((len & 0xC0) == 0xC0) {
            /* Compression pointer */
            if (data_offset + 1 >= pkt_len) {
                return DNS_ERR_FORMAT;
            }

            /* Calculate offset */
            uint16_t offset = ((len & 0x3F) << 8) | packet[data_offset + 1];

            if (!jumped) {
                bytes_consumed = (data_offset - (data - packet)) + 2;
                jumped = true;
            }

            /* Follow the pointer */
            data_offset = offset;

            /* Check for too many jumps (possible attack) */
            if (++jump_count > max_jumps) {
                kprintf("[DNS] Too many compression pointers\n");
                return DNS_ERR_FORMAT;
            }
            continue;
        }

        if (len == 0) {
            /* End of name */
            if (!jumped) {
                bytes_consumed = (data_offset - (data - packet)) + 1;
            }
            break;
        }

        /* Regular label */
        if (len > DNS_MAX_LABEL_LEN) {
            return DNS_ERR_FORMAT;
        }

        data_offset++;

        /* Check bounds */
        if (data_offset + len > pkt_len) {
            return DNS_ERR_FORMAT;
        }

        /* Add dot separator if not first label */
        if (name_pos > 0) {
            if (name_pos >= name_size - 1) {
                return DNS_ERR_FORMAT;
            }
            name_out[name_pos++] = '.';
        }

        /* Copy label */
        for (uint8_t i = 0; i < len; i++) {
            if (name_pos >= name_size - 1) {
                return DNS_ERR_FORMAT;
            }
            name_out[name_pos++] = (char)packet[data_offset + i];
        }

        data_offset += len;
    }

    /* Null terminate */
    name_out[name_pos] = '\0';

    return bytes_consumed;
}

int dns_build_query(const char *hostname, void *buf) {
    if (!dns_resolver.initialized) {
        kprintf("[DNS] Error: resolver not initialized\n");
        return DNS_ERR_INVALID_NAME;
    }

    if (hostname == NULL || buf == NULL) {
        return DNS_ERR_INVALID_NAME;
    }

    kprintf("[DNS] Building query for: %s\n", hostname);

    uint8_t *packet = (uint8_t *)buf;
    size_t offset = 0;

    /* Build DNS header */
    dns_header_t *header = (dns_header_t *)packet;
    header->id = htons(dns_generate_id());
    header->flags = htons(DNS_FLAG_RD);  /* Recursion desired */
    header->qdcount = htons(1);          /* One question */
    header->ancount = htons(0);
    header->nscount = htons(0);
    header->arcount = htons(0);

    offset = DNS_HEADER_SIZE;

    /* Encode hostname (QNAME) */
    int name_len = dns_encode_name(hostname, packet + offset, DNS_MAX_PACKET_SIZE - offset - 4);
    if (name_len < 0) {
        kprintf("[DNS] Failed to encode hostname\n");
        return name_len;
    }
    offset += name_len;

    /* Add question type and class */
    dns_question_t *question = (dns_question_t *)(packet + offset);
    question->qtype = htons(DNS_TYPE_A);
    question->qclass = htons(DNS_CLASS_IN);
    offset += sizeof(dns_question_t);

    kprintf("[DNS] Query built, size: %u bytes, ID: 0x%04x\n",
            (uint32_t)offset, ntohs(header->id));

    return (int)offset;
}

int dns_parse_response(const void *response, size_t len, uint32_t *ip_out, uint32_t *ttl_out) {
    if (response == NULL || ip_out == NULL) {
        return DNS_ERR_FORMAT;
    }

    if (len < DNS_HEADER_SIZE) {
        kprintf("[DNS] Response too short: %u bytes\n", (uint32_t)len);
        return DNS_ERR_FORMAT;
    }

    const uint8_t *packet = (const uint8_t *)response;
    const dns_header_t *header = (const dns_header_t *)packet;

    uint16_t flags = ntohs(header->flags);
    uint16_t id = ntohs(header->id);
    uint16_t qdcount = ntohs(header->qdcount);
    uint16_t ancount = ntohs(header->ancount);

    kprintf("[DNS] Parsing response, ID: 0x%04x, flags: 0x%04x\n", id, flags);

    /* Check QR bit (should be 1 for response) */
    if (!(flags & DNS_FLAG_QR)) {
        kprintf("[DNS] Not a response packet\n");
        return DNS_ERR_FORMAT;
    }

    /* Check for errors */
    uint8_t rcode = flags & DNS_FLAG_RCODE_MASK;
    if (rcode != DNS_RCODE_OK) {
        kprintf("[DNS] Server returned error: %u\n", rcode);
        switch (rcode) {
            case DNS_RCODE_FORMAT_ERR:
                return DNS_ERR_FORMAT;
            case DNS_RCODE_SERVER_FAIL:
                return DNS_ERR_SERVER;
            case DNS_RCODE_NAME_ERR:
                kprintf("[DNS] Name not found (NXDOMAIN)\n");
                return DNS_ERR_NOT_FOUND;
            case DNS_RCODE_NOT_IMPL:
            case DNS_RCODE_REFUSED:
                return DNS_ERR_SERVER;
            default:
                return DNS_ERR_SERVER;
        }
    }

    /* Check for truncation */
    if (flags & DNS_FLAG_TC) {
        kprintf("[DNS] Response was truncated\n");
        /* Continue anyway, we might still get an answer */
    }

    if (ancount == 0) {
        kprintf("[DNS] No answers in response\n");
        return DNS_ERR_NOT_FOUND;
    }

    kprintf("[DNS] Response contains %u questions, %u answers\n", qdcount, ancount);

    /* Skip the question section */
    size_t offset = DNS_HEADER_SIZE;

    for (uint16_t i = 0; i < qdcount; i++) {
        /* Skip QNAME */
        while (offset < len) {
            uint8_t label_len = packet[offset];
            if (label_len == 0) {
                offset++;  /* Skip the null terminator */
                break;
            }
            if ((label_len & 0xC0) == 0xC0) {
                /* Compression pointer */
                offset += 2;
                break;
            }
            offset += label_len + 1;
        }
        /* Skip QTYPE and QCLASS */
        offset += sizeof(dns_question_t);

        if (offset > len) {
            kprintf("[DNS] Malformed question section\n");
            return DNS_ERR_FORMAT;
        }
    }

    /* Parse answer section */
    for (uint16_t i = 0; i < ancount; i++) {
        if (offset >= len) {
            kprintf("[DNS] Truncated answer section\n");
            return DNS_ERR_FORMAT;
        }

        /* Decode name (skip it) */
        char name[DNS_MAX_NAME_LEN + 1];
        int name_bytes = dns_decode_name(packet, len, packet + offset, name, sizeof(name));
        if (name_bytes < 0) {
            kprintf("[DNS] Failed to decode answer name\n");
            return DNS_ERR_FORMAT;
        }
        offset += name_bytes;

        /* Check bounds for answer header */
        if (offset + sizeof(dns_answer_t) > len) {
            kprintf("[DNS] Truncated answer record\n");
            return DNS_ERR_FORMAT;
        }

        /* Read answer header */
        uint16_t atype = ntohs(*(uint16_t *)(packet + offset));
        offset += 2;
        uint16_t aclass = ntohs(*(uint16_t *)(packet + offset));
        offset += 2;
        uint32_t attl = ntohl(*(uint32_t *)(packet + offset));
        offset += 4;
        uint16_t rdlength = ntohs(*(uint16_t *)(packet + offset));
        offset += 2;

        kprintf("[DNS] Answer %u: type=%u, class=%u, ttl=%u, rdlen=%u\n",
                i, atype, aclass, (uint32_t)attl, rdlength);

        /* Check bounds for RDATA */
        if (offset + rdlength > len) {
            kprintf("[DNS] Truncated RDATA\n");
            return DNS_ERR_FORMAT;
        }

        /* Check if this is an A record */
        if (atype == DNS_TYPE_A && aclass == DNS_CLASS_IN && rdlength == 4) {
            /* Found an A record! */
            *ip_out = *(uint32_t *)(packet + offset);
            if (ttl_out != NULL) {
                *ttl_out = attl;
            }

            char ip_str[16];
            dns_ip_to_string(*ip_out, ip_str, sizeof(ip_str));
            kprintf("[DNS] Resolved to %s (TTL: %u)\n", ip_str, (uint32_t)attl);

            return DNS_OK;
        }

        /* Skip RDATA and continue to next record */
        offset += rdlength;
    }

    kprintf("[DNS] No A record found in response\n");
    return DNS_ERR_NOT_FOUND;
}

int dns_cache_lookup(const char *hostname, uint32_t *ip_out) {
    if (!dns_resolver.initialized) {
        return DNS_ERR_INVALID_NAME;
    }

    if (hostname == NULL || ip_out == NULL) {
        return DNS_ERR_INVALID_NAME;
    }

    uint64_t current_time = dns_get_ticks();

    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns_resolver.cache[i];

        if (!entry->valid) {
            continue;
        }

        /* Check if entry has expired */
        uint64_t age = current_time - entry->timestamp;
        if (age > entry->ttl) {
            /* Entry expired, invalidate it */
            entry->valid = false;
            kprintf("[DNS] Cache entry expired: %s\n", entry->name);
            continue;
        }

        /* Case-insensitive comparison */
        if (dns_strcasecmp(entry->name, hostname) == 0) {
            *ip_out = entry->ip;

            char ip_str[16];
            dns_ip_to_string(*ip_out, ip_str, sizeof(ip_str));
            kprintf("[DNS] Cache hit: %s -> %s\n", hostname, ip_str);

            return DNS_OK;
        }
    }

    kprintf("[DNS] Cache miss: %s\n", hostname);
    return DNS_ERR_NOT_FOUND;
}

int dns_cache_add(const char *hostname, uint32_t ip, uint32_t ttl) {
    if (!dns_resolver.initialized) {
        return DNS_ERR_INVALID_NAME;
    }

    if (hostname == NULL) {
        return DNS_ERR_INVALID_NAME;
    }

    size_t hostname_len = strlen(hostname);
    if (hostname_len > DNS_MAX_NAME_LEN) {
        return DNS_ERR_INVALID_NAME;
    }

    /* Clamp TTL to valid range */
    if (ttl < DNS_MIN_TTL) {
        ttl = DNS_MIN_TTL;
    }
    if (ttl > DNS_MAX_TTL) {
        ttl = DNS_MAX_TTL;
    }

    uint64_t current_time = dns_get_ticks();

    /* First, check if entry already exists (update it) */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns_resolver.cache[i];

        if (entry->valid && dns_strcasecmp(entry->name, hostname) == 0) {
            entry->ip = ip;
            entry->ttl = ttl;
            entry->timestamp = current_time;

            char ip_str[16];
            dns_ip_to_string(ip, ip_str, sizeof(ip_str));
            kprintf("[DNS] Cache updated: %s -> %s (TTL: %u)\n", hostname, ip_str, ttl);

            return DNS_OK;
        }
    }

    /* Find an empty slot or the oldest entry */
    int oldest_idx = 0;
    uint64_t oldest_age = 0;

    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_cache_entry_t *entry = &dns_resolver.cache[i];

        if (!entry->valid) {
            /* Found empty slot */
            oldest_idx = i;
            break;
        }

        uint64_t age = current_time - entry->timestamp;
        if (age > oldest_age) {
            oldest_age = age;
            oldest_idx = i;
        }
    }

    /* Add entry */
    dns_cache_entry_t *entry = &dns_resolver.cache[oldest_idx];
    strncpy(entry->name, hostname, DNS_MAX_NAME_LEN);
    entry->name[DNS_MAX_NAME_LEN] = '\0';
    entry->ip = ip;
    entry->ttl = ttl;
    entry->timestamp = current_time;
    entry->valid = true;

    char ip_str[16];
    dns_ip_to_string(ip, ip_str, sizeof(ip_str));
    kprintf("[DNS] Cache add: %s -> %s (TTL: %u, slot: %d)\n",
            hostname, ip_str, ttl, oldest_idx);

    return DNS_OK;
}

void dns_cache_clear(void) {
    kprintf("[DNS] Clearing cache\n");

    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        dns_resolver.cache[i].valid = false;
        dns_resolver.cache[i].name[0] = '\0';
    }
}

/* Stub for actual UDP send/receive - to be implemented with UDP layer */
static int dns_send_query(const void *query, size_t len, void *response, size_t *resp_len) {
    UNUSED(query);
    UNUSED(len);
    UNUSED(response);
    UNUSED(resp_len);

    /*
     * This is a stub function that should be implemented once the UDP layer is available.
     * The implementation should:
     * 1. Create a UDP socket
     * 2. Send the query to dns_resolver.server_ip on port 53
     * 3. Wait for response (with timeout)
     * 4. Copy response to buffer
     * 5. Return the response length
     */

    kprintf("[DNS] UDP transport not yet implemented\n");
    return DNS_ERR_NETWORK;
}

int dns_resolve(const char *hostname, uint32_t *ip_out) {
    if (!dns_resolver.initialized) {
        kprintf("[DNS] Error: resolver not initialized\n");
        return DNS_ERR_INVALID_NAME;
    }

    if (hostname == NULL || ip_out == NULL) {
        return DNS_ERR_INVALID_NAME;
    }

    kprintf("[DNS] Resolving: %s\n", hostname);

    /* First, check if it's already an IP address */
    uint32_t ip;
    if (dns_string_to_ip(hostname, &ip) == DNS_OK) {
        *ip_out = ip;
        kprintf("[DNS] Input is already an IP address\n");
        return DNS_OK;
    }

    /* Check the cache */
    if (dns_cache_lookup(hostname, ip_out) == DNS_OK) {
        return DNS_OK;
    }

    /* Build DNS query */
    uint8_t query_buf[DNS_MAX_PACKET_SIZE];
    int query_len = dns_build_query(hostname, query_buf);
    if (query_len < 0) {
        return query_len;
    }

    /* Send query and receive response */
    uint8_t response_buf[DNS_MAX_PACKET_SIZE];
    size_t response_len = sizeof(response_buf);

    int result = dns_send_query(query_buf, query_len, response_buf, &response_len);
    if (result != DNS_OK) {
        kprintf("[DNS] Failed to send/receive query\n");
        return result;
    }

    /* Parse response */
    uint32_t ttl = DNS_DEFAULT_TTL;
    result = dns_parse_response(response_buf, response_len, ip_out, &ttl);
    if (result != DNS_OK) {
        return result;
    }

    /* Add to cache */
    dns_cache_add(hostname, *ip_out, ttl);

    return DNS_OK;
}

char *dns_ip_to_string(uint32_t ip, char *buf, size_t size) {
    if (buf == NULL || size < 16) {
        return NULL;
    }

    /* IP is in network byte order (big-endian) */
    uint8_t *bytes = (uint8_t *)&ip;

    /* Format: a.b.c.d */
    size_t pos = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t octet = bytes[i];

        /* Convert octet to string */
        if (octet >= 100) {
            buf[pos++] = '0' + (octet / 100);
            octet %= 100;
            buf[pos++] = '0' + (octet / 10);
            buf[pos++] = '0' + (octet % 10);
        } else if (octet >= 10) {
            buf[pos++] = '0' + (octet / 10);
            buf[pos++] = '0' + (octet % 10);
        } else {
            buf[pos++] = '0' + octet;
        }

        /* Add dot separator (except after last octet) */
        if (i < 3) {
            buf[pos++] = '.';
        }
    }

    buf[pos] = '\0';
    return buf;
}

int dns_string_to_ip(const char *str, uint32_t *ip_out) {
    if (str == NULL || ip_out == NULL) {
        return DNS_ERR_INVALID_NAME;
    }

    uint8_t octets[4];
    int octet_idx = 0;
    uint32_t current_value = 0;
    bool has_digit = false;

    while (*str) {
        char c = *str++;

        if (c >= '0' && c <= '9') {
            current_value = current_value * 10 + (c - '0');
            if (current_value > 255) {
                return DNS_ERR_INVALID_NAME;
            }
            has_digit = true;
        } else if (c == '.') {
            if (!has_digit || octet_idx >= 3) {
                return DNS_ERR_INVALID_NAME;
            }
            octets[octet_idx++] = (uint8_t)current_value;
            current_value = 0;
            has_digit = false;
        } else {
            /* Invalid character - not an IP address */
            return DNS_ERR_INVALID_NAME;
        }
    }

    /* Process final octet */
    if (!has_digit || octet_idx != 3) {
        return DNS_ERR_INVALID_NAME;
    }
    octets[octet_idx] = (uint8_t)current_value;

    /* Construct IP in network byte order */
    *ip_out = (octets[0]) | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);

    return DNS_OK;
}
