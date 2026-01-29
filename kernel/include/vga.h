/**
 * AAAos Kernel - VGA Text Mode Driver
 *
 * Provides text output to VGA display in text mode (80x25).
 * Used for early boot console before graphical framebuffer.
 */

#ifndef _AAAOS_VGA_H
#define _AAAOS_VGA_H

#include "types.h"

/* VGA text mode buffer address */
#define VGA_MEMORY      0xB8000

/* VGA dimensions */
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

/* VGA color codes */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GRAY    = 7,
    VGA_COLOR_DARK_GRAY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW        = 14,
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

/**
 * Create a VGA color attribute byte
 * @param fg Foreground color
 * @param bg Background color
 * @return Combined color attribute
 */
static inline uint8_t vga_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(fg | (bg << 4));
}

/**
 * Create a VGA character entry (char + color)
 * @param c Character
 * @param color Color attribute
 * @return Combined VGA entry
 */
static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/**
 * Initialize VGA text mode console
 */
void vga_init(void);

/**
 * Clear the screen
 */
void vga_clear(void);

/**
 * Set the current text color
 * @param fg Foreground color
 * @param bg Background color
 */
void vga_set_color(vga_color_t fg, vga_color_t bg);

/**
 * Write a character at current cursor position
 * @param c Character to write
 */
void vga_putc(char c);

/**
 * Write a string at current cursor position
 * @param str Null-terminated string
 */
void vga_puts(const char *str);

/**
 * Write a formatted string (printf-like)
 * @param fmt Format string
 */
void vga_printf(const char *fmt, ...);

/**
 * Set cursor position
 * @param x Column (0-79)
 * @param y Row (0-24)
 */
void vga_set_cursor(int x, int y);

/**
 * Get current cursor X position
 * @return Column position
 */
int vga_get_cursor_x(void);

/**
 * Get current cursor Y position
 * @return Row position
 */
int vga_get_cursor_y(void);

/**
 * Scroll the screen up by one line
 */
void vga_scroll(void);

/**
 * Enable/disable hardware cursor
 * @param enable true to show cursor, false to hide
 */
void vga_enable_cursor(bool enable);

/**
 * Update hardware cursor position to match software cursor
 */
void vga_update_cursor(void);

#endif /* _AAAOS_VGA_H */
