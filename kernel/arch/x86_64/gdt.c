/**
 * AAAos Kernel - GDT Implementation
 */

#include "include/gdt.h"
#include "../../include/serial.h"

/* GDT entries */
static gdt_entry_t gdt[GDT_ENTRIES] ALIGNED(16);

/* TSS */
static tss_t tss ALIGNED(16);

/* GDT descriptor */
static gdt_descriptor_t gdt_descriptor;

/* Access byte flags */
#define GDT_PRESENT     (1 << 7)    /* Segment present */
#define GDT_DPL_RING0   (0 << 5)    /* Privilege level 0 */
#define GDT_DPL_RING3   (3 << 5)    /* Privilege level 3 */
#define GDT_SYSTEM      (0 << 4)    /* System segment */
#define GDT_CODE_DATA   (1 << 4)    /* Code/Data segment */
#define GDT_EXECUTABLE  (1 << 3)    /* Executable (code segment) */
#define GDT_DC          (1 << 2)    /* Direction/Conforming */
#define GDT_RW          (1 << 1)    /* Readable (code) / Writable (data) */
#define GDT_ACCESSED    (1 << 0)    /* Accessed */

/* Flags */
#define GDT_GRANULARITY (1 << 7)    /* 4KB granularity */
#define GDT_SIZE_32     (1 << 6)    /* 32-bit segment */
#define GDT_LONG_MODE   (1 << 5)    /* 64-bit code segment */

/* TSS type */
#define TSS_TYPE_AVAILABLE  0x9

/* Assembly function to load GDT */
extern void gdt_load(gdt_descriptor_t *descriptor, uint16_t code_seg, uint16_t data_seg);

/**
 * Set a GDT entry
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;

    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].flags_limit = ((limit >> 16) & 0x0F) | (flags & 0xF0);

    gdt[index].access = access;
}

/**
 * Set TSS entry in GDT (16 bytes for 64-bit mode)
 */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    gdt_system_entry_t *entry = (gdt_system_entry_t*)&gdt[index];

    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_mid_low = (base >> 16) & 0xFF;
    entry->access = GDT_PRESENT | GDT_SYSTEM | TSS_TYPE_AVAILABLE;
    entry->flags_limit = ((limit >> 16) & 0x0F);
    entry->base_mid_high = (base >> 24) & 0xFF;
    entry->base_high = (base >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}

/**
 * Initialize the GDT
 */
void gdt_init(void) {
    kprintf("[GDT] Initializing Global Descriptor Table...\n");

    /* Clear GDT */
    for (int i = 0; i < GDT_ENTRIES; i++) {
        gdt[i] = (gdt_entry_t){0};
    }

    /* Null segment (0x00) */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Kernel code segment (0x08) - 64-bit */
    gdt_set_entry(1, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL_RING0 | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_RW,
                  GDT_LONG_MODE | GDT_GRANULARITY);

    /* Kernel data segment (0x10) */
    gdt_set_entry(2, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL_RING0 | GDT_CODE_DATA | GDT_RW,
                  GDT_GRANULARITY);

    /* User code segment (0x18) - 64-bit */
    gdt_set_entry(3, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL_RING3 | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_RW,
                  GDT_LONG_MODE | GDT_GRANULARITY);

    /* User data segment (0x20) */
    gdt_set_entry(4, 0, 0xFFFFF,
                  GDT_PRESENT | GDT_DPL_RING3 | GDT_CODE_DATA | GDT_RW,
                  GDT_GRANULARITY);

    /* Initialize TSS */
    tss = (tss_t){0};
    tss.iopb_offset = sizeof(tss_t);

    /* TSS segment (0x28) - takes two GDT slots in 64-bit mode */
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t) - 1);

    /* Set up GDT descriptor */
    gdt_descriptor.limit = sizeof(gdt) - 1;
    gdt_descriptor.base = (uint64_t)&gdt;

    /* Load GDT */
    gdt_load(&gdt_descriptor, GDT_KERNEL_CODE, GDT_KERNEL_DATA);

    /* Load TSS */
    __asm__ __volatile__(
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        : : : "ax"
    );

    kprintf("[GDT] GDT loaded at %p, %d bytes\n", (void*)gdt_descriptor.base, gdt_descriptor.limit + 1);
    kprintf("[GDT] TSS loaded at %p\n", (void*)&tss);
}

/**
 * Set the kernel stack pointer in TSS
 */
void gdt_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

/**
 * Get the TSS structure
 */
tss_t* gdt_get_tss(void) {
    return &tss;
}
