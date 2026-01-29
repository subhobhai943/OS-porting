/**
 * AAAos Network Stack - DNS Resolver
 *
 * Provides DNS resolution services for the network stack.
 * Supports A record (IPv4) lookups with result caching.
 */

#ifndef _AAAOS_NET_DNS_H
#define _AAAOS_NET_DNS_H

#include "../../kernel/include/types.h"

/* DNS constants */
#define DNS_PORT                53
#define DNS_MAX_NAME_LEN        255
#define DNS_MAX_LABEL_LEN       63
#define DNS_HEADER_SIZE         12
#define DNS_MAX_PACKET_SIZE     512

/* DNS cache settings */
#define DNS_CACHE_SIZE          32
#define DNS_DEFAULT_TTL         300     /* 5 minutes default TTL */
#define DNS_MIN_TTL             60      /* Minimum TTL (1 minute) */
#define DNS_MAX_TTL             86400   /* Maximum TTL (24 hours) */

/* Default DNS server (Google DNS: 8.8.8.8) */
#define DNS_DEFAULT_SERVER      0x08080808

/* DNS query/response types */
#define DNS_TYPE_A              1       /* IPv4 address */
#define DNS_TYPE_NS             2       /* Name server */
#define DNS_TYPE_CNAME          5       /* Canonical name */
#define DNS_TYPE_SOA            6       /* Start of authority */
#define DNS_TYPE_PTR            12      /* Pointer record */
#define DNS_TYPE_MX             15      /* Mail exchange */
#define DNS_TYPE_TXT            16      /* Text record */
#define DNS_TYPE_AAAA           28      /* IPv6 address */
#define DNS_TYPE_SRV            33      /* Service record */

/* DNS query classes */
#define DNS_CLASS_IN            1       /* Internet */
#define DNS_CLASS_CS            2       /* CSNET (obsolete) */
#define DNS_CLASS_CH            3       /* CHAOS */
#define DNS_CLASS_HS            4       /* Hesiod */

/* DNS header flags */
#define DNS_FLAG_QR             0x8000  /* Query/Response (0=query, 1=response) */
#define DNS_FLAG_OPCODE_MASK    0x7800  /* Operation code */
#define DNS_FLAG_AA             0x0400  /* Authoritative Answer */
#define DNS_FLAG_TC             0x0200  /* Truncated */
#define DNS_FLAG_RD             0x0100  /* Recursion Desired */
#define DNS_FLAG_RA             0x0080  /* Recursion Available */
#define DNS_FLAG_Z_MASK         0x0070  /* Reserved (must be zero) */
#define DNS_FLAG_RCODE_MASK     0x000F  /* Response code */

/* DNS response codes */
#define DNS_RCODE_OK            0       /* No error */
#define DNS_RCODE_FORMAT_ERR    1       /* Format error */
#define DNS_RCODE_SERVER_FAIL   2       /* Server failure */
#define DNS_RCODE_NAME_ERR      3       /* Name error (NXDOMAIN) */
#define DNS_RCODE_NOT_IMPL      4       /* Not implemented */
#define DNS_RCODE_REFUSED       5       /* Refused */

/* DNS error codes */
#define DNS_OK                  0
#define DNS_ERR_INVALID_NAME    -1
#define DNS_ERR_NETWORK         -2
#define DNS_ERR_TIMEOUT         -3
#define DNS_ERR_NO_MEMORY       -4
#define DNS_ERR_FORMAT          -5
#define DNS_ERR_SERVER          -6
#define DNS_ERR_NOT_FOUND       -7
#define DNS_ERR_TRUNCATED       -8

/**
 * DNS header structure (12 bytes)
 *
 * Wire format (big-endian):
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |                      ID                       |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |                    QDCOUNT                    |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |                    ANCOUNT                    |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |                    NSCOUNT                    |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   |                    ARCOUNT                    |
 *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */
typedef struct dns_header {
    uint16_t id;            /* Identification number */
    uint16_t flags;         /* Flags and codes */
    uint16_t qdcount;       /* Number of questions */
    uint16_t ancount;       /* Number of answer RRs */
    uint16_t nscount;       /* Number of authority RRs */
    uint16_t arcount;       /* Number of additional RRs */
} PACKED dns_header_t;

/**
 * DNS question structure
 *
 * Note: QNAME is variable length and precedes this structure
 *       QNAME is not included here due to variable size
 */
typedef struct dns_question {
    uint16_t qtype;         /* Question type (e.g., DNS_TYPE_A) */
    uint16_t qclass;        /* Question class (e.g., DNS_CLASS_IN) */
} PACKED dns_question_t;

/**
 * DNS answer/resource record structure
 *
 * Wire format:
 *   NAME (variable, may use compression)
 *   TYPE (2 bytes)
 *   CLASS (2 bytes)
 *   TTL (4 bytes)
 *   RDLENGTH (2 bytes)
 *   RDATA (variable, RDLENGTH bytes)
 */
typedef struct dns_answer {
    uint16_t type;          /* Record type */
    uint16_t class;         /* Record class */
    uint32_t ttl;           /* Time to live (seconds) */
    uint16_t rdlength;      /* Length of RDATA */
    /* RDATA follows (not included due to variable size) */
} PACKED dns_answer_t;

/**
 * DNS cache entry structure
 */
typedef struct dns_cache_entry {
    char     name[DNS_MAX_NAME_LEN + 1];    /* Hostname (null-terminated) */
    uint32_t ip;                             /* Resolved IPv4 address */
    uint32_t ttl;                            /* Time to live (seconds) */
    uint64_t timestamp;                      /* When entry was added (ticks) */
    bool     valid;                          /* Entry is valid */
} dns_cache_entry_t;

/**
 * DNS resolver state
 */
typedef struct dns_resolver {
    uint32_t server_ip;                      /* DNS server IP address */
    uint16_t next_id;                        /* Next query ID */
    dns_cache_entry_t cache[DNS_CACHE_SIZE]; /* DNS cache */
    bool     initialized;                    /* Resolver initialized */
} dns_resolver_t;

/*
 * DNS Resolver Functions
 */

/**
 * Initialize the DNS resolver
 *
 * Sets up the DNS resolver with default settings (Google DNS 8.8.8.8).
 * Must be called before any DNS operations.
 *
 * @return DNS_OK on success, negative error code on failure
 */
int dns_init(void);

/**
 * Set the DNS server address
 *
 * @param server_ip DNS server IPv4 address (network byte order)
 * @return DNS_OK on success, negative error code on failure
 */
int dns_set_server(uint32_t server_ip);

/**
 * Get the current DNS server address
 *
 * @return DNS server IPv4 address (network byte order)
 */
uint32_t dns_get_server(void);

/**
 * Resolve a hostname to an IPv4 address
 *
 * First checks the cache, then sends a DNS query if needed.
 *
 * @param hostname The hostname to resolve
 * @param ip_out   Pointer to store the resolved IP address
 * @return DNS_OK on success, negative error code on failure
 */
int dns_resolve(const char *hostname, uint32_t *ip_out);

/**
 * Look up a hostname in the DNS cache
 *
 * @param hostname The hostname to look up
 * @param ip_out   Pointer to store the cached IP address
 * @return DNS_OK if found in cache, DNS_ERR_NOT_FOUND if not cached
 */
int dns_cache_lookup(const char *hostname, uint32_t *ip_out);

/**
 * Add an entry to the DNS cache
 *
 * @param hostname The hostname
 * @param ip       The resolved IP address
 * @param ttl      Time to live in seconds
 * @return DNS_OK on success, negative error code on failure
 */
int dns_cache_add(const char *hostname, uint32_t ip, uint32_t ttl);

/**
 * Clear the DNS cache
 */
void dns_cache_clear(void);

/**
 * Build a DNS query packet
 *
 * Constructs a DNS query for an A record lookup.
 *
 * @param hostname The hostname to query
 * @param buf      Buffer to store the query packet (must be >= DNS_MAX_PACKET_SIZE)
 * @return Size of the query packet on success, negative error code on failure
 */
int dns_build_query(const char *hostname, void *buf);

/**
 * Parse a DNS response packet
 *
 * Extracts the first A record from the response.
 *
 * @param response The response packet
 * @param len      Length of the response
 * @param ip_out   Pointer to store the resolved IP address
 * @param ttl_out  Pointer to store the TTL (can be NULL)
 * @return DNS_OK on success, negative error code on failure
 */
int dns_parse_response(const void *response, size_t len, uint32_t *ip_out, uint32_t *ttl_out);

/*
 * Utility Functions
 */

/**
 * Encode a hostname to DNS wire format (length-prefixed labels)
 *
 * Example: "www.google.com" -> "\x03www\x06google\x03com\x00"
 *
 * @param hostname The hostname to encode
 * @param buf      Buffer to store the encoded name
 * @param bufsize  Size of the buffer
 * @return Length of encoded name on success, negative error code on failure
 */
int dns_encode_name(const char *hostname, uint8_t *buf, size_t bufsize);

/**
 * Decode a DNS name from wire format
 *
 * Handles label compression (pointers).
 *
 * @param packet   Start of the DNS packet (for compression)
 * @param pkt_len  Total packet length
 * @param data     Pointer to the name in the packet
 * @param name_out Buffer to store the decoded name
 * @param name_size Size of the name buffer
 * @return Number of bytes consumed from data, negative error code on failure
 */
int dns_decode_name(const uint8_t *packet, size_t pkt_len, const uint8_t *data,
                    char *name_out, size_t name_size);

/**
 * Validate a hostname
 *
 * @param hostname The hostname to validate
 * @return true if valid, false otherwise
 */
bool dns_validate_hostname(const char *hostname);

/**
 * Convert an IP address to string format (dotted decimal)
 *
 * @param ip   IP address (network byte order)
 * @param buf  Buffer to store the string (at least 16 bytes)
 * @param size Buffer size
 * @return Pointer to buf on success, NULL on failure
 */
char *dns_ip_to_string(uint32_t ip, char *buf, size_t size);

/**
 * Convert a string to IP address
 *
 * @param str    IP address string (dotted decimal)
 * @param ip_out Pointer to store the IP address (network byte order)
 * @return DNS_OK on success, negative error code on failure
 */
int dns_string_to_ip(const char *str, uint32_t *ip_out);

#endif /* _AAAOS_NET_DNS_H */
