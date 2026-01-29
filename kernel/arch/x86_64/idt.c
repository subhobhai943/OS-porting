/**
 * AAAos Kernel - IDT Implementation
 */

#include "include/idt.h"
#include "include/gdt.h"
#include "../../include/serial.h"
#include "../../include/vga.h"
#include "io.h"

/* IDT entries */
static idt_entry_t idt[IDT_ENTRIES] ALIGNED(16);

/* IDT descriptor */
static idt_descriptor_t idt_descriptor;

/* Registered interrupt handlers */
static interrupt_handler_t handlers[IDT_ENTRIES];

/* Exception messages */
static const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

/* External ISR stubs (defined in idt_asm.asm) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/**
 * Set an IDT entry
 */
static void idt_set_gate(int index, uint64_t handler, uint16_t selector, uint8_t type) {
    idt[index].offset_low = handler & 0xFFFF;
    idt[index].offset_mid = (handler >> 16) & 0xFFFF;
    idt[index].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[index].selector = selector;
    idt[index].ist = 0;
    idt[index].type_attr = type;
    idt[index].reserved = 0;
}

/**
 * Initialize the 8259 PIC
 */
static void pic_init(void) {
    /* Start initialization sequence */
    outb(0x20, 0x11);   /* Master PIC command */
    outb(0xA0, 0x11);   /* Slave PIC command */
    io_wait();

    /* Set vector offsets */
    outb(0x21, 0x20);   /* Master PIC: IRQs 0-7 -> interrupts 32-39 */
    outb(0xA1, 0x28);   /* Slave PIC: IRQs 8-15 -> interrupts 40-47 */
    io_wait();

    /* Tell Master about Slave at IRQ2 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    io_wait();

    /* 8086 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();

    /* Mask all interrupts initially (unmask as needed) */
    outb(0x21, 0xFD);   /* Unmask IRQ1 (keyboard) */
    outb(0xA1, 0xFF);
}

/**
 * Send End-Of-Interrupt to PIC
 */
void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);   /* Slave PIC */
    }
    outb(0x20, 0x20);       /* Master PIC */
}

/**
 * Initialize the IDT
 */
void idt_init(void) {
    kprintf("[IDT] Initializing Interrupt Descriptor Table...\n");

    /* Clear handlers */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = NULL;
    }

    /* Set up CPU exception handlers (ISRs 0-31) */
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, IDT_GATE_TRAP);
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(22, (uint64_t)isr22, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(23, (uint64_t)isr23, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(24, (uint64_t)isr24, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(25, (uint64_t)isr25, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(26, (uint64_t)isr26, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(27, (uint64_t)isr27, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(28, (uint64_t)isr28, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(29, (uint64_t)isr29, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(30, (uint64_t)isr30, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(31, (uint64_t)isr31, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);

    /* Set up hardware IRQ handlers (32-47) */
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, IDT_GATE_INTERRUPT);

    /* Initialize PIC */
    pic_init();

    /* Set up IDT descriptor */
    idt_descriptor.limit = sizeof(idt) - 1;
    idt_descriptor.base = (uint64_t)&idt;

    /* Load IDT */
    __asm__ __volatile__("lidt %0" : : "m"(idt_descriptor));

    kprintf("[IDT] IDT loaded at %p, %d entries\n", (void*)idt_descriptor.base, IDT_ENTRIES);
    kprintf("[IDT] PIC remapped to vectors 32-47\n");
}

/**
 * Register an interrupt handler
 */
void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}

/**
 * Common interrupt handler (called from assembly)
 */
void interrupt_handler(interrupt_frame_t *frame) {
    uint64_t int_no = frame->int_no;

    /* Call registered handler if present */
    if (handlers[int_no] != NULL) {
        handlers[int_no](frame);
    } else if (int_no < 32) {
        /* Unhandled CPU exception - panic */
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        vga_printf("\n\n  KERNEL PANIC: %s (Exception %d)\n", exception_messages[int_no], (int)int_no);
        vga_printf("  Error Code: 0x%016llX\n", frame->error_code);
        vga_printf("  RIP: 0x%016llX  RSP: 0x%016llX\n", frame->rip, frame->rsp);
        vga_printf("  RAX: 0x%016llX  RBX: 0x%016llX\n", frame->rax, frame->rbx);
        vga_printf("  RCX: 0x%016llX  RDX: 0x%016llX\n", frame->rcx, frame->rdx);
        vga_printf("  RSI: 0x%016llX  RDI: 0x%016llX\n", frame->rsi, frame->rdi);
        vga_printf("  RBP: 0x%016llX  R8:  0x%016llX\n", frame->rbp, frame->r8);
        vga_printf("  R9:  0x%016llX  R10: 0x%016llX\n", frame->r9, frame->r10);
        vga_printf("  CS: 0x%04llX  SS: 0x%04llX  RFLAGS: 0x%016llX\n", frame->cs, frame->ss, frame->rflags);

        /* Also log to serial */
        kprintf("\n[PANIC] %s (Exception %d)\n", exception_messages[int_no], (int)int_no);
        kprintf("Error Code: 0x%016llx\n", frame->error_code);
        kprintf("RIP: 0x%016llx\n", frame->rip);

        /* Halt */
        __asm__ __volatile__("cli; hlt");
        for (;;);
    }

    /* Send EOI for hardware interrupts */
    if (int_no >= 32 && int_no < 48) {
        pic_eoi((uint8_t)(int_no - 32));
    }
}
