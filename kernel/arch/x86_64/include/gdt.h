/**
 * AAAos Kernel - Global Descriptor Table (GDT)
 *
 * The GDT defines memory segments and their access rights.
 * In 64-bit mode, segmentation is mostly disabled, but we still
 * need a valid GDT for proper CPU operation.
 */

#ifndef _AAAOS_ARCH_GDT_H
#define _AAAOS_ARCH_GDT_H

#include "../../../include/types.h"

/* GDT segment selectors */
#define GDT_NULL_SEG        0x00
#define GDT_KERNEL_CODE     0x08
#define GDT_KERNEL_DATA     0x10
#define GDT_USER_CODE       0x18
#define GDT_USER_DATA       0x20
#define GDT_TSS             0x28

/* GDT entry count (null + kernel code/data + user code/data + TSS) */
#define GDT_ENTRIES         7

/**
 * GDT entry structure (8 bytes)
 */
typedef struct PACKED {
    uint16_t limit_low;     /* Limit bits 0-15 */
    uint16_t base_low;      /* Base bits 0-15 */
    uint8_t  base_mid;      /* Base bits 16-23 */
    uint8_t  access;        /* Access byte */
    uint8_t  flags_limit;   /* Flags (4 bits) + Limit bits 16-19 */
    uint8_t  base_high;     /* Base bits 24-31 */
} gdt_entry_t;

/**
 * GDT entry for system segments (TSS) - 16 bytes in 64-bit mode
 */
typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid_low;
    uint8_t  access;
    uint8_t  flags_limit;
    uint8_t  base_mid_high;
    uint32_t base_high;
    uint32_t reserved;
} gdt_system_entry_t;

/**
 * GDT descriptor (GDTR)
 */
typedef struct PACKED {
    uint16_t limit;         /* Size of GDT - 1 */
    uint64_t base;          /* Linear address of GDT */
} gdt_descriptor_t;

/**
 * Task State Segment (TSS)
 * Used for stack switching on privilege level changes
 */
typedef struct PACKED {
    uint32_t reserved0;
    uint64_t rsp0;          /* Stack pointer for ring 0 */
    uint64_t rsp1;          /* Stack pointer for ring 1 */
    uint64_t rsp2;          /* Stack pointer for ring 2 */
    uint64_t reserved1;
    uint64_t ist1;          /* Interrupt stack table 1 */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O permission bitmap offset */
} tss_t;

/**
 * Initialize the GDT with default segments
 */
void gdt_init(void);

/**
 * Set the kernel stack pointer in TSS
 * @param rsp0 Stack pointer for ring 0
 */
void gdt_set_kernel_stack(uint64_t rsp0);

/**
 * Get the TSS structure
 * @return Pointer to TSS
 */
tss_t* gdt_get_tss(void);

#endif /* _AAAOS_ARCH_GDT_H */
