/**
 * AAAos Kernel - x86_64 I/O Port Access
 *
 * Inline assembly wrappers for port I/O instructions.
 */

#ifndef _AAAOS_ARCH_IO_H
#define _AAAOS_ARCH_IO_H

#include "../../include/types.h"

/**
 * Read a byte from an I/O port
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a byte to an I/O port
 */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read a word (16-bit) from an I/O port
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a word (16-bit) to an I/O port
 */
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Read a double word (32-bit) from an I/O port
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Write a double word (32-bit) to an I/O port
 */
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Small delay using I/O port (for timing-sensitive operations)
 */
static inline void io_wait(void) {
    outb(0x80, 0);  /* Port 0x80 is used for POST codes */
}

#endif /* _AAAOS_ARCH_IO_H */
