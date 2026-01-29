/**
 * AAAos Kernel - Serial Port Driver Implementation
 */

#include "include/serial.h"
#include "arch/x86_64/io.h"
#include <stdarg.h>

/**
 * Initialize serial port
 */
void serial_init(uint16_t port) {
    /* Disable interrupts */
    outb(port + SERIAL_INT_ENABLE, 0x00);

    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate */
    outb(port + SERIAL_LINE_CTRL, 0x80);

    /* Set divisor to 1 (115200 baud) */
    outb(port + SERIAL_DIVISOR_LO, 0x01);
    outb(port + SERIAL_DIVISOR_HI, 0x00);

    /* 8 bits, no parity, one stop bit (8N1) */
    outb(port + SERIAL_LINE_CTRL, 0x03);

    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(port + SERIAL_FIFO_CTRL, 0xC7);

    /* Enable IRQs, RTS/DSR set */
    outb(port + SERIAL_MODEM_CTRL, 0x0B);

    /* Set in loopback mode for testing */
    outb(port + SERIAL_MODEM_CTRL, 0x1E);

    /* Test serial chip by sending 0xAE */
    outb(port + SERIAL_DATA, 0xAE);

    /* Check if we get the same byte back */
    if (inb(port + SERIAL_DATA) != 0xAE) {
        return;  /* Serial port not working */
    }

    /* Set normal operation mode */
    outb(port + SERIAL_MODEM_CTRL, 0x0F);
}

/**
 * Check if transmit buffer is empty
 */
static inline bool serial_tx_empty(uint16_t port) {
    return (inb(port + SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY) != 0;
}

/**
 * Write a character to serial port
 */
void serial_putc(uint16_t port, char c) {
    /* Wait for transmit buffer to be empty */
    while (!serial_tx_empty(port)) {
        /* Spin */
    }
    outb(port + SERIAL_DATA, (uint8_t)c);
}

/**
 * Write a string to serial port
 */
void serial_puts(uint16_t port, const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putc(port, '\r');
        }
        serial_putc(port, *str++);
    }
}

/**
 * Check if data is available to read
 */
bool serial_data_ready(uint16_t port) {
    return (inb(port + SERIAL_LINE_STATUS) & SERIAL_LSR_DATA_READY) != 0;
}

/**
 * Read a character from serial port (blocking)
 */
char serial_getc(uint16_t port) {
    while (!serial_data_ready(port)) {
        /* Spin */
    }
    return (char)inb(port + SERIAL_DATA);
}

/* Helper function to print an unsigned integer */
static void print_uint(uint16_t port, uint64_t value, int base, int width, char pad) {
    char buffer[65];
    const char *digits = "0123456789abcdef";
    int i = 0;

    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = digits[value % base];
            value /= base;
        }
    }

    /* Padding */
    while (i < width) {
        buffer[i++] = pad;
    }

    /* Print in reverse */
    while (i > 0) {
        serial_putc(port, buffer[--i]);
    }
}

/* Helper function to print a signed integer */
static void print_int(uint16_t port, int64_t value, int width, char pad) {
    if (value < 0) {
        serial_putc(port, '-');
        value = -value;
        if (width > 0) width--;
    }
    print_uint(port, (uint64_t)value, 10, width, pad);
}

/**
 * Printf-like function for serial output
 */
void serial_printf(uint16_t port, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                serial_putc(port, '\r');
            }
            serial_putc(port, *fmt++);
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Parse width */
        int width = 0;
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Handle length modifiers */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
            if (*fmt == 'l') {
                is_long = 2;
                fmt++;
            }
        }

        switch (*fmt) {
            case 'd':
            case 'i':
                if (is_long >= 2) {
                    print_int(port, va_arg(args, int64_t), width, pad);
                } else if (is_long == 1) {
                    print_int(port, va_arg(args, long), width, pad);
                } else {
                    print_int(port, va_arg(args, int), width, pad);
                }
                break;

            case 'u':
                if (is_long >= 2) {
                    print_uint(port, va_arg(args, uint64_t), 10, width, pad);
                } else if (is_long == 1) {
                    print_uint(port, va_arg(args, unsigned long), 10, width, pad);
                } else {
                    print_uint(port, va_arg(args, unsigned int), 10, width, pad);
                }
                break;

            case 'x':
            case 'X':
                if (is_long >= 2) {
                    print_uint(port, va_arg(args, uint64_t), 16, width, pad);
                } else if (is_long == 1) {
                    print_uint(port, va_arg(args, unsigned long), 16, width, pad);
                } else {
                    print_uint(port, va_arg(args, unsigned int), 16, width, pad);
                }
                break;

            case 'p':
                serial_puts(port, "0x");
                print_uint(port, (uint64_t)va_arg(args, void*), 16, 16, '0');
                break;

            case 's': {
                const char *s = va_arg(args, const char*);
                if (s == NULL) s = "(null)";
                serial_puts(port, s);
                break;
            }

            case 'c':
                serial_putc(port, (char)va_arg(args, int));
                break;

            case '%':
                serial_putc(port, '%');
                break;

            default:
                serial_putc(port, '%');
                serial_putc(port, *fmt);
                break;
        }
        fmt++;
    }

    va_end(args);
}
