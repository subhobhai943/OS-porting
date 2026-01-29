/**
 * AAAos Terminal Emulator
 *
 * A terminal emulator application that provides text output with scrollback,
 * keyboard input handling, ANSI escape code support, and shell command execution.
 * Works in GUI window mode via the compositor or fullscreen via framebuffer.
 */

#ifndef _AAAOS_APP_TERMINAL_H
#define _AAAOS_APP_TERMINAL_H

#include "../../kernel/include/types.h"
#include "../../gui/compositor/compositor.h"
#include "../../drivers/input/keyboard.h"

/* Terminal configuration */
#define TERMINAL_DEFAULT_WIDTH      80      /* Default terminal width in characters */
#define TERMINAL_DEFAULT_HEIGHT     25      /* Default terminal height in characters */
#define TERMINAL_MAX_WIDTH          256     /* Maximum terminal width */
#define TERMINAL_MAX_HEIGHT         128     /* Maximum terminal height */
#define TERMINAL_SCROLLBACK_LINES   1000    /* Number of scrollback lines */
#define TERMINAL_INPUT_BUFFER_SIZE  256     /* Input buffer size */
#define TERMINAL_ESCAPE_BUFFER_SIZE 32      /* Escape sequence buffer size */
#define TERMINAL_TAB_WIDTH          8       /* Tab stop width */

/* Terminal font dimensions (using framebuffer font) */
#define TERMINAL_CHAR_WIDTH         8       /* Character width in pixels */
#define TERMINAL_CHAR_HEIGHT        16      /* Character height in pixels */

/* ANSI escape sequence states */
typedef enum {
    ESC_STATE_NORMAL,           /* Normal character processing */
    ESC_STATE_ESCAPE,           /* Received ESC (0x1B) */
    ESC_STATE_CSI,              /* Received ESC [ (Control Sequence Introducer) */
    ESC_STATE_OSC               /* Received ESC ] (Operating System Command) */
} terminal_esc_state_t;

/* ANSI color codes (indices into color palette) */
typedef enum {
    ANSI_COLOR_BLACK = 0,
    ANSI_COLOR_RED,
    ANSI_COLOR_GREEN,
    ANSI_COLOR_YELLOW,
    ANSI_COLOR_BLUE,
    ANSI_COLOR_MAGENTA,
    ANSI_COLOR_CYAN,
    ANSI_COLOR_WHITE,
    ANSI_COLOR_BRIGHT_BLACK,    /* Dark gray */
    ANSI_COLOR_BRIGHT_RED,
    ANSI_COLOR_BRIGHT_GREEN,
    ANSI_COLOR_BRIGHT_YELLOW,
    ANSI_COLOR_BRIGHT_BLUE,
    ANSI_COLOR_BRIGHT_MAGENTA,
    ANSI_COLOR_BRIGHT_CYAN,
    ANSI_COLOR_BRIGHT_WHITE,
    ANSI_COLOR_MAX
} ansi_color_t;

/* Text attributes */
#define ATTR_BOLD           0x01    /* Bold text */
#define ATTR_UNDERLINE      0x02    /* Underlined text */
#define ATTR_BLINK          0x04    /* Blinking text */
#define ATTR_INVERSE        0x08    /* Inverse video */
#define ATTR_HIDDEN         0x10    /* Hidden text */

/**
 * Terminal character cell
 * Represents a single character with its attributes
 */
typedef struct {
    char        ch;             /* Character */
    uint8_t     fg_color;       /* Foreground color index */
    uint8_t     bg_color;       /* Background color index */
    uint8_t     attributes;     /* Text attributes (bold, etc.) */
} terminal_cell_t;

/**
 * Terminal line
 * A single line in the terminal buffer
 */
typedef struct {
    terminal_cell_t *cells;     /* Array of character cells */
    bool            dirty;      /* Line needs to be redrawn */
} terminal_line_t;

/**
 * Terminal structure
 * Contains all state for a terminal instance
 */
typedef struct terminal {
    /* Dimensions (in characters) */
    uint32_t        width;              /* Terminal width in characters */
    uint32_t        height;             /* Terminal height in characters */

    /* Cursor position */
    int32_t         cursor_x;           /* Cursor column (0-based) */
    int32_t         cursor_y;           /* Cursor row (0-based) */
    bool            cursor_visible;     /* Cursor is visible */
    bool            cursor_blink_state; /* Current blink state */

    /* Screen buffer (visible area) */
    terminal_line_t *buffer;            /* Array of lines for visible area */

    /* Scrollback buffer */
    terminal_line_t *scrollback;        /* Array of scrollback lines */
    uint32_t        scrollback_size;    /* Total scrollback capacity */
    uint32_t        scrollback_count;   /* Number of lines in scrollback */
    uint32_t        scrollback_head;    /* Index of oldest line in scrollback */
    int32_t         scroll_offset;      /* Current scroll offset (0 = bottom) */

    /* Current text attributes */
    uint8_t         fg_color;           /* Current foreground color */
    uint8_t         bg_color;           /* Current background color */
    uint8_t         attributes;         /* Current text attributes */
    uint8_t         default_fg;         /* Default foreground color */
    uint8_t         default_bg;         /* Default background color */

    /* Escape sequence parsing */
    terminal_esc_state_t esc_state;     /* Current escape state */
    char            esc_buffer[TERMINAL_ESCAPE_BUFFER_SIZE]; /* Escape sequence buffer */
    uint32_t        esc_length;         /* Current escape sequence length */

    /* Input handling */
    char            input_buffer[TERMINAL_INPUT_BUFFER_SIZE]; /* Input line buffer */
    uint32_t        input_length;       /* Current input length */
    uint32_t        input_pos;          /* Cursor position in input */

    /* GUI window (NULL for fullscreen mode) */
    window_t        *window;            /* Associated window */
    bool            fullscreen;         /* Running in fullscreen mode */
    bool            needs_redraw;       /* Screen needs full redraw */

    /* State */
    bool            running;            /* Terminal is running */
    bool            echo_enabled;       /* Echo input characters */
    bool            line_mode;          /* Line-buffered input mode */
    bool            insert_mode;        /* Insert mode (vs overwrite) */

    /* Saved cursor position (for save/restore) */
    int32_t         saved_cursor_x;
    int32_t         saved_cursor_y;

    /* Color palette */
    uint32_t        palette[ANSI_COLOR_MAX]; /* ARGB color palette */

} terminal_t;

/*============================================================================
 * Terminal Subsystem Initialization
 *============================================================================*/

/**
 * Initialize the terminal subsystem
 * Sets up shared resources for all terminal instances.
 * @return 0 on success, negative error code on failure
 */
int terminal_init(void);

/**
 * Shutdown the terminal subsystem
 * Cleans up all shared resources.
 */
void terminal_shutdown(void);

/*============================================================================
 * Terminal Instance Management
 *============================================================================*/

/**
 * Create a new terminal instance
 * @param width_chars Terminal width in characters
 * @param height_chars Terminal height in characters
 * @return Pointer to new terminal, or NULL on failure
 */
terminal_t *terminal_create(uint32_t width_chars, uint32_t height_chars);

/**
 * Destroy a terminal instance and free all resources
 * @param term Terminal to destroy
 */
void terminal_destroy(terminal_t *term);

/**
 * Create a terminal in a GUI window
 * @param width_chars Terminal width in characters
 * @param height_chars Terminal height in characters
 * @param title Window title
 * @return Pointer to new terminal, or NULL on failure
 */
terminal_t *terminal_create_windowed(uint32_t width_chars, uint32_t height_chars,
                                     const char *title);

/**
 * Create a fullscreen terminal
 * Uses the entire framebuffer.
 * @return Pointer to new terminal, or NULL on failure
 */
terminal_t *terminal_create_fullscreen(void);

/*============================================================================
 * Output Functions
 *============================================================================*/

/**
 * Output a single character to the terminal
 * Handles control characters and escape sequences.
 * @param term Terminal instance
 * @param c Character to output
 */
void terminal_putc(terminal_t *term, char c);

/**
 * Output a null-terminated string to the terminal
 * @param term Terminal instance
 * @param str String to output
 */
void terminal_puts(terminal_t *term, const char *str);

/**
 * Output a formatted string to the terminal (printf-style)
 * @param term Terminal instance
 * @param fmt Format string
 * @param ... Variable arguments
 * @return Number of characters written
 */
int terminal_printf(terminal_t *term, const char *fmt, ...);

/**
 * Clear the terminal screen
 * Fills screen with current background color and moves cursor to home.
 * @param term Terminal instance
 */
void terminal_clear(terminal_t *term);

/**
 * Clear from cursor to end of screen
 * @param term Terminal instance
 */
void terminal_clear_to_end(terminal_t *term);

/**
 * Clear from start of screen to cursor
 * @param term Terminal instance
 */
void terminal_clear_to_start(terminal_t *term);

/**
 * Clear the current line
 * @param term Terminal instance
 * @param mode 0 = cursor to end, 1 = start to cursor, 2 = entire line
 */
void terminal_clear_line(terminal_t *term, int mode);

/*============================================================================
 * Cursor Functions
 *============================================================================*/

/**
 * Set cursor position
 * @param term Terminal instance
 * @param x Column (0-based)
 * @param y Row (0-based)
 */
void terminal_set_cursor(terminal_t *term, int x, int y);

/**
 * Move cursor relative to current position
 * @param term Terminal instance
 * @param dx Horizontal movement (negative = left)
 * @param dy Vertical movement (negative = up)
 */
void terminal_move_cursor(terminal_t *term, int dx, int dy);

/**
 * Save current cursor position
 * @param term Terminal instance
 */
void terminal_save_cursor(terminal_t *term);

/**
 * Restore saved cursor position
 * @param term Terminal instance
 */
void terminal_restore_cursor(terminal_t *term);

/**
 * Show or hide the cursor
 * @param term Terminal instance
 * @param visible true to show, false to hide
 */
void terminal_set_cursor_visible(terminal_t *term, bool visible);

/*============================================================================
 * Color and Attribute Functions
 *============================================================================*/

/**
 * Set foreground and background colors
 * @param term Terminal instance
 * @param fg Foreground color index (0-15)
 * @param bg Background color index (0-15)
 */
void terminal_set_color(terminal_t *term, uint8_t fg, uint8_t bg);

/**
 * Set text attributes
 * @param term Terminal instance
 * @param attributes Attribute flags (ATTR_BOLD, etc.)
 */
void terminal_set_attributes(terminal_t *term, uint8_t attributes);

/**
 * Reset colors and attributes to defaults
 * @param term Terminal instance
 */
void terminal_reset_attributes(terminal_t *term);

/**
 * Set a custom color in the palette
 * @param term Terminal instance
 * @param index Color index (0-15)
 * @param color 32-bit ARGB color value
 */
void terminal_set_palette_color(terminal_t *term, uint8_t index, uint32_t color);

/*============================================================================
 * Scrolling Functions
 *============================================================================*/

/**
 * Scroll the terminal buffer
 * @param term Terminal instance
 * @param lines Number of lines to scroll (positive = up, negative = down)
 */
void terminal_scroll(terminal_t *term, int lines);

/**
 * Scroll the view (for scrollback)
 * @param term Terminal instance
 * @param lines Number of lines to scroll view (positive = up into history)
 */
void terminal_scroll_view(terminal_t *term, int lines);

/**
 * Scroll view to bottom (most recent output)
 * @param term Terminal instance
 */
void terminal_scroll_to_bottom(terminal_t *term);

/*============================================================================
 * Input Handling Functions
 *============================================================================*/

/**
 * Handle keyboard input
 * Processes key events and handles line editing.
 * @param term Terminal instance
 * @param key Key event from keyboard driver
 * @return true if input should be processed by shell, false otherwise
 */
bool terminal_handle_input(terminal_t *term, key_event_t *key);

/**
 * Get a line of input from the terminal
 * Blocks until user presses Enter.
 * @param term Terminal instance
 * @param buffer Buffer to store input
 * @param max_length Maximum input length
 * @return Number of characters read, or -1 on error
 */
int terminal_getline(terminal_t *term, char *buffer, size_t max_length);

/**
 * Get a single character from the terminal
 * Blocks until a character is available.
 * @param term Terminal instance
 * @return Character read, or -1 on error
 */
int terminal_getchar(terminal_t *term);

/**
 * Check if input is available
 * @param term Terminal instance
 * @return true if input is waiting
 */
bool terminal_has_input(terminal_t *term);

/*============================================================================
 * ANSI Escape Sequence Processing
 *============================================================================*/

/**
 * Process an ANSI escape sequence
 * @param term Terminal instance
 * @param seq Escape sequence string (without ESC and [)
 * @return true if sequence was recognized and handled
 */
bool terminal_process_escape(terminal_t *term, const char *seq);

/*============================================================================
 * Rendering Functions
 *============================================================================*/

/**
 * Draw/render the terminal to its output surface
 * Updates the window buffer or framebuffer.
 * @param term Terminal instance
 */
void terminal_draw(terminal_t *term);

/**
 * Draw a single line of the terminal
 * @param term Terminal instance
 * @param line Line number to draw
 */
void terminal_draw_line(terminal_t *term, int line);

/**
 * Draw the cursor
 * @param term Terminal instance
 */
void terminal_draw_cursor(terminal_t *term);

/**
 * Update cursor blink state
 * @param term Terminal instance
 */
void terminal_blink_cursor(terminal_t *term);

/*============================================================================
 * Main Loop Functions
 *============================================================================*/

/**
 * Run the terminal main loop
 * Handles input, processes commands, and renders output.
 * Does not return until terminal is closed.
 * @param term Terminal instance
 */
void terminal_run(terminal_t *term);

/**
 * Execute a command in the terminal
 * @param term Terminal instance
 * @param command Command string to execute
 * @return Command exit status
 */
int terminal_execute(terminal_t *term, const char *command);

/**
 * Stop the terminal
 * Causes terminal_run to return.
 * @param term Terminal instance
 */
void terminal_stop(terminal_t *term);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Get the terminal window (if windowed mode)
 * @param term Terminal instance
 * @return Window pointer, or NULL if fullscreen
 */
window_t *terminal_get_window(terminal_t *term);

/**
 * Resize the terminal
 * @param term Terminal instance
 * @param new_width New width in characters
 * @param new_height New height in characters
 * @return 0 on success, negative error code on failure
 */
int terminal_resize(terminal_t *term, uint32_t new_width, uint32_t new_height);

/**
 * Set terminal title (updates window title if windowed)
 * @param term Terminal instance
 * @param title New title string
 */
void terminal_set_title(terminal_t *term, const char *title);

/**
 * Ring the terminal bell
 * @param term Terminal instance
 */
void terminal_bell(terminal_t *term);

#endif /* _AAAOS_APP_TERMINAL_H */
