/**
 * AAAos GUI - Textbox Widget
 *
 * Provides a text input widget for single-line text entry
 * with cursor support and basic editing capabilities.
 */

#ifndef _AAAOS_GUI_TEXTBOX_H
#define _AAAOS_GUI_TEXTBOX_H

#include "widget.h"

/* Maximum textbox content length */
#define TEXTBOX_MAX_TEXT        256

/* Default textbox dimensions */
#define TEXTBOX_DEFAULT_HEIGHT  24
#define TEXTBOX_PADDING         4

/* Textbox colors (32-bit ARGB) */
#define TEXTBOX_COLOR_BG            0xFF2A2A2A  /* Dark background */
#define TEXTBOX_COLOR_BG_FOCUSED    0xFF3A3A3A  /* Focused background */
#define TEXTBOX_COLOR_BG_DISABLED   0xFF1A1A1A  /* Disabled background */
#define TEXTBOX_COLOR_BORDER        0xFF5A5A5A  /* Border color */
#define TEXTBOX_COLOR_BORDER_FOCUS  0xFF007ACC  /* Blue border when focused */
#define TEXTBOX_COLOR_TEXT          0xFFFFFFFF  /* Text color */
#define TEXTBOX_COLOR_TEXT_DISABLED 0xFF888888  /* Disabled text color */
#define TEXTBOX_COLOR_CURSOR        0xFFFFFFFF  /* Cursor color */
#define TEXTBOX_COLOR_SELECTION     0x80007ACC  /* Selection highlight */
#define TEXTBOX_COLOR_PLACEHOLDER   0xFF888888  /* Placeholder text color */

/**
 * Textbox widget structure
 * Extends the base widget with textbox-specific properties
 */
typedef struct textbox {
    widget_t base;                          /* Base widget (must be first) */

    char text[TEXTBOX_MAX_TEXT];            /* Text content */
    char placeholder[TEXTBOX_MAX_TEXT];     /* Placeholder text */
    size_t text_length;                     /* Current text length */

    /* Cursor and selection */
    size_t cursor_pos;                      /* Cursor position in text */
    size_t selection_start;                 /* Selection start position */
    size_t selection_end;                   /* Selection end position */
    bool has_selection;                     /* Is there an active selection? */

    /* Scrolling */
    size_t scroll_offset;                   /* Character offset for scrolling */

    /* Cursor blink state */
    bool cursor_visible;                    /* Is cursor currently visible? */
    uint32_t cursor_blink_time;             /* Time since last cursor blink */

    /* Colors */
    uint32_t color_bg;                      /* Background color */
    uint32_t color_border;                  /* Border color */
    uint32_t color_text;                    /* Text color */
    uint32_t color_cursor;                  /* Cursor color */

    /* Flags */
    bool read_only;                         /* Is textbox read-only? */
    bool password_mode;                     /* Show dots instead of text? */
    size_t max_length;                      /* Maximum text length (0 = unlimited) */

    /* Callbacks */
    void (*on_change)(struct textbox *tb);  /* Called when text changes */
    void (*on_enter)(struct textbox *tb);   /* Called when Enter is pressed */
    void *callback_data;                    /* User data for callbacks */
} textbox_t;

/**
 * Create a new textbox widget
 * @param x X position
 * @param y Y position
 * @param width Textbox width
 * @param height Textbox height (0 for default)
 * @return Pointer to new textbox or NULL on failure
 */
textbox_t *textbox_create(int32_t x, int32_t y, int32_t width, int32_t height);

/**
 * Initialize an existing textbox structure
 * @param tb Textbox to initialize
 * @param x X position
 * @param y Y position
 * @param width Textbox width
 * @param height Textbox height (0 for default)
 */
void textbox_init(textbox_t *tb, int32_t x, int32_t y, int32_t width, int32_t height);

/**
 * Get textbox text content
 * @param tb Textbox
 * @param buf Buffer to store text
 * @param max Maximum buffer size
 * @return Number of characters copied (not including null terminator)
 */
size_t textbox_get_text(textbox_t *tb, char *buf, size_t max);

/**
 * Set textbox text content
 * @param tb Textbox
 * @param text New text content
 */
void textbox_set_text(textbox_t *tb, const char *text);

/**
 * Append text to textbox
 * @param tb Textbox
 * @param text Text to append
 */
void textbox_append_text(textbox_t *tb, const char *text);

/**
 * Insert text at cursor position
 * @param tb Textbox
 * @param text Text to insert
 */
void textbox_insert_text(textbox_t *tb, const char *text);

/**
 * Delete selected text or character at cursor
 * @param tb Textbox
 * @param forward true to delete forward (Delete key), false to delete backward (Backspace)
 */
void textbox_delete(textbox_t *tb, bool forward);

/**
 * Clear all text
 * @param tb Textbox
 */
void textbox_clear(textbox_t *tb);

/**
 * Set cursor position
 * @param tb Textbox
 * @param pos New cursor position
 */
void textbox_set_cursor(textbox_t *tb, size_t pos);

/**
 * Get cursor position
 * @param tb Textbox
 * @return Current cursor position
 */
size_t textbox_get_cursor(textbox_t *tb);

/**
 * Move cursor
 * @param tb Textbox
 * @param delta Number of positions to move (negative = left, positive = right)
 * @param extend_selection true to extend selection
 */
void textbox_move_cursor(textbox_t *tb, int32_t delta, bool extend_selection);

/**
 * Select all text
 * @param tb Textbox
 */
void textbox_select_all(textbox_t *tb);

/**
 * Clear selection
 * @param tb Textbox
 */
void textbox_clear_selection(textbox_t *tb);

/**
 * Get selected text
 * @param tb Textbox
 * @param buf Buffer to store selected text
 * @param max Maximum buffer size
 * @return Number of characters copied
 */
size_t textbox_get_selection(textbox_t *tb, char *buf, size_t max);

/**
 * Set placeholder text
 * @param tb Textbox
 * @param text Placeholder text
 */
void textbox_set_placeholder(textbox_t *tb, const char *text);

/**
 * Set read-only mode
 * @param tb Textbox
 * @param read_only true to make read-only
 */
void textbox_set_read_only(textbox_t *tb, bool read_only);

/**
 * Set password mode (shows dots)
 * @param tb Textbox
 * @param password_mode true to enable password mode
 */
void textbox_set_password_mode(textbox_t *tb, bool password_mode);

/**
 * Set maximum text length
 * @param tb Textbox
 * @param max_length Maximum length (0 = unlimited up to TEXTBOX_MAX_TEXT)
 */
void textbox_set_max_length(textbox_t *tb, size_t max_length);

/**
 * Set change callback
 * @param tb Textbox
 * @param callback Callback function
 * @param data User data
 */
void textbox_set_on_change(textbox_t *tb, void (*callback)(textbox_t *), void *data);

/**
 * Set enter callback
 * @param tb Textbox
 * @param callback Callback function
 * @param data User data
 */
void textbox_set_on_enter(textbox_t *tb, void (*callback)(textbox_t *), void *data);

/**
 * Update cursor blink state (call periodically)
 * @param tb Textbox
 * @param delta_ms Milliseconds since last update
 */
void textbox_update(textbox_t *tb, uint32_t delta_ms);

/**
 * Destroy a textbox and free its resources
 * @param tb Textbox to destroy
 */
void textbox_destroy(textbox_t *tb);

#endif /* _AAAOS_GUI_TEXTBOX_H */
