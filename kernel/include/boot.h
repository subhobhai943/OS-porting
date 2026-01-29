/**
 * AAAos Kernel - Boot Information Structure
 *
 * This header defines the structure passed from the bootloader to the kernel
 * containing essential system information.
 */

#ifndef _AAAOS_BOOT_H
#define _AAAOS_BOOT_H

#include "types.h"

/* Boot magic number - identifies valid boot info */
#define BOOT_MAGIC 0xAAAB007

/* Memory map entry types (E820 compatible) */
#define MEMORY_TYPE_USABLE      1
#define MEMORY_TYPE_RESERVED    2
#define MEMORY_TYPE_ACPI_RECL   3
#define MEMORY_TYPE_ACPI_NVS    4
#define MEMORY_TYPE_BAD         5

/**
 * Memory map entry (E820 format)
 */
typedef struct PACKED {
    uint64_t base;          /* Start address */
    uint64_t length;        /* Size in bytes */
    uint32_t type;          /* Memory type */
    uint32_t acpi_attrs;    /* ACPI extended attributes */
} memory_map_entry_t;

/**
 * Boot information structure
 * Passed from bootloader to kernel in RDI register
 */
typedef struct PACKED {
    uint32_t magic;             /* Boot magic number */
    uint32_t reserved;          /* Padding */
    uint64_t mem_map_addr;      /* Physical address of memory map */
    uint64_t mem_map_count;     /* Number of memory map entries */
    uint64_t framebuffer;       /* Framebuffer address */
    uint32_t fb_width;          /* Screen width (chars or pixels) */
    uint32_t fb_height;         /* Screen height */
    uint32_t fb_bpp;            /* Bits per pixel */
    uint32_t fb_pitch;          /* Bytes per line */
} boot_info_t;

/**
 * Validate boot information structure
 * @param info Pointer to boot info
 * @return true if valid, false otherwise
 */
static inline bool boot_info_valid(const boot_info_t *info) {
    return info != NULL && info->magic == BOOT_MAGIC;
}

#endif /* _AAAOS_BOOT_H */
