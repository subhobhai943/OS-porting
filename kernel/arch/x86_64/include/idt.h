/**
 * AAAos Kernel - Interrupt Descriptor Table (IDT)
 *
 * The IDT defines handlers for CPU exceptions and hardware interrupts.
 */

#ifndef _AAAOS_ARCH_IDT_H
#define _AAAOS_ARCH_IDT_H

#include "../../../include/types.h"

/* Number of IDT entries (256 possible interrupt vectors) */
#define IDT_ENTRIES     256

/* CPU exception vectors */
#define EXCEPTION_DE    0       /* Divide Error */
#define EXCEPTION_DB    1       /* Debug */
#define EXCEPTION_NMI   2       /* Non-Maskable Interrupt */
#define EXCEPTION_BP    3       /* Breakpoint */
#define EXCEPTION_OF    4       /* Overflow */
#define EXCEPTION_BR    5       /* Bound Range Exceeded */
#define EXCEPTION_UD    6       /* Invalid Opcode */
#define EXCEPTION_NM    7       /* Device Not Available */
#define EXCEPTION_DF    8       /* Double Fault */
#define EXCEPTION_TS    10      /* Invalid TSS */
#define EXCEPTION_NP    11      /* Segment Not Present */
#define EXCEPTION_SS    12      /* Stack-Segment Fault */
#define EXCEPTION_GP    13      /* General Protection Fault */
#define EXCEPTION_PF    14      /* Page Fault */
#define EXCEPTION_MF    16      /* x87 Floating-Point Exception */
#define EXCEPTION_AC    17      /* Alignment Check */
#define EXCEPTION_MC    18      /* Machine Check */
#define EXCEPTION_XM    19      /* SIMD Floating-Point Exception */
#define EXCEPTION_VE    20      /* Virtualization Exception */
#define EXCEPTION_CP    21      /* Control Protection Exception */

/* Hardware IRQ vectors (remapped to 32-47) */
#define IRQ_BASE        32
#define IRQ_TIMER       (IRQ_BASE + 0)
#define IRQ_KEYBOARD    (IRQ_BASE + 1)
#define IRQ_CASCADE     (IRQ_BASE + 2)
#define IRQ_COM2        (IRQ_BASE + 3)
#define IRQ_COM1        (IRQ_BASE + 4)
#define IRQ_LPT2        (IRQ_BASE + 5)
#define IRQ_FLOPPY      (IRQ_BASE + 6)
#define IRQ_LPT1        (IRQ_BASE + 7)
#define IRQ_RTC         (IRQ_BASE + 8)
#define IRQ_MOUSE       (IRQ_BASE + 12)
#define IRQ_FPU         (IRQ_BASE + 13)
#define IRQ_ATA1        (IRQ_BASE + 14)
#define IRQ_ATA2        (IRQ_BASE + 15)

/* IDT gate types */
#define IDT_GATE_INTERRUPT  0x8E    /* P=1, DPL=0, Interrupt gate */
#define IDT_GATE_TRAP       0x8F    /* P=1, DPL=0, Trap gate */
#define IDT_GATE_USER       0xEE    /* P=1, DPL=3, Interrupt gate (userspace) */

/**
 * IDT entry (gate descriptor) - 16 bytes in 64-bit mode
 */
typedef struct PACKED {
    uint16_t offset_low;    /* Offset bits 0-15 */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table offset (3 bits) */
    uint8_t  type_attr;     /* Gate type and attributes */
    uint16_t offset_mid;    /* Offset bits 16-31 */
    uint32_t offset_high;   /* Offset bits 32-63 */
    uint32_t reserved;      /* Reserved */
} idt_entry_t;

/**
 * IDT descriptor (IDTR)
 */
typedef struct PACKED {
    uint16_t limit;         /* Size of IDT - 1 */
    uint64_t base;          /* Linear address of IDT */
} idt_descriptor_t;

/**
 * Interrupt frame pushed by CPU
 */
typedef struct PACKED {
    /* Pushed by our stub */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;

    /* Interrupt number and error code */
    uint64_t int_no;
    uint64_t error_code;

    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/**
 * Interrupt handler function type
 */
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);

/**
 * Initialize the IDT with default handlers
 */
void idt_init(void);

/**
 * Register an interrupt handler
 * @param vector Interrupt vector number (0-255)
 * @param handler Function to call when interrupt occurs
 */
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

/**
 * Enable/disable interrupts
 */
static inline void interrupts_enable(void) {
    __asm__ __volatile__("sti");
}

static inline void interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

/**
 * Check if interrupts are enabled
 */
static inline bool interrupts_enabled(void) {
    uint64_t flags;
    __asm__ __volatile__("pushfq; pop %0" : "=r"(flags));
    return (flags & (1 << 9)) != 0;
}

#endif /* _AAAOS_ARCH_IDT_H */
