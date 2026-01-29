/**
 * AAAos Kernel - Framebuffer Graphics Driver
 *
 * Provides pixel-level graphics operations for VESA/GOP framebuffers.
 * Supports 32-bit ARGB color format (0xAARRGGBB).
 */

#ifndef _AAAOS_FRAMEBUFFER_H
#define _AAAOS_FRAMEBUFFER_H

#include "../../kernel/include/types.h"
#include "../../kernel/include/boot.h"

/* Color format: 0xAARRGGBB */
#define FB_COLOR_BLACK      0xFF000000
#define FB_COLOR_WHITE      0xFFFFFFFF
#define FB_COLOR_RED        0xFFFF0000
#define FB_COLOR_GREEN      0xFF00FF00
#define FB_COLOR_BLUE       0xFF0000FF
#define FB_COLOR_YELLOW     0xFFFFFF00
#define FB_COLOR_CYAN       0xFF00FFFF
#define FB_COLOR_MAGENTA    0xFFFF00FF
#define FB_COLOR_GRAY       0xFF808080
#define FB_COLOR_DARK_GRAY  0xFF404040
#define FB_COLOR_LIGHT_GRAY 0xFFC0C0C0

/* Font dimensions */
#define FB_FONT_WIDTH       8
#define FB_FONT_HEIGHT      16

/* Color manipulation macros */
#define FB_MAKE_COLOR(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

#define FB_GET_ALPHA(c)     (((c) >> 24) & 0xFF)
#define FB_GET_RED(c)       (((c) >> 16) & 0xFF)
#define FB_GET_GREEN(c)     (((c) >> 8) & 0xFF)
#define FB_GET_BLUE(c)      ((c) & 0xFF)

/**
 * Framebuffer information structure
 */
typedef struct {
    uint32_t *address;      /* Pointer to framebuffer memory */
    uint32_t width;         /* Screen width in pixels */
    uint32_t height;        /* Screen height in pixels */
    uint32_t pitch;         /* Bytes per scanline */
    uint32_t bpp;           /* Bits per pixel */
    uint32_t size;          /* Total framebuffer size in bytes */
    bool initialized;       /* Framebuffer initialization status */
} framebuffer_t;

/**
 * Initialize framebuffer from boot information
 * @param boot_info Pointer to boot information structure
 * @return 0 on success, negative error code on failure
 */
int fb_init(boot_info_t *boot_info);

/**
 * Get framebuffer information
 * @return Pointer to framebuffer info structure (read-only)
 */
const framebuffer_t *fb_get_info(void);

/**
 * Check if framebuffer is initialized
 * @return true if initialized, false otherwise
 */
bool fb_is_initialized(void);

/**
 * Draw a single pixel
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Pixel color (0xAARRGGBB)
 */
void fb_put_pixel(int x, int y, uint32_t color);

/**
 * Get pixel color at coordinates
 * @param x X coordinate
 * @param y Y coordinate
 * @return Pixel color (0xAARRGGBB), or 0 if out of bounds
 */
uint32_t fb_get_pixel(int x, int y);

/**
 * Fill a rectangle with solid color
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param w Width in pixels
 * @param h Height in pixels
 * @param color Fill color (0xAARRGGBB)
 */
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

/**
 * Draw a rectangle outline
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param w Width in pixels
 * @param h Height in pixels
 * @param color Outline color (0xAARRGGBB)
 */
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);

/**
 * Draw a line using Bresenham's algorithm
 * @param x0 Start X coordinate
 * @param y0 Start Y coordinate
 * @param x1 End X coordinate
 * @param y1 End Y coordinate
 * @param color Line color (0xAARRGGBB)
 */
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

/**
 * Draw a character using 8x16 bitmap font
 * @param x Top-left X coordinate
 * @param y Top-left Y coordinate
 * @param c Character to draw
 * @param fg Foreground color (0xAARRGGBB)
 * @param bg Background color (0xAARRGGBB)
 */
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);

/**
 * Draw a string
 * @param x Starting X coordinate
 * @param y Starting Y coordinate
 * @param str Null-terminated string
 * @param fg Foreground color (0xAARRGGBB)
 * @param bg Background color (0xAARRGGBB)
 */
void fb_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);

/**
 * Clear the entire screen with a color
 * @param color Fill color (0xAARRGGBB)
 */
void fb_clear(uint32_t color);

/**
 * Scroll the screen up by specified number of lines
 * @param lines Number of pixel lines to scroll
 */
void fb_scroll(int lines);

/**
 * Draw a horizontal line (optimized)
 * @param x Starting X coordinate
 * @param y Y coordinate
 * @param w Width in pixels
 * @param color Line color (0xAARRGGBB)
 */
void fb_draw_hline(int x, int y, int w, uint32_t color);

/**
 * Draw a vertical line (optimized)
 * @param x X coordinate
 * @param y Starting Y coordinate
 * @param h Height in pixels
 * @param color Line color (0xAARRGGBB)
 */
void fb_draw_vline(int x, int y, int h, uint32_t color);

/**
 * Draw a filled circle
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param r Radius in pixels
 * @param color Fill color (0xAARRGGBB)
 */
void fb_fill_circle(int cx, int cy, int r, uint32_t color);

/**
 * Draw a circle outline
 * @param cx Center X coordinate
 * @param cy Center Y coordinate
 * @param r Radius in pixels
 * @param color Outline color (0xAARRGGBB)
 */
void fb_draw_circle(int cx, int cy, int r, uint32_t color);

#endif /* _AAAOS_FRAMEBUFFER_H */
