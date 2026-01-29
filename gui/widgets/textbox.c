/**
 * AAAos GUI - Textbox Widget Implementation
 *
 * Implements a text input widget for single-line text entry
 * with cursor support and basic editing capabilities.
 */

#include "textbox.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"
#include "../../drivers/input/keyboard.h"

/* Font metrics (assumed fixed-width font) */
#define FONT_CHAR_WIDTH     8
#define FONT_CHAR_HEIGHT    16

/* Cursor blink interval in milliseconds */
#define CURSOR_BLINK_INTERVAL   500

/* Forward declarations for internal functions */
static void textbox_draw_impl(widget_t *w, void *buffer);
static void textbox_key_handler(widget_t *w, event_t *event);
static void textbox_mouse_handler(widget_t *w, event_t *event);
static void textbox_focus_handler(widget_t *w, event_t *event);
static void textbox_ensure_cursor_visible(textbox_t *tb);
static void textbox_delete_selection(textbox_t *tb);

/**
 * Draw a filled rectangle (placeholder for graphics driver)
 */
static void draw_filled_rect(void *buffer, int32_t x, int32_t y,
                             int32_t width, int32_t height, uint32_t color) {
    UNUSED(buffer);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    UNUSED(color);
}

/**
 * Draw a rectangle border (placeholder for graphics driver)
 */
static void draw_rect_border(void *buffer, int32_t x, int32_t y,
                             int32_t width, int32_t height, uint32_t color) {
    UNUSED(buffer);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    UNUSED(color);
}

/**
 * Draw text at position (placeholder for graphics driver)
 */
static void draw_text(void *buffer, int32_t x, int32_t y,
                      const char *text, uint32_t color) {
    UNUSED(buffer);
    UNUSED(x);
    UNUSED(y);
    UNUSED(text);
    UNUSED(color);
}

/**
 * Create a new textbox widget
 */
textbox_t *textbox_create(int32_t x, int32_t y, int32_t width, int32_t height) {
    textbox_t *tb = (textbox_t *)kmalloc(sizeof(textbox_t));
    if (!tb) {
        kprintf("[TEXTBOX] ERROR: Failed to allocate textbox\n");
        return NULL;
    }

    textbox_init(tb, x, y, width, height);

    kprintf("[TEXTBOX] Created textbox at (%d, %d) size %dx%d\n",
            x, y, width, height > 0 ? height : TEXTBOX_DEFAULT_HEIGHT);

    return tb;
}

/**
 * Initialize an existing textbox structure
 */
void textbox_init(textbox_t *tb, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!tb) {
        kprintf("[TEXTBOX] ERROR: textbox_init called with NULL textbox\n");
        return;
    }

    /* Use default height if not specified */
    if (height <= 0) {
        height = TEXTBOX_DEFAULT_HEIGHT;
    }

    /* Initialize base widget */
    widget_init(&tb->base, x, y, width, height);

    /* Initialize text buffer */
    tb->text[0] = '\0';
    tb->placeholder[0] = '\0';
    tb->text_length = 0;

    /* Initialize cursor and selection */
    tb->cursor_pos = 0;
    tb->selection_start = 0;
    tb->selection_end = 0;
    tb->has_selection = false;

    /* Initialize scrolling */
    tb->scroll_offset = 0;

    /* Initialize cursor blink */
    tb->cursor_visible = true;
    tb->cursor_blink_time = 0;

    /* Set default colors */
    tb->color_bg = TEXTBOX_COLOR_BG;
    tb->color_border = TEXTBOX_COLOR_BORDER;
    tb->color_text = TEXTBOX_COLOR_TEXT;
    tb->color_cursor = TEXTBOX_COLOR_CURSOR;

    /* Set default flags */
    tb->read_only = false;
    tb->password_mode = false;
    tb->max_length = 0;  /* Unlimited (up to buffer size) */

    /* Clear callbacks */
    tb->on_change = NULL;
    tb->on_enter = NULL;
    tb->callback_data = NULL;

    /* Set up virtual functions and event handlers */
    tb->base.draw = textbox_draw_impl;
    tb->base.on_key = textbox_key_handler;
    tb->base.on_mouse = textbox_mouse_handler;
    tb->base.on_focus = textbox_focus_handler;

    kprintf("[TEXTBOX] Initialized textbox (widget %u)\n", tb->base.id);
}

/**
 * Ensure cursor is visible (adjust scroll offset)
 */
static void textbox_ensure_cursor_visible(textbox_t *tb) {
    if (!tb) return;

    /* Calculate visible area in characters */
    int32_t visible_chars = (tb->base.width - 2 * TEXTBOX_PADDING) / FONT_CHAR_WIDTH;
    if (visible_chars <= 0) visible_chars = 1;

    /* Adjust scroll offset if cursor is outside visible area */
    if (tb->cursor_pos < tb->scroll_offset) {
        tb->scroll_offset = tb->cursor_pos;
    } else if (tb->cursor_pos >= tb->scroll_offset + (size_t)visible_chars) {
        tb->scroll_offset = tb->cursor_pos - (size_t)visible_chars + 1;
    }
}

/**
 * Delete selected text
 */
static void textbox_delete_selection(textbox_t *tb) {
    if (!tb || !tb->has_selection) return;

    size_t start = tb->selection_start < tb->selection_end ?
                   tb->selection_start : tb->selection_end;
    size_t end = tb->selection_start < tb->selection_end ?
                 tb->selection_end : tb->selection_start;

    /* Move text after selection to start position */
    memmove(&tb->text[start], &tb->text[end], tb->text_length - end + 1);
    tb->text_length -= (end - start);
    tb->cursor_pos = start;
    tb->has_selection = false;

    textbox_ensure_cursor_visible(tb);
    widget_invalidate(&tb->base);
}

/**
 * Internal draw function for textboxes
 */
static void textbox_draw_impl(widget_t *w, void *buffer) {
    if (!w || !buffer) return;

    textbox_t *tb = (textbox_t *)w;

    /* Get absolute position */
    int32_t abs_x, abs_y;
    widget_get_absolute_pos(w, &abs_x, &abs_y);

    /* Determine colors based on state */
    uint32_t bg_color = tb->color_bg;
    uint32_t border_color = tb->color_border;
    uint32_t text_color = tb->color_text;

    if (!(w->flags & WIDGET_FLAG_ENABLED)) {
        bg_color = TEXTBOX_COLOR_BG_DISABLED;
        text_color = TEXTBOX_COLOR_TEXT_DISABLED;
    } else if (w->flags & WIDGET_FLAG_FOCUSED) {
        bg_color = TEXTBOX_COLOR_BG_FOCUSED;
        border_color = TEXTBOX_COLOR_BORDER_FOCUS;
    }

    /* Draw background */
    draw_filled_rect(buffer, abs_x, abs_y, w->width, w->height, bg_color);

    /* Draw border */
    draw_rect_border(buffer, abs_x, abs_y, w->width, w->height, border_color);

    /* Calculate text area */
    int32_t text_x = abs_x + TEXTBOX_PADDING;
    int32_t text_y = abs_y + (w->height - FONT_CHAR_HEIGHT) / 2;
    int32_t visible_width = w->width - 2 * TEXTBOX_PADDING;

    /* Determine what text to display */
    const char *display_text = tb->text;
    char password_buf[TEXTBOX_MAX_TEXT];

    /* Handle placeholder */
    if (tb->text_length == 0 && tb->placeholder[0] != '\0' &&
        !(w->flags & WIDGET_FLAG_FOCUSED)) {
        display_text = tb->placeholder;
        text_color = TEXTBOX_COLOR_PLACEHOLDER;
    }
    /* Handle password mode */
    else if (tb->password_mode && tb->text_length > 0) {
        size_t i;
        for (i = 0; i < tb->text_length && i < TEXTBOX_MAX_TEXT - 1; i++) {
            password_buf[i] = '*';
        }
        password_buf[i] = '\0';
        display_text = password_buf;
    }

    /* Draw selection highlight */
    if (tb->has_selection && (w->flags & WIDGET_FLAG_FOCUSED)) {
        size_t start = tb->selection_start < tb->selection_end ?
                       tb->selection_start : tb->selection_end;
        size_t end = tb->selection_start < tb->selection_end ?
                     tb->selection_end : tb->selection_start;

        /* Only draw visible part of selection */
        if (start < tb->scroll_offset) start = tb->scroll_offset;

        int32_t sel_x = text_x + (int32_t)(start - tb->scroll_offset) * FONT_CHAR_WIDTH;
        int32_t sel_width = (int32_t)(end - start) * FONT_CHAR_WIDTH;

        /* Clip to visible area */
        if (sel_x < text_x) {
            sel_width -= (text_x - sel_x);
            sel_x = text_x;
        }
        if (sel_x + sel_width > text_x + visible_width) {
            sel_width = text_x + visible_width - sel_x;
        }

        if (sel_width > 0) {
            draw_filled_rect(buffer, sel_x, text_y, sel_width,
                           FONT_CHAR_HEIGHT, TEXTBOX_COLOR_SELECTION);
        }
    }

    /* Draw visible text */
    if (display_text[0] != '\0') {
        /* Get visible portion of text */
        size_t display_len = strlen(display_text);
        if (display_len > tb->scroll_offset) {
            const char *visible_text = display_text + tb->scroll_offset;
            draw_text(buffer, text_x, text_y, visible_text, text_color);
        }
    }

    /* Draw cursor if focused and visible */
    if ((w->flags & WIDGET_FLAG_FOCUSED) && tb->cursor_visible &&
        (w->flags & WIDGET_FLAG_ENABLED)) {
        int32_t cursor_x = text_x +
            (int32_t)(tb->cursor_pos - tb->scroll_offset) * FONT_CHAR_WIDTH;

        if (cursor_x >= text_x && cursor_x < text_x + visible_width) {
            draw_filled_rect(buffer, cursor_x, text_y, 2,
                           FONT_CHAR_HEIGHT, tb->color_cursor);
        }
    }

    kprintf("[TEXTBOX] Drew textbox, cursor_pos=%zu, text='%s'\n",
            tb->cursor_pos, tb->text);
}

/**
 * Key event handler for textbox
 */
static void textbox_key_handler(widget_t *w, event_t *event) {
    if (!w || !event) return;

    textbox_t *tb = (textbox_t *)w;

    /* Don't handle if disabled or not focused */
    if (!(w->flags & WIDGET_FLAG_ENABLED) || !(w->flags & WIDGET_FLAG_FOCUSED)) {
        return;
    }

    if (event->type != EVENT_KEY_DOWN && event->type != EVENT_KEY_CHAR) {
        return;
    }

    bool text_changed = false;
    bool shift_held = (event->key.modifiers & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
    bool ctrl_held = (event->key.modifiers & (MOD_LCTRL | MOD_RCTRL)) != 0;

    /* Handle special keys */
    switch (event->key.keycode) {
        case KEY_LEFT:
            textbox_move_cursor(tb, -1, shift_held);
            event->handled = true;
            break;

        case KEY_RIGHT:
            textbox_move_cursor(tb, 1, shift_held);
            event->handled = true;
            break;

        case KEY_HOME:
            if (shift_held && tb->cursor_pos > 0) {
                if (!tb->has_selection) {
                    tb->selection_start = tb->cursor_pos;
                    tb->has_selection = true;
                }
                tb->selection_end = 0;
            } else {
                tb->has_selection = false;
            }
            tb->cursor_pos = 0;
            textbox_ensure_cursor_visible(tb);
            widget_invalidate(w);
            event->handled = true;
            break;

        case KEY_END:
            if (shift_held && tb->cursor_pos < tb->text_length) {
                if (!tb->has_selection) {
                    tb->selection_start = tb->cursor_pos;
                    tb->has_selection = true;
                }
                tb->selection_end = tb->text_length;
            } else {
                tb->has_selection = false;
            }
            tb->cursor_pos = tb->text_length;
            textbox_ensure_cursor_visible(tb);
            widget_invalidate(w);
            event->handled = true;
            break;

        case KEY_BACKSPACE:
            if (!tb->read_only) {
                textbox_delete(tb, false);
                text_changed = true;
            }
            event->handled = true;
            break;

        case KEY_DELETE:
            if (!tb->read_only) {
                textbox_delete(tb, true);
                text_changed = true;
            }
            event->handled = true;
            break;

        case KEY_ENTER:
            if (tb->on_enter) {
                tb->on_enter(tb);
            }
            event->handled = true;
            break;

        case KEY_A:
            /* Ctrl+A = Select All */
            if (ctrl_held) {
                textbox_select_all(tb);
                event->handled = true;
            }
            break;

        default:
            break;
    }

    /* Handle printable characters */
    if (!event->handled && event->key.ascii >= 32 && event->key.ascii < 127) {
        if (!tb->read_only) {
            char str[2] = { event->key.ascii, '\0' };
            textbox_insert_text(tb, str);
            text_changed = true;
        }
        event->handled = true;
    }

    /* Trigger change callback if text changed */
    if (text_changed && tb->on_change) {
        tb->on_change(tb);
    }

    /* Reset cursor blink on any key press */
    tb->cursor_visible = true;
    tb->cursor_blink_time = 0;
}

/**
 * Mouse event handler for textbox
 */
static void textbox_mouse_handler(widget_t *w, event_t *event) {
    if (!w || !event) return;

    textbox_t *tb = (textbox_t *)w;

    /* Don't handle if disabled */
    if (!(w->flags & WIDGET_FLAG_ENABLED)) {
        return;
    }

    if (event->type == EVENT_MOUSE_DOWN) {
        /* Get click position */
        widget_set_focus(w);

        /* Calculate character position from click */
        int32_t abs_x, abs_y;
        widget_get_absolute_pos(w, &abs_x, &abs_y);

        int32_t click_offset = event->mouse.global_x - abs_x - TEXTBOX_PADDING;
        size_t char_pos = tb->scroll_offset + (size_t)(click_offset / FONT_CHAR_WIDTH);

        if (char_pos > tb->text_length) {
            char_pos = tb->text_length;
        }

        tb->cursor_pos = char_pos;
        tb->has_selection = false;
        tb->cursor_visible = true;
        tb->cursor_blink_time = 0;

        widget_invalidate(w);
        event->handled = true;

        kprintf("[TEXTBOX] Click set cursor to position %zu\n", char_pos);
    }
}

/**
 * Focus event handler for textbox
 */
static void textbox_focus_handler(widget_t *w, event_t *event) {
    if (!w || !event) return;

    textbox_t *tb = (textbox_t *)w;

    if (event->type == EVENT_FOCUS_GAINED) {
        tb->cursor_visible = true;
        tb->cursor_blink_time = 0;
        kprintf("[TEXTBOX] Textbox gained focus\n");
    } else if (event->type == EVENT_FOCUS_LOST) {
        tb->has_selection = false;
        kprintf("[TEXTBOX] Textbox lost focus\n");
    }

    widget_invalidate(w);
}

/**
 * Get textbox text content
 */
size_t textbox_get_text(textbox_t *tb, char *buf, size_t max) {
    if (!tb || !buf || max == 0) {
        kprintf("[TEXTBOX] ERROR: textbox_get_text invalid parameters\n");
        return 0;
    }

    size_t copy_len = tb->text_length;
    if (copy_len >= max) {
        copy_len = max - 1;
    }

    memcpy(buf, tb->text, copy_len);
    buf[copy_len] = '\0';

    return copy_len;
}

/**
 * Set textbox text content
 */
void textbox_set_text(textbox_t *tb, const char *text) {
    if (!tb) {
        kprintf("[TEXTBOX] ERROR: textbox_set_text called with NULL textbox\n");
        return;
    }

    if (text) {
        size_t len = strlen(text);
        size_t max_len = tb->max_length > 0 ? tb->max_length : TEXTBOX_MAX_TEXT - 1;
        if (len > max_len) {
            len = max_len;
        }

        memcpy(tb->text, text, len);
        tb->text[len] = '\0';
        tb->text_length = len;
    } else {
        tb->text[0] = '\0';
        tb->text_length = 0;
    }

    /* Reset cursor and selection */
    tb->cursor_pos = tb->text_length;
    tb->has_selection = false;
    tb->scroll_offset = 0;
    textbox_ensure_cursor_visible(tb);

    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Text set to '%s'\n", tb->text);
}

/**
 * Append text to textbox
 */
void textbox_append_text(textbox_t *tb, const char *text) {
    if (!tb || !text) return;

    size_t add_len = strlen(text);
    size_t max_len = tb->max_length > 0 ? tb->max_length : TEXTBOX_MAX_TEXT - 1;
    size_t available = max_len - tb->text_length;

    if (add_len > available) {
        add_len = available;
    }

    if (add_len > 0) {
        memcpy(&tb->text[tb->text_length], text, add_len);
        tb->text_length += add_len;
        tb->text[tb->text_length] = '\0';
        tb->cursor_pos = tb->text_length;

        textbox_ensure_cursor_visible(tb);
        widget_invalidate(&tb->base);
    }

    kprintf("[TEXTBOX] Appended text, total length=%zu\n", tb->text_length);
}

/**
 * Insert text at cursor position
 */
void textbox_insert_text(textbox_t *tb, const char *text) {
    if (!tb || !text || tb->read_only) return;

    /* Delete selection first if any */
    if (tb->has_selection) {
        textbox_delete_selection(tb);
    }

    size_t insert_len = strlen(text);
    size_t max_len = tb->max_length > 0 ? tb->max_length : TEXTBOX_MAX_TEXT - 1;
    size_t available = max_len - tb->text_length;

    if (insert_len > available) {
        insert_len = available;
    }

    if (insert_len > 0) {
        /* Make room for inserted text */
        memmove(&tb->text[tb->cursor_pos + insert_len],
                &tb->text[tb->cursor_pos],
                tb->text_length - tb->cursor_pos + 1);

        /* Insert the text */
        memcpy(&tb->text[tb->cursor_pos], text, insert_len);
        tb->text_length += insert_len;
        tb->cursor_pos += insert_len;

        textbox_ensure_cursor_visible(tb);
        widget_invalidate(&tb->base);
    }

    kprintf("[TEXTBOX] Inserted '%s' at position %zu\n", text, tb->cursor_pos - insert_len);
}

/**
 * Delete selected text or character at cursor
 */
void textbox_delete(textbox_t *tb, bool forward) {
    if (!tb || tb->read_only) return;

    /* Delete selection if any */
    if (tb->has_selection) {
        textbox_delete_selection(tb);
        return;
    }

    if (forward) {
        /* Delete character after cursor (Delete key) */
        if (tb->cursor_pos < tb->text_length) {
            memmove(&tb->text[tb->cursor_pos],
                    &tb->text[tb->cursor_pos + 1],
                    tb->text_length - tb->cursor_pos);
            tb->text_length--;
            widget_invalidate(&tb->base);
            kprintf("[TEXTBOX] Deleted character at position %zu\n", tb->cursor_pos);
        }
    } else {
        /* Delete character before cursor (Backspace) */
        if (tb->cursor_pos > 0) {
            memmove(&tb->text[tb->cursor_pos - 1],
                    &tb->text[tb->cursor_pos],
                    tb->text_length - tb->cursor_pos + 1);
            tb->text_length--;
            tb->cursor_pos--;
            textbox_ensure_cursor_visible(tb);
            widget_invalidate(&tb->base);
            kprintf("[TEXTBOX] Backspaced at position %zu\n", tb->cursor_pos + 1);
        }
    }
}

/**
 * Clear all text
 */
void textbox_clear(textbox_t *tb) {
    if (!tb) return;

    tb->text[0] = '\0';
    tb->text_length = 0;
    tb->cursor_pos = 0;
    tb->has_selection = false;
    tb->scroll_offset = 0;

    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Cleared\n");
}

/**
 * Set cursor position
 */
void textbox_set_cursor(textbox_t *tb, size_t pos) {
    if (!tb) return;

    if (pos > tb->text_length) {
        pos = tb->text_length;
    }

    tb->cursor_pos = pos;
    tb->has_selection = false;
    textbox_ensure_cursor_visible(tb);
    widget_invalidate(&tb->base);
}

/**
 * Get cursor position
 */
size_t textbox_get_cursor(textbox_t *tb) {
    if (!tb) return 0;
    return tb->cursor_pos;
}

/**
 * Move cursor
 */
void textbox_move_cursor(textbox_t *tb, int32_t delta, bool extend_selection) {
    if (!tb) return;

    size_t new_pos = tb->cursor_pos;

    if (delta < 0) {
        if ((size_t)(-delta) > tb->cursor_pos) {
            new_pos = 0;
        } else {
            new_pos = tb->cursor_pos + (size_t)delta;
        }
    } else {
        new_pos = tb->cursor_pos + (size_t)delta;
        if (new_pos > tb->text_length) {
            new_pos = tb->text_length;
        }
    }

    if (extend_selection) {
        if (!tb->has_selection) {
            tb->selection_start = tb->cursor_pos;
            tb->has_selection = true;
        }
        tb->selection_end = new_pos;
    } else {
        tb->has_selection = false;
    }

    tb->cursor_pos = new_pos;
    textbox_ensure_cursor_visible(tb);
    widget_invalidate(&tb->base);
}

/**
 * Select all text
 */
void textbox_select_all(textbox_t *tb) {
    if (!tb || tb->text_length == 0) return;

    tb->selection_start = 0;
    tb->selection_end = tb->text_length;
    tb->cursor_pos = tb->text_length;
    tb->has_selection = true;

    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Selected all text\n");
}

/**
 * Clear selection
 */
void textbox_clear_selection(textbox_t *tb) {
    if (!tb) return;

    tb->has_selection = false;
    widget_invalidate(&tb->base);
}

/**
 * Get selected text
 */
size_t textbox_get_selection(textbox_t *tb, char *buf, size_t max) {
    if (!tb || !buf || max == 0 || !tb->has_selection) {
        if (buf && max > 0) buf[0] = '\0';
        return 0;
    }

    size_t start = tb->selection_start < tb->selection_end ?
                   tb->selection_start : tb->selection_end;
    size_t end = tb->selection_start < tb->selection_end ?
                 tb->selection_end : tb->selection_start;

    size_t sel_len = end - start;
    if (sel_len >= max) {
        sel_len = max - 1;
    }

    memcpy(buf, &tb->text[start], sel_len);
    buf[sel_len] = '\0';

    return sel_len;
}

/**
 * Set placeholder text
 */
void textbox_set_placeholder(textbox_t *tb, const char *text) {
    if (!tb) return;

    if (text) {
        strncpy(tb->placeholder, text, TEXTBOX_MAX_TEXT - 1);
        tb->placeholder[TEXTBOX_MAX_TEXT - 1] = '\0';
    } else {
        tb->placeholder[0] = '\0';
    }

    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Placeholder set to '%s'\n", tb->placeholder);
}

/**
 * Set read-only mode
 */
void textbox_set_read_only(textbox_t *tb, bool read_only) {
    if (!tb) return;

    tb->read_only = read_only;
    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Read-only mode set to %s\n", read_only ? "true" : "false");
}

/**
 * Set password mode
 */
void textbox_set_password_mode(textbox_t *tb, bool password_mode) {
    if (!tb) return;

    tb->password_mode = password_mode;
    widget_invalidate(&tb->base);

    kprintf("[TEXTBOX] Password mode set to %s\n", password_mode ? "true" : "false");
}

/**
 * Set maximum text length
 */
void textbox_set_max_length(textbox_t *tb, size_t max_length) {
    if (!tb) return;

    if (max_length == 0 || max_length >= TEXTBOX_MAX_TEXT) {
        tb->max_length = 0;  /* Unlimited */
    } else {
        tb->max_length = max_length;

        /* Truncate existing text if necessary */
        if (tb->text_length > max_length) {
            tb->text[max_length] = '\0';
            tb->text_length = max_length;
            if (tb->cursor_pos > max_length) {
                tb->cursor_pos = max_length;
            }
            textbox_ensure_cursor_visible(tb);
            widget_invalidate(&tb->base);
        }
    }

    kprintf("[TEXTBOX] Max length set to %zu\n", max_length);
}

/**
 * Set change callback
 */
void textbox_set_on_change(textbox_t *tb, void (*callback)(textbox_t *), void *data) {
    if (!tb) return;

    tb->on_change = callback;
    tb->callback_data = data;
}

/**
 * Set enter callback
 */
void textbox_set_on_enter(textbox_t *tb, void (*callback)(textbox_t *), void *data) {
    if (!tb) return;

    tb->on_enter = callback;
    tb->callback_data = data;
}

/**
 * Update cursor blink state
 */
void textbox_update(textbox_t *tb, uint32_t delta_ms) {
    if (!tb) return;

    /* Only blink cursor if focused */
    if (!(tb->base.flags & WIDGET_FLAG_FOCUSED)) {
        return;
    }

    tb->cursor_blink_time += delta_ms;

    if (tb->cursor_blink_time >= CURSOR_BLINK_INTERVAL) {
        tb->cursor_blink_time = 0;
        tb->cursor_visible = !tb->cursor_visible;
        widget_invalidate(&tb->base);
    }
}

/**
 * Destroy a textbox and free its resources
 */
void textbox_destroy(textbox_t *tb) {
    if (!tb) return;

    kprintf("[TEXTBOX] Destroying textbox\n");

    /* Destroy base widget */
    widget_destroy(&tb->base);

    /* Free the textbox structure */
    kfree(tb);
}
