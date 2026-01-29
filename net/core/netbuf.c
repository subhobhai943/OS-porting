/**
 * AAAos Network Stack - Network Buffer Implementation
 */

#include "netbuf.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"
#include "../../kernel/mm/pmm.h"

/* Simple memory allocation for network buffers */
/* In a full implementation, this would use a slab allocator */

/**
 * Simple aligned memory allocation for network buffers
 * Uses the physical memory manager for now
 */
static void *net_malloc(size_t size) {
    /* Align to page size for simplicity */
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    physaddr_t addr = pmm_alloc_pages(pages);
    if (addr == 0) {
        return NULL;
    }
    /* In a real system, we would map this to virtual address */
    /* For now, assume identity mapping or higher-half kernel */
    return (void *)addr;
}

static void net_free(void *ptr, size_t size) {
    if (ptr == NULL) return;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    pmm_free_pages((physaddr_t)ptr, pages);
}

netbuf_t *netbuf_alloc(size_t size, size_t headroom) {
    netbuf_t *buf;

    /* Validate parameters */
    if (size == 0 || size > NETBUF_MAX_SIZE) {
        kprintf("[NETBUF] alloc failed: invalid size %u\n", (uint32_t)size);
        return NULL;
    }

    if (headroom >= size) {
        kprintf("[NETBUF] alloc failed: headroom %u >= size %u\n",
                (uint32_t)headroom, (uint32_t)size);
        return NULL;
    }

    /* Allocate netbuf structure */
    buf = (netbuf_t *)net_malloc(sizeof(netbuf_t));
    if (buf == NULL) {
        kprintf("[NETBUF] alloc failed: out of memory for structure\n");
        return NULL;
    }

    /* Initialize structure */
    memset(buf, 0, sizeof(netbuf_t));

    /* Allocate data buffer */
    buf->buffer_start = (uint8_t *)net_malloc(size);
    if (buf->buffer_start == NULL) {
        kprintf("[NETBUF] alloc failed: out of memory for buffer\n");
        net_free(buf, sizeof(netbuf_t));
        return NULL;
    }

    /* Set up buffer pointers */
    buf->capacity = size;
    buf->data = buf->buffer_start + headroom;
    buf->len = 0;
    buf->next = NULL;

    return buf;
}

void netbuf_free(netbuf_t *buf) {
    if (buf == NULL) {
        return;
    }

    /* Free data buffer */
    if (buf->buffer_start != NULL) {
        net_free(buf->buffer_start, buf->capacity);
    }

    /* Free structure */
    net_free(buf, sizeof(netbuf_t));
}

void *netbuf_push(netbuf_t *buf, size_t len) {
    if (buf == NULL) {
        return NULL;
    }

    /* Check if we have enough headroom */
    size_t headroom = netbuf_headroom(buf);
    if (len > headroom) {
        kprintf("[NETBUF] push failed: need %u bytes, only %u headroom\n",
                (uint32_t)len, (uint32_t)headroom);
        return NULL;
    }

    /* Move data pointer back and increase length */
    buf->data -= len;
    buf->len += len;

    return buf->data;
}

void *netbuf_pull(netbuf_t *buf, size_t len) {
    if (buf == NULL) {
        return NULL;
    }

    /* Check if we have enough data */
    if (len > buf->len) {
        kprintf("[NETBUF] pull failed: need %u bytes, only %u available\n",
                (uint32_t)len, (uint32_t)buf->len);
        return NULL;
    }

    /* Move data pointer forward and decrease length */
    buf->data += len;
    buf->len -= len;

    return buf->data;
}

void *netbuf_put(netbuf_t *buf, size_t len) {
    if (buf == NULL) {
        return NULL;
    }

    /* Check if we have enough tailroom */
    size_t tailroom = netbuf_tailroom(buf);
    if (len > tailroom) {
        kprintf("[NETBUF] put failed: need %u bytes, only %u tailroom\n",
                (uint32_t)len, (uint32_t)tailroom);
        return NULL;
    }

    /* Get pointer to current end */
    void *ptr = buf->data + buf->len;

    /* Increase length */
    buf->len += len;

    return ptr;
}

int netbuf_trim(netbuf_t *buf, size_t len) {
    if (buf == NULL) {
        return -1;
    }

    if (len > buf->len) {
        kprintf("[NETBUF] trim failed: need %u bytes, only %u available\n",
                (uint32_t)len, (uint32_t)buf->len);
        return -1;
    }

    buf->len -= len;
    return 0;
}

netbuf_t *netbuf_clone(const netbuf_t *buf) {
    if (buf == NULL) {
        return NULL;
    }

    /* Calculate current headroom */
    size_t headroom = netbuf_headroom(buf);

    /* Allocate new buffer with same parameters */
    netbuf_t *clone = netbuf_alloc(buf->capacity, headroom);
    if (clone == NULL) {
        return NULL;
    }

    /* Copy data */
    memcpy(clone->data, buf->data, buf->len);
    clone->len = buf->len;

    /* Copy metadata */
    clone->protocol = buf->protocol;
    clone->flags = buf->flags;
    memcpy(clone->src_mac, buf->src_mac, 6);
    memcpy(clone->dst_mac, buf->dst_mac, 6);
    clone->src_ip = buf->src_ip;
    clone->dst_ip = buf->dst_ip;

    return clone;
}

void netbuf_reset(netbuf_t *buf, size_t headroom) {
    if (buf == NULL) {
        return;
    }

    if (headroom >= buf->capacity) {
        headroom = NETBUF_DEFAULT_HEADROOM;
    }

    buf->data = buf->buffer_start + headroom;
    buf->len = 0;
    buf->protocol = 0;
    buf->flags = 0;
    buf->next = NULL;
    memset(buf->src_mac, 0, 6);
    memset(buf->dst_mac, 0, 6);
    buf->src_ip = 0;
    buf->dst_ip = 0;
}

int netbuf_copy_in(netbuf_t *buf, const void *data, size_t len) {
    if (buf == NULL || data == NULL) {
        return -1;
    }

    /* Check if we have enough space */
    size_t tailroom = netbuf_tailroom(buf);
    if (len > tailroom) {
        kprintf("[NETBUF] copy_in failed: need %u bytes, only %u tailroom\n",
                (uint32_t)len, (uint32_t)tailroom);
        return -1;
    }

    /* Copy data */
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;

    return 0;
}
