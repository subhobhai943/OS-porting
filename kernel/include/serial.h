/**
 * AAAos Kernel - Serial Port Driver
 *
 * Provides serial output for kernel debugging.
 * Uses COM1 (0x3F8) by default.
 */

#ifndef _AAAOS_SERIAL_H
#define _AAAOS_SERIAL_H

#include "types.h"

/* Serial port base addresses */
#define COM1_PORT   0x3F8
#define COM2_PORT   0x2F8
#define COM3_PORT   0x3E8
#define COM4_PORT   0x2E8

/* Serial port registers (offset from base) */
#define SERIAL_DATA         0   /* Data register (R/W) */
#define SERIAL_INT_ENABLE   1   /* Interrupt enable */
#define SERIAL_FIFO_CTRL    2   /* FIFO control */
#define SERIAL_LINE_CTRL    3   /* Line control */
#define SERIAL_MODEM_CTRL   4   /* Modem control */
#define SERIAL_LINE_STATUS  5   /* Line status */
#define SERIAL_MODEM_STATUS 6   /* Modem status */
#define SERIAL_SCRATCH      7   /* Scratch register */

/* Divisor latch registers (when DLAB=1) */
#define SERIAL_DIVISOR_LO   0   /* Divisor latch low byte */
#define SERIAL_DIVISOR_HI   1   /* Divisor latch high byte */

/* Line status register bits */
#define SERIAL_LSR_DATA_READY   0x01
#define SERIAL_LSR_OVERRUN_ERR  0x02
#define SERIAL_LSR_PARITY_ERR   0x04
#define SERIAL_LSR_FRAME_ERR    0x08
#define SERIAL_LSR_BREAK_INT    0x10
#define SERIAL_LSR_TX_EMPTY     0x20
#define SERIAL_LSR_TX_IDLE      0x40
#define SERIAL_LSR_FIFO_ERR     0x80

/**
 * Initialize serial port for debugging output
 * @param port Base port address (e.g., COM1_PORT)
 */
void serial_init(uint16_t port);

/**
 * Write a character to serial port
 * @param port Base port address
 * @param c Character to write
 */
void serial_putc(uint16_t port, char c);

/**
 * Write a string to serial port
 * @param port Base port address
 * @param str Null-terminated string
 */
void serial_puts(uint16_t port, const char *str);

/**
 * Write a formatted string to serial port (printf-like)
 * @param port Base port address
 * @param fmt Format string
 */
void serial_printf(uint16_t port, const char *fmt, ...);

/**
 * Read a character from serial port (blocking)
 * @param port Base port address
 * @return Character read
 */
char serial_getc(uint16_t port);

/**
 * Check if data is available to read
 * @param port Base port address
 * @return true if data available
 */
bool serial_data_ready(uint16_t port);

/* Convenience macros for COM1 */
#define serial_init_com1()      serial_init(COM1_PORT)
#define kputc(c)                serial_putc(COM1_PORT, c)
#define kputs(s)                serial_puts(COM1_PORT, s)
#define kprintf(fmt, ...)       serial_printf(COM1_PORT, fmt, ##__VA_ARGS__)

#endif /* _AAAOS_SERIAL_H */
