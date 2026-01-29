/**
 * AAAos Kernel - VGA Text Mode Driver Implementation
 */

#include "include/vga.h"
#include "arch/x86_64/io.h"
#include <stdarg.h>

/* VGA state */
static uint16_t *vga_buffer = (uint16_t*)VGA_MEMORY;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color;

/* VGA ports */
#define VGA_CTRL_PORT   0x3D4
#define VGA_DATA_PORT   0x3D5

/**
 * Initialize VGA text mode console
 */
void vga_init(void) {
    current_color = vga_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    vga_clear();
    vga_enable_cursor(true);
}

/**
 * Clear the screen
 */
void vga_clear(void) {
    uint16_t blank = vga_entry(' ', current_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

/**
 * Set the current text color
 */
void vga_set_color(vga_color_t fg, vga_color_t bg) {
    current_color = vga_color(fg, bg);
}

/**
 * Scroll the screen up by one line
 */
void vga_scroll(void) {
    /* Move all lines up */
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    /* Clear the last line */
    uint16_t blank = vga_entry(' ', current_color);
    for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
}

/**
 * Write a character at current cursor position
 */
void vga_putc(char c) {
    switch (c) {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;

        case '\r':
            cursor_x = 0;
            break;

        case '\t':
            /* Tab to next 8-column boundary */
            cursor_x = (cursor_x + 8) & ~7;
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
            }
            break;

        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(' ', current_color);
            }
            break;

        default:
            if (c >= ' ') {
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, current_color);
                cursor_x++;
            }
            break;
    }

    /* Handle line wrap */
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    /* Handle scroll */
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
        cursor_y = VGA_HEIGHT - 1;
    }

    vga_update_cursor();
}

/**
 * Write a string at current cursor position
 */
void vga_puts(const char *str) {
    while (*str) {
        vga_putc(*str++);
    }
}

/**
 * Set cursor position
 */
void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        cursor_x = x;
        cursor_y = y;
        vga_update_cursor();
    }
}

/**
 * Get current cursor X position
 */
int vga_get_cursor_x(void) {
    return cursor_x;
}

/**
 * Get current cursor Y position
 */
int vga_get_cursor_y(void) {
    return cursor_y;
}

/**
 * Enable/disable hardware cursor
 */
void vga_enable_cursor(bool enable) {
    if (enable) {
        /* Cursor start scanline 14, end scanline 15 */
        outb(VGA_CTRL_PORT, 0x0A);
        outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xC0) | 14);
        outb(VGA_CTRL_PORT, 0x0B);
        outb(VGA_DATA_PORT, (inb(VGA_DATA_PORT) & 0xE0) | 15);
    } else {
        /* Disable cursor */
        outb(VGA_CTRL_PORT, 0x0A);
        outb(VGA_DATA_PORT, 0x20);
    }
}

/**
 * Update hardware cursor position
 */
void vga_update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;

    outb(VGA_CTRL_PORT, 0x0F);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL_PORT, 0x0E);
    outb(VGA_DATA_PORT, (uint8_t)((pos >> 8) & 0xFF));
}

/* Helper function to print an unsigned integer */
static void vga_print_uint(uint64_t value, int base, int width, char pad) {
    char buffer[65];
    const char *digits = "0123456789ABCDEF";
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
        vga_putc(buffer[--i]);
    }
}

/* Helper function to print a signed integer */
static void vga_print_int(int64_t value, int width, char pad) {
    if (value < 0) {
        vga_putc('-');
        value = -value;
        if (width > 0) width--;
    }
    vga_print_uint((uint64_t)value, 10, width, pad);
}

/**
 * Printf-like function for VGA output
 */
void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putc(*fmt++);
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
                    vga_print_int(va_arg(args, int64_t), width, pad);
                } else if (is_long == 1) {
                    vga_print_int(va_arg(args, long), width, pad);
                } else {
                    vga_print_int(va_arg(args, int), width, pad);
                }
                break;

            case 'u':
                if (is_long >= 2) {
                    vga_print_uint(va_arg(args, uint64_t), 10, width, pad);
                } else if (is_long == 1) {
                    vga_print_uint(va_arg(args, unsigned long), 10, width, pad);
                } else {
                    vga_print_uint(va_arg(args, unsigned int), 10, width, pad);
                }
                break;

            case 'x':
            case 'X':
                if (is_long >= 2) {
                    vga_print_uint(va_arg(args, uint64_t), 16, width, pad);
                } else if (is_long == 1) {
                    vga_print_uint(va_arg(args, unsigned long), 16, width, pad);
                } else {
                    vga_print_uint(va_arg(args, unsigned int), 16, width, pad);
                }
                break;

            case 'p':
                vga_puts("0x");
                vga_print_uint((uint64_t)va_arg(args, void*), 16, 16, '0');
                break;

            case 's': {
                const char *s = va_arg(args, const char*);
                if (s == NULL) s = "(null)";
                vga_puts(s);
                break;
            }

            case 'c':
                vga_putc((char)va_arg(args, int));
                break;

            case '%':
                vga_putc('%');
                break;

            default:
                vga_putc('%');
                vga_putc(*fmt);
                break;
        }
        fmt++;
    }

    va_end(args);
}
