/**
 * AAAos Terminal Emulator - Implementation
 *
 * Provides a terminal emulator with ANSI escape code support,
 * scrollback buffer, and shell command execution.
 */

#include "terminal.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"
#include "../../drivers/video/framebuffer.h"
#include "../../drivers/input/keyboard.h"
#include "../../gui/compositor/compositor.h"
#include "../../apps/shell/shell.h"

/* Terminal subsystem state */
static bool g_terminal_initialized = false;

/* Default color palette (ARGB format) */
static const uint32_t default_palette[ANSI_COLOR_MAX] = {
    [ANSI_COLOR_BLACK]          = 0xFF000000,
    [ANSI_COLOR_RED]            = 0xFFAA0000,
    [ANSI_COLOR_GREEN]          = 0xFF00AA00,
    [ANSI_COLOR_YELLOW]         = 0xFFAA5500,
    [ANSI_COLOR_BLUE]           = 0xFF0000AA,
    [ANSI_COLOR_MAGENTA]        = 0xFFAA00AA,
    [ANSI_COLOR_CYAN]           = 0xFF00AAAA,
    [ANSI_COLOR_WHITE]          = 0xFFAAAAAA,
    [ANSI_COLOR_BRIGHT_BLACK]   = 0xFF555555,
    [ANSI_COLOR_BRIGHT_RED]     = 0xFFFF5555,
    [ANSI_COLOR_BRIGHT_GREEN]   = 0xFF55FF55,
    [ANSI_COLOR_BRIGHT_YELLOW]  = 0xFFFFFF55,
    [ANSI_COLOR_BRIGHT_BLUE]    = 0xFF5555FF,
    [ANSI_COLOR_BRIGHT_MAGENTA] = 0xFFFF55FF,
    [ANSI_COLOR_BRIGHT_CYAN]    = 0xFF55FFFF,
    [ANSI_COLOR_BRIGHT_WHITE]   = 0xFFFFFFFF
};

/* Forward declarations for internal functions */
static void terminal_process_char(terminal_t *term, char c);
static void terminal_process_control(terminal_t *term, char c);
static void terminal_new_line(terminal_t *term);
static void terminal_carriage_return(terminal_t *term);
static void terminal_tab(terminal_t *term);
static void terminal_backspace(terminal_t *term);
static void terminal_scroll_up(terminal_t *term, int lines);
static void terminal_scroll_down(terminal_t *term, int lines);
static terminal_line_t *terminal_alloc_line(uint32_t width);
static void terminal_free_line(terminal_line_t *line);
static void terminal_clear_cell(terminal_t *term, terminal_cell_t *cell);
static void terminal_put_char_at(terminal_t *term, int x, int y, char c);
static bool terminal_parse_csi(terminal_t *term, const char *seq);
static int terminal_parse_int(const char **str, int default_val);

/*============================================================================
 * Terminal Subsystem Initialization
 *============================================================================*/

int terminal_init(void)
{
    if (g_terminal_initialized) {
        return 0;
    }

    kprintf("[terminal] Initializing terminal subsystem\n");

    g_terminal_initialized = true;

    kprintf("[terminal] Terminal subsystem initialized\n");
    return 0;
}

void terminal_shutdown(void)
{
    if (!g_terminal_initialized) {
        return;
    }

    kprintf("[terminal] Shutting down terminal subsystem\n");

    g_terminal_initialized = false;
}

/*============================================================================
 * Terminal Instance Management
 *============================================================================*/

terminal_t *terminal_create(uint32_t width_chars, uint32_t height_chars)
{
    terminal_t *term;
    uint32_t i;

    /* Validate parameters */
    if (width_chars == 0 || width_chars > TERMINAL_MAX_WIDTH) {
        width_chars = TERMINAL_DEFAULT_WIDTH;
    }
    if (height_chars == 0 || height_chars > TERMINAL_MAX_HEIGHT) {
        height_chars = TERMINAL_DEFAULT_HEIGHT;
    }

    kprintf("[terminal] Creating terminal %ux%u\n", width_chars, height_chars);

    /* Allocate terminal structure */
    term = (terminal_t *)kmalloc(sizeof(terminal_t));
    if (!term) {
        kprintf("[terminal] Failed to allocate terminal structure\n");
        return NULL;
    }

    /* Initialize to zero */
    memset(term, 0, sizeof(terminal_t));

    /* Set dimensions */
    term->width = width_chars;
    term->height = height_chars;

    /* Allocate screen buffer */
    term->buffer = (terminal_line_t *)kmalloc(height_chars * sizeof(terminal_line_t));
    if (!term->buffer) {
        kprintf("[terminal] Failed to allocate screen buffer\n");
        kfree(term);
        return NULL;
    }

    /* Initialize each line in screen buffer */
    for (i = 0; i < height_chars; i++) {
        term->buffer[i].cells = (terminal_cell_t *)kmalloc(width_chars * sizeof(terminal_cell_t));
        if (!term->buffer[i].cells) {
            kprintf("[terminal] Failed to allocate line %u\n", i);
            /* Free previously allocated lines */
            while (i > 0) {
                i--;
                kfree(term->buffer[i].cells);
            }
            kfree(term->buffer);
            kfree(term);
            return NULL;
        }
        term->buffer[i].dirty = true;
    }

    /* Allocate scrollback buffer */
    term->scrollback_size = TERMINAL_SCROLLBACK_LINES;
    term->scrollback = (terminal_line_t *)kmalloc(term->scrollback_size * sizeof(terminal_line_t));
    if (!term->scrollback) {
        kprintf("[terminal] Failed to allocate scrollback buffer\n");
        /* Free screen buffer */
        for (i = 0; i < height_chars; i++) {
            kfree(term->buffer[i].cells);
        }
        kfree(term->buffer);
        kfree(term);
        return NULL;
    }

    /* Initialize scrollback lines to NULL (allocated on demand) */
    for (i = 0; i < term->scrollback_size; i++) {
        term->scrollback[i].cells = NULL;
        term->scrollback[i].dirty = false;
    }

    term->scrollback_count = 0;
    term->scrollback_head = 0;
    term->scroll_offset = 0;

    /* Set default colors */
    term->default_fg = ANSI_COLOR_WHITE;
    term->default_bg = ANSI_COLOR_BLACK;
    term->fg_color = term->default_fg;
    term->bg_color = term->default_bg;
    term->attributes = 0;

    /* Initialize color palette */
    memcpy(term->palette, default_palette, sizeof(default_palette));

    /* Initialize cursor */
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->cursor_visible = true;
    term->cursor_blink_state = true;
    term->saved_cursor_x = 0;
    term->saved_cursor_y = 0;

    /* Initialize escape state */
    term->esc_state = ESC_STATE_NORMAL;
    term->esc_length = 0;

    /* Initialize input state */
    term->input_length = 0;
    term->input_pos = 0;
    term->echo_enabled = true;
    term->line_mode = true;
    term->insert_mode = true;

    /* No window yet */
    term->window = NULL;
    term->fullscreen = false;
    term->needs_redraw = true;

    /* Terminal is ready */
    term->running = false;

    /* Clear the screen */
    terminal_clear(term);

    kprintf("[terminal] Terminal created successfully\n");
    return term;
}

void terminal_destroy(terminal_t *term)
{
    uint32_t i;

    if (!term) {
        return;
    }

    kprintf("[terminal] Destroying terminal\n");

    /* Free screen buffer */
    if (term->buffer) {
        for (i = 0; i < term->height; i++) {
            if (term->buffer[i].cells) {
                kfree(term->buffer[i].cells);
            }
        }
        kfree(term->buffer);
    }

    /* Free scrollback buffer */
    if (term->scrollback) {
        for (i = 0; i < term->scrollback_size; i++) {
            if (term->scrollback[i].cells) {
                kfree(term->scrollback[i].cells);
            }
        }
        kfree(term->scrollback);
    }

    /* Destroy window if in windowed mode */
    if (term->window) {
        compositor_destroy_window(term->window);
    }

    /* Free terminal structure */
    kfree(term);
}

terminal_t *terminal_create_windowed(uint32_t width_chars, uint32_t height_chars,
                                     const char *title)
{
    terminal_t *term;
    int pixel_width, pixel_height;

    /* Create base terminal */
    term = terminal_create(width_chars, height_chars);
    if (!term) {
        return NULL;
    }

    /* Calculate window size in pixels */
    pixel_width = width_chars * TERMINAL_CHAR_WIDTH;
    pixel_height = height_chars * TERMINAL_CHAR_HEIGHT;

    /* Create window */
    term->window = compositor_create_window(100, 100, pixel_width, pixel_height,
                                            title ? title : "Terminal");
    if (!term->window) {
        kprintf("[terminal] Failed to create window\n");
        terminal_destroy(term);
        return NULL;
    }

    term->fullscreen = false;
    term->needs_redraw = true;

    kprintf("[terminal] Created windowed terminal %dx%d pixels\n",
            pixel_width, pixel_height);

    return term;
}

terminal_t *terminal_create_fullscreen(void)
{
    terminal_t *term;
    const framebuffer_t *fb;
    uint32_t width_chars, height_chars;

    /* Get framebuffer info */
    fb = fb_get_info();
    if (!fb || !fb->initialized) {
        kprintf("[terminal] Framebuffer not available\n");
        return NULL;
    }

    /* Calculate terminal size from screen size */
    width_chars = fb->width / TERMINAL_CHAR_WIDTH;
    height_chars = fb->height / TERMINAL_CHAR_HEIGHT;

    /* Create terminal */
    term = terminal_create(width_chars, height_chars);
    if (!term) {
        return NULL;
    }

    term->fullscreen = true;
    term->window = NULL;
    term->needs_redraw = true;

    kprintf("[terminal] Created fullscreen terminal %ux%u chars\n",
            width_chars, height_chars);

    return term;
}

/*============================================================================
 * Output Functions
 *============================================================================*/

void terminal_putc(terminal_t *term, char c)
{
    if (!term) {
        return;
    }

    terminal_process_char(term, c);
}

void terminal_puts(terminal_t *term, const char *str)
{
    if (!term || !str) {
        return;
    }

    while (*str) {
        terminal_putc(term, *str++);
    }
}

int terminal_printf(terminal_t *term, const char *fmt, ...)
{
    char buffer[1024];
    int len = 0;
    const char *p = fmt;
    __builtin_va_list args;

    if (!term || !fmt) {
        return -1;
    }

    __builtin_va_start(args, fmt);

    /* Simple printf implementation */
    while (*p && len < (int)sizeof(buffer) - 1) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    while (*s && len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = *s++;
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    int val = __builtin_va_arg(args, int);
                    char num[32];
                    int i = 0;
                    bool negative = false;

                    if (val < 0) {
                        negative = true;
                        val = -val;
                    }

                    do {
                        num[i++] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0 && i < 31);

                    if (negative && len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = '-';
                    }

                    while (i > 0 && len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = num[--i];
                    }
                    break;
                }
                case 'u': {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    char num[32];
                    int i = 0;

                    do {
                        num[i++] = '0' + (val % 10);
                        val /= 10;
                    } while (val > 0 && i < 31);

                    while (i > 0 && len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = num[--i];
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    char num[32];
                    const char *hex = (*p == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                    int i = 0;

                    do {
                        num[i++] = hex[val & 0xF];
                        val >>= 4;
                    } while (val > 0 && i < 31);

                    while (i > 0 && len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = num[--i];
                    }
                    break;
                }
                case 'c': {
                    int c = __builtin_va_arg(args, int);
                    buffer[len++] = (char)c;
                    break;
                }
                case '%':
                    buffer[len++] = '%';
                    break;
                default:
                    buffer[len++] = '%';
                    if (len < (int)sizeof(buffer) - 1) {
                        buffer[len++] = *p;
                    }
                    break;
            }
            p++;
        } else {
            buffer[len++] = *p++;
        }
    }

    __builtin_va_end(args);

    buffer[len] = '\0';
    terminal_puts(term, buffer);

    return len;
}

void terminal_clear(terminal_t *term)
{
    uint32_t y, x;

    if (!term) {
        return;
    }

    /* Clear all cells in the buffer */
    for (y = 0; y < term->height; y++) {
        for (x = 0; x < term->width; x++) {
            terminal_clear_cell(term, &term->buffer[y].cells[x]);
        }
        term->buffer[y].dirty = true;
    }

    /* Reset cursor to home position */
    term->cursor_x = 0;
    term->cursor_y = 0;

    term->needs_redraw = true;
}

void terminal_clear_to_end(terminal_t *term)
{
    uint32_t y, x;

    if (!term) {
        return;
    }

    /* Clear from cursor to end of current line */
    for (x = (uint32_t)term->cursor_x; x < term->width; x++) {
        terminal_clear_cell(term, &term->buffer[term->cursor_y].cells[x]);
    }
    term->buffer[term->cursor_y].dirty = true;

    /* Clear all lines below */
    for (y = (uint32_t)term->cursor_y + 1; y < term->height; y++) {
        for (x = 0; x < term->width; x++) {
            terminal_clear_cell(term, &term->buffer[y].cells[x]);
        }
        term->buffer[y].dirty = true;
    }

    term->needs_redraw = true;
}

void terminal_clear_to_start(terminal_t *term)
{
    uint32_t y, x;

    if (!term) {
        return;
    }

    /* Clear all lines above */
    for (y = 0; y < (uint32_t)term->cursor_y; y++) {
        for (x = 0; x < term->width; x++) {
            terminal_clear_cell(term, &term->buffer[y].cells[x]);
        }
        term->buffer[y].dirty = true;
    }

    /* Clear from start of current line to cursor */
    for (x = 0; x <= (uint32_t)term->cursor_x && x < term->width; x++) {
        terminal_clear_cell(term, &term->buffer[term->cursor_y].cells[x]);
    }
    term->buffer[term->cursor_y].dirty = true;

    term->needs_redraw = true;
}

void terminal_clear_line(terminal_t *term, int mode)
{
    uint32_t x;
    uint32_t start, end;

    if (!term) {
        return;
    }

    switch (mode) {
        case 0: /* Cursor to end */
            start = (uint32_t)term->cursor_x;
            end = term->width;
            break;
        case 1: /* Start to cursor */
            start = 0;
            end = (uint32_t)term->cursor_x + 1;
            break;
        case 2: /* Entire line */
        default:
            start = 0;
            end = term->width;
            break;
    }

    for (x = start; x < end && x < term->width; x++) {
        terminal_clear_cell(term, &term->buffer[term->cursor_y].cells[x]);
    }

    term->buffer[term->cursor_y].dirty = true;
    term->needs_redraw = true;
}

/*============================================================================
 * Cursor Functions
 *============================================================================*/

void terminal_set_cursor(terminal_t *term, int x, int y)
{
    if (!term) {
        return;
    }

    /* Clamp to valid range */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)term->width) x = (int)term->width - 1;
    if (y >= (int)term->height) y = (int)term->height - 1;

    term->cursor_x = x;
    term->cursor_y = y;
}

void terminal_move_cursor(terminal_t *term, int dx, int dy)
{
    if (!term) {
        return;
    }

    terminal_set_cursor(term, term->cursor_x + dx, term->cursor_y + dy);
}

void terminal_save_cursor(terminal_t *term)
{
    if (!term) {
        return;
    }

    term->saved_cursor_x = term->cursor_x;
    term->saved_cursor_y = term->cursor_y;
}

void terminal_restore_cursor(terminal_t *term)
{
    if (!term) {
        return;
    }

    terminal_set_cursor(term, term->saved_cursor_x, term->saved_cursor_y);
}

void terminal_set_cursor_visible(terminal_t *term, bool visible)
{
    if (!term) {
        return;
    }

    term->cursor_visible = visible;
}

/*============================================================================
 * Color and Attribute Functions
 *============================================================================*/

void terminal_set_color(terminal_t *term, uint8_t fg, uint8_t bg)
{
    if (!term) {
        return;
    }

    if (fg < ANSI_COLOR_MAX) {
        term->fg_color = fg;
    }
    if (bg < ANSI_COLOR_MAX) {
        term->bg_color = bg;
    }
}

void terminal_set_attributes(terminal_t *term, uint8_t attributes)
{
    if (!term) {
        return;
    }

    term->attributes = attributes;
}

void terminal_reset_attributes(terminal_t *term)
{
    if (!term) {
        return;
    }

    term->fg_color = term->default_fg;
    term->bg_color = term->default_bg;
    term->attributes = 0;
}

void terminal_set_palette_color(terminal_t *term, uint8_t index, uint32_t color)
{
    if (!term || index >= ANSI_COLOR_MAX) {
        return;
    }

    term->palette[index] = color;
    term->needs_redraw = true;
}

/*============================================================================
 * Scrolling Functions
 *============================================================================*/

void terminal_scroll(terminal_t *term, int lines)
{
    if (!term || lines == 0) {
        return;
    }

    if (lines > 0) {
        terminal_scroll_up(term, lines);
    } else {
        terminal_scroll_down(term, -lines);
    }
}

static void terminal_scroll_up(terminal_t *term, int lines)
{
    uint32_t y, x;
    uint32_t src_line;
    terminal_line_t *sb_line;
    uint32_t sb_index;

    if (lines <= 0 || (uint32_t)lines > term->height) {
        lines = (int)term->height;
    }

    /* Save top lines to scrollback */
    for (y = 0; y < (uint32_t)lines; y++) {
        /* Calculate scrollback index */
        sb_index = (term->scrollback_head + term->scrollback_count) % term->scrollback_size;

        /* Allocate scrollback line if needed */
        sb_line = &term->scrollback[sb_index];
        if (!sb_line->cells) {
            sb_line->cells = (terminal_cell_t *)kmalloc(term->width * sizeof(terminal_cell_t));
        }

        if (sb_line->cells) {
            /* Copy line to scrollback */
            memcpy(sb_line->cells, term->buffer[y].cells,
                   term->width * sizeof(terminal_cell_t));

            /* Update scrollback count */
            if (term->scrollback_count < term->scrollback_size) {
                term->scrollback_count++;
            } else {
                /* Scrollback is full, advance head */
                term->scrollback_head = (term->scrollback_head + 1) % term->scrollback_size;
            }
        }
    }

    /* Move lines up */
    for (y = 0; y < term->height - (uint32_t)lines; y++) {
        src_line = y + (uint32_t)lines;
        memcpy(term->buffer[y].cells, term->buffer[src_line].cells,
               term->width * sizeof(terminal_cell_t));
        term->buffer[y].dirty = true;
    }

    /* Clear bottom lines */
    for (y = term->height - (uint32_t)lines; y < term->height; y++) {
        for (x = 0; x < term->width; x++) {
            terminal_clear_cell(term, &term->buffer[y].cells[x]);
        }
        term->buffer[y].dirty = true;
    }

    term->needs_redraw = true;
}

static void terminal_scroll_down(terminal_t *term, int lines)
{
    uint32_t y, x;
    int src_line;

    if (lines <= 0 || (uint32_t)lines > term->height) {
        lines = (int)term->height;
    }

    /* Move lines down */
    for (y = term->height - 1; y >= (uint32_t)lines; y--) {
        src_line = (int)y - lines;
        memcpy(term->buffer[y].cells, term->buffer[src_line].cells,
               term->width * sizeof(terminal_cell_t));
        term->buffer[y].dirty = true;
    }

    /* Clear top lines */
    for (y = 0; y < (uint32_t)lines; y++) {
        for (x = 0; x < term->width; x++) {
            terminal_clear_cell(term, &term->buffer[y].cells[x]);
        }
        term->buffer[y].dirty = true;
    }

    term->needs_redraw = true;
}

void terminal_scroll_view(terminal_t *term, int lines)
{
    if (!term) {
        return;
    }

    term->scroll_offset += lines;

    /* Clamp scroll offset */
    if (term->scroll_offset < 0) {
        term->scroll_offset = 0;
    }
    if ((uint32_t)term->scroll_offset > term->scrollback_count) {
        term->scroll_offset = (int32_t)term->scrollback_count;
    }

    term->needs_redraw = true;
}

void terminal_scroll_to_bottom(terminal_t *term)
{
    if (!term) {
        return;
    }

    if (term->scroll_offset != 0) {
        term->scroll_offset = 0;
        term->needs_redraw = true;
    }
}

/*============================================================================
 * Input Handling Functions
 *============================================================================*/

bool terminal_handle_input(terminal_t *term, key_event_t *key)
{
    if (!term || !key || !key->pressed) {
        return false;
    }

    /* Handle special keys */
    switch (key->keycode) {
        case KEY_UP:
            if (key->modifiers & MOD_SHIFT) {
                terminal_scroll_view(term, 1);
            }
            return false;

        case KEY_DOWN:
            if (key->modifiers & MOD_SHIFT) {
                terminal_scroll_view(term, -1);
            }
            return false;

        case KEY_PAGEUP:
            terminal_scroll_view(term, (int)term->height / 2);
            return false;

        case KEY_PAGEDOWN:
            terminal_scroll_view(term, -(int)term->height / 2);
            return false;

        case KEY_HOME:
            if (key->modifiers & MOD_SHIFT) {
                term->scroll_offset = (int32_t)term->scrollback_count;
                term->needs_redraw = true;
            }
            return false;

        case KEY_END:
            if (key->modifiers & MOD_SHIFT) {
                terminal_scroll_to_bottom(term);
            }
            return false;

        case KEY_BACKSPACE:
            if (term->input_pos > 0) {
                /* Remove character before cursor */
                memmove(&term->input_buffer[term->input_pos - 1],
                        &term->input_buffer[term->input_pos],
                        term->input_length - term->input_pos);
                term->input_pos--;
                term->input_length--;
                term->input_buffer[term->input_length] = '\0';

                if (term->echo_enabled) {
                    terminal_backspace(term);
                }
            }
            return false;

        case KEY_DELETE:
            if (term->input_pos < term->input_length) {
                /* Remove character at cursor */
                memmove(&term->input_buffer[term->input_pos],
                        &term->input_buffer[term->input_pos + 1],
                        term->input_length - term->input_pos);
                term->input_length--;
                term->input_buffer[term->input_length] = '\0';
            }
            return false;

        case KEY_ENTER:
            if (term->echo_enabled) {
                terminal_putc(term, '\n');
            }
            return true; /* Signal that input is complete */

        case KEY_LEFT:
            if (term->input_pos > 0) {
                term->input_pos--;
            }
            return false;

        case KEY_RIGHT:
            if (term->input_pos < term->input_length) {
                term->input_pos++;
            }
            return false;

        default:
            break;
    }

    /* Handle printable characters */
    if (key->ascii >= 32 && key->ascii < 127) {
        if (term->input_length < TERMINAL_INPUT_BUFFER_SIZE - 1) {
            /* Auto-scroll to bottom on input */
            terminal_scroll_to_bottom(term);

            /* Insert character at cursor position */
            if (term->insert_mode && term->input_pos < term->input_length) {
                memmove(&term->input_buffer[term->input_pos + 1],
                        &term->input_buffer[term->input_pos],
                        term->input_length - term->input_pos);
            }

            term->input_buffer[term->input_pos] = key->ascii;
            term->input_pos++;
            if (term->input_pos > term->input_length) {
                term->input_length = term->input_pos;
            }
            term->input_length++;
            term->input_buffer[term->input_length] = '\0';

            if (term->echo_enabled) {
                terminal_putc(term, key->ascii);
            }
        }
        return false;
    }

    return false;
}

int terminal_getline(terminal_t *term, char *buffer, size_t max_length)
{
    key_event_t key;

    if (!term || !buffer || max_length == 0) {
        return -1;
    }

    /* Reset input buffer */
    term->input_length = 0;
    term->input_pos = 0;
    term->input_buffer[0] = '\0';

    /* Read until Enter is pressed */
    while (term->running) {
        if (keyboard_get_event(&key)) {
            if (terminal_handle_input(term, &key)) {
                /* Enter was pressed */
                break;
            }
        }

        /* Draw terminal */
        terminal_draw(term);
    }

    /* Copy input to buffer */
    size_t copy_len = term->input_length;
    if (copy_len >= max_length) {
        copy_len = max_length - 1;
    }
    memcpy(buffer, term->input_buffer, copy_len);
    buffer[copy_len] = '\0';

    /* Reset input buffer */
    term->input_length = 0;
    term->input_pos = 0;

    return (int)copy_len;
}

int terminal_getchar(terminal_t *term)
{
    key_event_t key;

    if (!term) {
        return -1;
    }

    while (term->running) {
        if (keyboard_poll_event(&key)) {
            if (key.pressed && key.ascii >= 32 && key.ascii < 127) {
                return key.ascii;
            }
            if (key.pressed && key.keycode == KEY_ENTER) {
                return '\n';
            }
        }
    }

    return -1;
}

bool terminal_has_input(terminal_t *term)
{
    UNUSED(term);
    return keyboard_has_input();
}

/*============================================================================
 * ANSI Escape Sequence Processing
 *============================================================================*/

bool terminal_process_escape(terminal_t *term, const char *seq)
{
    return terminal_parse_csi(term, seq);
}

static bool terminal_parse_csi(terminal_t *term, const char *seq)
{
    int params[16];
    int param_count = 0;
    const char *p = seq;
    char final_char;
    int n, m;

    /* Parse parameters */
    while (*p && param_count < 16) {
        if (*p >= '0' && *p <= '9') {
            params[param_count++] = terminal_parse_int(&p, 1);
        } else if (*p == ';') {
            p++;
            if (param_count == 0) {
                params[param_count++] = 1;
            }
        } else {
            break;
        }
    }

    final_char = *p;

    /* Default parameters */
    n = (param_count > 0) ? params[0] : 1;
    m = (param_count > 1) ? params[1] : 1;

    switch (final_char) {
        case 'A': /* Cursor up */
            terminal_move_cursor(term, 0, -n);
            return true;

        case 'B': /* Cursor down */
            terminal_move_cursor(term, 0, n);
            return true;

        case 'C': /* Cursor forward */
            terminal_move_cursor(term, n, 0);
            return true;

        case 'D': /* Cursor back */
            terminal_move_cursor(term, -n, 0);
            return true;

        case 'E': /* Cursor next line */
            term->cursor_x = 0;
            terminal_move_cursor(term, 0, n);
            return true;

        case 'F': /* Cursor previous line */
            term->cursor_x = 0;
            terminal_move_cursor(term, 0, -n);
            return true;

        case 'G': /* Cursor horizontal absolute */
            terminal_set_cursor(term, n - 1, term->cursor_y);
            return true;

        case 'H': /* Cursor position */
        case 'f':
            terminal_set_cursor(term, m - 1, n - 1);
            return true;

        case 'J': /* Erase in display */
            switch (n) {
                case 0:
                    terminal_clear_to_end(term);
                    break;
                case 1:
                    terminal_clear_to_start(term);
                    break;
                case 2:
                case 3:
                    terminal_clear(term);
                    break;
            }
            return true;

        case 'K': /* Erase in line */
            terminal_clear_line(term, (param_count > 0) ? params[0] : 0);
            return true;

        case 'S': /* Scroll up */
            terminal_scroll(term, n);
            return true;

        case 'T': /* Scroll down */
            terminal_scroll(term, -n);
            return true;

        case 'm': /* SGR - Select Graphic Rendition */
            if (param_count == 0) {
                /* Reset all attributes */
                terminal_reset_attributes(term);
            } else {
                int i;
                for (i = 0; i < param_count; i++) {
                    int code = params[i];
                    switch (code) {
                        case 0: /* Reset */
                            terminal_reset_attributes(term);
                            break;
                        case 1: /* Bold */
                            term->attributes |= ATTR_BOLD;
                            break;
                        case 4: /* Underline */
                            term->attributes |= ATTR_UNDERLINE;
                            break;
                        case 5: /* Blink */
                            term->attributes |= ATTR_BLINK;
                            break;
                        case 7: /* Inverse */
                            term->attributes |= ATTR_INVERSE;
                            break;
                        case 8: /* Hidden */
                            term->attributes |= ATTR_HIDDEN;
                            break;
                        case 22: /* Normal intensity */
                            term->attributes &= ~ATTR_BOLD;
                            break;
                        case 24: /* Underline off */
                            term->attributes &= ~ATTR_UNDERLINE;
                            break;
                        case 25: /* Blink off */
                            term->attributes &= ~ATTR_BLINK;
                            break;
                        case 27: /* Inverse off */
                            term->attributes &= ~ATTR_INVERSE;
                            break;
                        case 28: /* Hidden off */
                            term->attributes &= ~ATTR_HIDDEN;
                            break;
                        case 30: case 31: case 32: case 33:
                        case 34: case 35: case 36: case 37:
                            /* Foreground color */
                            term->fg_color = (uint8_t)(code - 30);
                            break;
                        case 39: /* Default foreground */
                            term->fg_color = term->default_fg;
                            break;
                        case 40: case 41: case 42: case 43:
                        case 44: case 45: case 46: case 47:
                            /* Background color */
                            term->bg_color = (uint8_t)(code - 40);
                            break;
                        case 49: /* Default background */
                            term->bg_color = term->default_bg;
                            break;
                        case 90: case 91: case 92: case 93:
                        case 94: case 95: case 96: case 97:
                            /* Bright foreground colors */
                            term->fg_color = (uint8_t)(code - 90 + 8);
                            break;
                        case 100: case 101: case 102: case 103:
                        case 104: case 105: case 106: case 107:
                            /* Bright background colors */
                            term->bg_color = (uint8_t)(code - 100 + 8);
                            break;
                    }
                }
            }
            return true;

        case 's': /* Save cursor position */
            terminal_save_cursor(term);
            return true;

        case 'u': /* Restore cursor position */
            terminal_restore_cursor(term);
            return true;

        case 'n': /* Device status report */
            if (n == 6) {
                /* Report cursor position (we don't actually send a response) */
            }
            return true;

        case 'h': /* Set mode */
        case 'l': /* Reset mode */
            /* Handle various modes (cursor visibility, etc.) */
            return true;

        default:
            /* Unknown sequence */
            kprintf("[terminal] Unknown CSI sequence: %s\n", seq);
            return false;
    }
}

static int terminal_parse_int(const char **str, int default_val)
{
    int value = 0;
    bool has_digit = false;

    while (**str >= '0' && **str <= '9') {
        value = value * 10 + (**str - '0');
        (*str)++;
        has_digit = true;
    }

    return has_digit ? value : default_val;
}

/*============================================================================
 * Rendering Functions
 *============================================================================*/

void terminal_draw(terminal_t *term)
{
    uint32_t y;

    if (!term) {
        return;
    }

    /* Only redraw if needed */
    if (!term->needs_redraw) {
        /* Check for dirty lines */
        bool has_dirty = false;
        for (y = 0; y < term->height; y++) {
            if (term->buffer[y].dirty) {
                has_dirty = true;
                break;
            }
        }
        if (!has_dirty) {
            return;
        }
    }

    /* Draw all lines */
    for (y = 0; y < term->height; y++) {
        if (term->needs_redraw || term->buffer[y].dirty) {
            terminal_draw_line(term, (int)y);
            term->buffer[y].dirty = false;
        }
    }

    /* Draw cursor */
    if (term->cursor_visible && term->cursor_blink_state && term->scroll_offset == 0) {
        terminal_draw_cursor(term);
    }

    term->needs_redraw = false;

    /* Update display */
    if (term->window) {
        compositor_invalidate(term->window->x, term->window->y,
                              term->window->width, term->window->height);
        compositor_render();
    }
}

void terminal_draw_line(terminal_t *term, int line)
{
    uint32_t x;
    int pixel_x, pixel_y;
    terminal_cell_t *cell;
    uint32_t fg, bg;
    uint32_t *dest_buffer;
    int dest_x_offset = 0;
    int dest_y_offset = 0;

    if (!term || line < 0 || (uint32_t)line >= term->height) {
        return;
    }

    /* Determine where to draw */
    if (term->window) {
        dest_buffer = term->window->buffer;
        dest_x_offset = 0;
        dest_y_offset = 0;
    } else if (term->fullscreen) {
        const framebuffer_t *fb = fb_get_info();
        if (!fb || !fb->initialized) {
            return;
        }
        dest_buffer = fb->address;
        dest_x_offset = 0;
        dest_y_offset = 0;
    } else {
        return;
    }

    pixel_y = dest_y_offset + line * TERMINAL_CHAR_HEIGHT;

    for (x = 0; x < term->width; x++) {
        cell = &term->buffer[line].cells[x];

        /* Get colors from palette */
        fg = term->palette[cell->fg_color & 0x0F];
        bg = term->palette[cell->bg_color & 0x0F];

        /* Apply inverse attribute */
        if (cell->attributes & ATTR_INVERSE) {
            uint32_t tmp = fg;
            fg = bg;
            bg = tmp;
        }

        /* Apply bold (use bright color) */
        if (cell->attributes & ATTR_BOLD) {
            uint8_t bright_idx = (cell->fg_color & 0x07) + 8;
            fg = term->palette[bright_idx];
        }

        pixel_x = dest_x_offset + (int)x * TERMINAL_CHAR_WIDTH;

        /* Draw character */
        if (term->window) {
            /* Draw to window buffer */
            int cx, cy;
            for (cy = 0; cy < TERMINAL_CHAR_HEIGHT; cy++) {
                for (cx = 0; cx < TERMINAL_CHAR_WIDTH; cx++) {
                    int buf_x = pixel_x + cx;
                    int buf_y = pixel_y + cy;
                    if (buf_x >= 0 && buf_x < term->window->width &&
                        buf_y >= 0 && buf_y < term->window->height) {
                        /* For now, just fill with background */
                        dest_buffer[buf_y * term->window->width + buf_x] = bg;
                    }
                }
            }
            /* Draw the character using font (simplified - use framebuffer function) */
            fb_draw_char(term->window->x + pixel_x, term->window->y + pixel_y,
                         cell->ch ? cell->ch : ' ', fg, bg);
        } else {
            /* Draw directly to framebuffer */
            fb_draw_char(pixel_x, pixel_y, cell->ch ? cell->ch : ' ', fg, bg);
        }
    }
}

void terminal_draw_cursor(terminal_t *term)
{
    int pixel_x, pixel_y;
    uint32_t cursor_color;

    if (!term || !term->cursor_visible) {
        return;
    }

    pixel_x = (int)term->cursor_x * TERMINAL_CHAR_WIDTH;
    pixel_y = (int)term->cursor_y * TERMINAL_CHAR_HEIGHT;

    /* Use foreground color for cursor */
    cursor_color = term->palette[term->fg_color];

    if (term->window) {
        pixel_x += term->window->x;
        pixel_y += term->window->y;
    }

    /* Draw a block cursor */
    fb_fill_rect(pixel_x, pixel_y + TERMINAL_CHAR_HEIGHT - 2,
                 TERMINAL_CHAR_WIDTH, 2, cursor_color);
}

void terminal_blink_cursor(terminal_t *term)
{
    if (!term) {
        return;
    }

    term->cursor_blink_state = !term->cursor_blink_state;

    /* Mark cursor area as dirty */
    if (term->cursor_y >= 0 && (uint32_t)term->cursor_y < term->height) {
        term->buffer[term->cursor_y].dirty = true;
    }
}

/*============================================================================
 * Main Loop Functions
 *============================================================================*/

void terminal_run(terminal_t *term)
{
    char command[TERMINAL_INPUT_BUFFER_SIZE];
    int len;

    if (!term) {
        return;
    }

    term->running = true;

    /* Print welcome message */
    terminal_puts(term, "\033[1;36mAAAos Terminal\033[0m\n");
    terminal_puts(term, "Type 'help' for available commands.\n\n");

    while (term->running) {
        /* Print prompt */
        terminal_puts(term, "\033[1;32maaos>\033[0m ");

        /* Get input */
        len = terminal_getline(term, command, sizeof(command));
        if (len < 0) {
            break;
        }

        if (len > 0) {
            /* Execute command */
            terminal_execute(term, command);
        }
    }
}

int terminal_execute(terminal_t *term, const char *command)
{
    int result;

    if (!term || !command) {
        return -1;
    }

    /* Skip empty commands */
    while (*command == ' ') command++;
    if (*command == '\0') {
        return 0;
    }

    /* Check for built-in terminal commands */
    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        terminal_stop(term);
        return 0;
    }

    if (strcmp(command, "clear") == 0) {
        terminal_clear(term);
        return 0;
    }

    /* Execute shell command */
    result = shell_execute(command);

    return result;
}

void terminal_stop(terminal_t *term)
{
    if (!term) {
        return;
    }

    term->running = false;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

window_t *terminal_get_window(terminal_t *term)
{
    if (!term) {
        return NULL;
    }
    return term->window;
}

int terminal_resize(terminal_t *term, uint32_t new_width, uint32_t new_height)
{
    terminal_line_t *new_buffer;
    uint32_t y, x;
    uint32_t copy_width, copy_height;

    if (!term || new_width == 0 || new_height == 0) {
        return -1;
    }

    if (new_width > TERMINAL_MAX_WIDTH) new_width = TERMINAL_MAX_WIDTH;
    if (new_height > TERMINAL_MAX_HEIGHT) new_height = TERMINAL_MAX_HEIGHT;

    if (new_width == term->width && new_height == term->height) {
        return 0;
    }

    kprintf("[terminal] Resizing from %ux%u to %ux%u\n",
            term->width, term->height, new_width, new_height);

    /* Allocate new buffer */
    new_buffer = (terminal_line_t *)kmalloc(new_height * sizeof(terminal_line_t));
    if (!new_buffer) {
        return -1;
    }

    for (y = 0; y < new_height; y++) {
        new_buffer[y].cells = (terminal_cell_t *)kmalloc(new_width * sizeof(terminal_cell_t));
        if (!new_buffer[y].cells) {
            /* Free allocated lines */
            while (y > 0) {
                y--;
                kfree(new_buffer[y].cells);
            }
            kfree(new_buffer);
            return -1;
        }
        new_buffer[y].dirty = true;

        /* Clear new line */
        for (x = 0; x < new_width; x++) {
            terminal_clear_cell(term, &new_buffer[y].cells[x]);
        }
    }

    /* Copy old content */
    copy_width = MIN(term->width, new_width);
    copy_height = MIN(term->height, new_height);

    for (y = 0; y < copy_height; y++) {
        memcpy(new_buffer[y].cells, term->buffer[y].cells,
               copy_width * sizeof(terminal_cell_t));
    }

    /* Free old buffer */
    for (y = 0; y < term->height; y++) {
        kfree(term->buffer[y].cells);
    }
    kfree(term->buffer);

    /* Install new buffer */
    term->buffer = new_buffer;
    term->width = new_width;
    term->height = new_height;

    /* Adjust cursor if needed */
    if (term->cursor_x >= (int)new_width) {
        term->cursor_x = (int)new_width - 1;
    }
    if (term->cursor_y >= (int)new_height) {
        term->cursor_y = (int)new_height - 1;
    }

    term->needs_redraw = true;

    return 0;
}

void terminal_set_title(terminal_t *term, const char *title)
{
    if (!term || !term->window || !title) {
        return;
    }

    strncpy(term->window->title, title, WINDOW_TITLE_MAX_LEN - 1);
    term->window->title[WINDOW_TITLE_MAX_LEN - 1] = '\0';
}

void terminal_bell(terminal_t *term)
{
    UNUSED(term);
    /* Could implement visual bell or speaker beep */
    kprintf("[terminal] BELL\n");
}

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

static void terminal_process_char(terminal_t *term, char c)
{
    /* Handle escape sequences */
    switch (term->esc_state) {
        case ESC_STATE_NORMAL:
            if (c == '\033') {
                term->esc_state = ESC_STATE_ESCAPE;
                term->esc_length = 0;
            } else if (c < 32) {
                terminal_process_control(term, c);
            } else {
                terminal_put_char_at(term, term->cursor_x, term->cursor_y, c);
                term->cursor_x++;
                if (term->cursor_x >= (int)term->width) {
                    terminal_new_line(term);
                }
            }
            break;

        case ESC_STATE_ESCAPE:
            if (c == '[') {
                term->esc_state = ESC_STATE_CSI;
            } else if (c == ']') {
                term->esc_state = ESC_STATE_OSC;
            } else if (c == '7') {
                /* Save cursor */
                terminal_save_cursor(term);
                term->esc_state = ESC_STATE_NORMAL;
            } else if (c == '8') {
                /* Restore cursor */
                terminal_restore_cursor(term);
                term->esc_state = ESC_STATE_NORMAL;
            } else if (c == 'c') {
                /* Reset terminal */
                terminal_clear(term);
                terminal_reset_attributes(term);
                term->esc_state = ESC_STATE_NORMAL;
            } else {
                /* Unknown escape, return to normal */
                term->esc_state = ESC_STATE_NORMAL;
            }
            break;

        case ESC_STATE_CSI:
            if (term->esc_length < TERMINAL_ESCAPE_BUFFER_SIZE - 1) {
                term->esc_buffer[term->esc_length++] = c;
                term->esc_buffer[term->esc_length] = '\0';
            }

            /* Check if sequence is complete */
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '@') {
                terminal_process_escape(term, term->esc_buffer);
                term->esc_state = ESC_STATE_NORMAL;
                term->esc_length = 0;
            }
            break;

        case ESC_STATE_OSC:
            /* Operating System Command - typically ends with BEL or ST */
            if (c == '\007') { /* BEL */
                term->esc_state = ESC_STATE_NORMAL;
            } else if (term->esc_length < TERMINAL_ESCAPE_BUFFER_SIZE - 1) {
                term->esc_buffer[term->esc_length++] = c;
            }
            break;
    }
}

static void terminal_process_control(terminal_t *term, char c)
{
    switch (c) {
        case '\n':  /* Line feed */
            terminal_new_line(term);
            break;

        case '\r':  /* Carriage return */
            terminal_carriage_return(term);
            break;

        case '\t':  /* Tab */
            terminal_tab(term);
            break;

        case '\b':  /* Backspace */
            terminal_backspace(term);
            break;

        case '\a':  /* Bell */
            terminal_bell(term);
            break;

        case '\f':  /* Form feed (clear screen) */
            terminal_clear(term);
            break;

        default:
            /* Ignore other control characters */
            break;
    }
}

static void terminal_new_line(terminal_t *term)
{
    term->cursor_x = 0;
    term->cursor_y++;

    if (term->cursor_y >= (int)term->height) {
        term->cursor_y = (int)term->height - 1;
        terminal_scroll(term, 1);
    }
}

static void terminal_carriage_return(terminal_t *term)
{
    term->cursor_x = 0;
}

static void terminal_tab(terminal_t *term)
{
    int next_tab = ((term->cursor_x / TERMINAL_TAB_WIDTH) + 1) * TERMINAL_TAB_WIDTH;

    while (term->cursor_x < next_tab && term->cursor_x < (int)term->width) {
        terminal_put_char_at(term, term->cursor_x, term->cursor_y, ' ');
        term->cursor_x++;
    }

    if (term->cursor_x >= (int)term->width) {
        terminal_new_line(term);
    }
}

static void terminal_backspace(terminal_t *term)
{
    if (term->cursor_x > 0) {
        term->cursor_x--;
        terminal_put_char_at(term, term->cursor_x, term->cursor_y, ' ');
        term->buffer[term->cursor_y].dirty = true;
    }
}

static terminal_line_t *terminal_alloc_line(uint32_t width)
{
    terminal_line_t *line = (terminal_line_t *)kmalloc(sizeof(terminal_line_t));
    if (!line) {
        return NULL;
    }

    line->cells = (terminal_cell_t *)kmalloc(width * sizeof(terminal_cell_t));
    if (!line->cells) {
        kfree(line);
        return NULL;
    }

    line->dirty = true;
    return line;
}

static void terminal_free_line(terminal_line_t *line)
{
    if (line) {
        if (line->cells) {
            kfree(line->cells);
        }
        kfree(line);
    }
}

static void terminal_clear_cell(terminal_t *term, terminal_cell_t *cell)
{
    cell->ch = ' ';
    cell->fg_color = term->fg_color;
    cell->bg_color = term->bg_color;
    cell->attributes = 0;
}

static void terminal_put_char_at(terminal_t *term, int x, int y, char c)
{
    terminal_cell_t *cell;

    if (x < 0 || x >= (int)term->width || y < 0 || y >= (int)term->height) {
        return;
    }

    cell = &term->buffer[y].cells[x];
    cell->ch = c;
    cell->fg_color = term->fg_color;
    cell->bg_color = term->bg_color;
    cell->attributes = term->attributes;

    term->buffer[y].dirty = true;
}
