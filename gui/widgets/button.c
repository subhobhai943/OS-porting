/**
 * AAAos GUI - Button Widget Implementation
 *
 * Implements a clickable button widget with text label and
 * customizable click callback.
 */

#include "button.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/* Forward declarations for internal functions */
static void button_draw_impl(widget_t *w, void *buffer);
static void button_mouse_handler(widget_t *w, event_t *event);
static void button_key_handler(widget_t *w, event_t *event);

/**
 * Draw a filled rectangle (simple software renderer)
 * This is a basic implementation - in practice, this would use the graphics driver
 */
static void draw_filled_rect(void *buffer, int32_t x, int32_t y,
                             int32_t width, int32_t height, uint32_t color) {
    /* Buffer assumed to be 32-bit ARGB framebuffer */
    /* In a real implementation, we'd need screen width/height/pitch */
    /* For now, this is a placeholder that logs the operation */
    UNUSED(buffer);
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    UNUSED(color);
}

/**
 * Draw a rectangle border
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
 * Draw text at position
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
 * Create a new button widget
 */
button_t *button_create(int32_t x, int32_t y, int32_t width, int32_t height, const char *text) {
    button_t *btn = (button_t *)kmalloc(sizeof(button_t));
    if (!btn) {
        kprintf("[BUTTON] ERROR: Failed to allocate button\n");
        return NULL;
    }

    button_init(btn, x, y, width, height, text);

    kprintf("[BUTTON] Created button '%s' at (%d, %d) size %dx%d\n",
            text ? text : "", x, y, width, height);

    return btn;
}

/**
 * Initialize an existing button structure
 */
void button_init(button_t *btn, int32_t x, int32_t y, int32_t width, int32_t height, const char *text) {
    if (!btn) {
        kprintf("[BUTTON] ERROR: button_init called with NULL button\n");
        return;
    }

    /* Initialize base widget */
    widget_init(&btn->base, x, y, width, height);

    /* Set button-specific properties */
    btn->state = BUTTON_STATE_NORMAL;
    btn->on_click_callback = NULL;
    btn->callback_data = NULL;

    /* Set default colors */
    btn->color_normal = BUTTON_COLOR_NORMAL;
    btn->color_hover = BUTTON_COLOR_HOVER;
    btn->color_pressed = BUTTON_COLOR_PRESSED;
    btn->color_text = BUTTON_COLOR_TEXT;
    btn->color_border = BUTTON_COLOR_BORDER;

    /* Set text */
    if (text) {
        strncpy(btn->text, text, BUTTON_MAX_TEXT - 1);
        btn->text[BUTTON_MAX_TEXT - 1] = '\0';
    } else {
        btn->text[0] = '\0';
    }

    /* Set up virtual functions and event handlers */
    btn->base.draw = button_draw_impl;
    btn->base.on_mouse = button_mouse_handler;
    btn->base.on_key = button_key_handler;

    kprintf("[BUTTON] Initialized button (widget %u) with text '%s'\n",
            btn->base.id, btn->text);
}

/**
 * Internal draw function for buttons
 */
static void button_draw_impl(widget_t *w, void *buffer) {
    if (!w || !buffer) return;

    button_t *btn = (button_t *)w;

    /* Get absolute position */
    int32_t abs_x, abs_y;
    widget_get_absolute_pos(w, &abs_x, &abs_y);

    /* Determine background color based on state */
    uint32_t bg_color;
    uint32_t text_color = btn->color_text;

    if (!(w->flags & WIDGET_FLAG_ENABLED)) {
        bg_color = BUTTON_COLOR_DISABLED;
        text_color = BUTTON_COLOR_TEXT_DISABLED;
        btn->state = BUTTON_STATE_DISABLED;
    } else {
        switch (btn->state) {
            case BUTTON_STATE_HOVER:
                bg_color = btn->color_hover;
                break;
            case BUTTON_STATE_PRESSED:
                bg_color = btn->color_pressed;
                break;
            case BUTTON_STATE_NORMAL:
            default:
                bg_color = btn->color_normal;
                break;
        }
    }

    /* Draw button background */
    draw_filled_rect(buffer, abs_x, abs_y, w->width, w->height, bg_color);

    /* Draw border (different color if focused) */
    uint32_t border_color = (w->flags & WIDGET_FLAG_FOCUSED) ?
                            BUTTON_COLOR_BORDER_FOCUS : btn->color_border;
    draw_rect_border(buffer, abs_x, abs_y, w->width, w->height, border_color);

    /* Draw text centered in button */
    if (btn->text[0] != '\0') {
        /* Calculate text position (approximate centering) */
        size_t text_len = strlen(btn->text);
        int32_t text_width = (int32_t)(text_len * 8);  /* Assume 8px wide chars */
        int32_t text_height = 16;  /* Assume 16px tall chars */
        int32_t text_x = abs_x + (w->width - text_width) / 2;
        int32_t text_y = abs_y + (w->height - text_height) / 2;

        draw_text(buffer, text_x, text_y, btn->text, text_color);
    }

    kprintf("[BUTTON] Drew button '%s' state=%d at (%d, %d)\n",
            btn->text, btn->state, abs_x, abs_y);
}

/**
 * Mouse event handler for buttons
 */
static void button_mouse_handler(widget_t *w, event_t *event) {
    if (!w || !event) return;

    button_t *btn = (button_t *)w;

    /* Don't handle if disabled */
    if (!(w->flags & WIDGET_FLAG_ENABLED)) {
        return;
    }

    switch (event->type) {
        case EVENT_MOUSE_ENTER:
            btn->state = BUTTON_STATE_HOVER;
            widget_invalidate(w);
            event->handled = true;
            kprintf("[BUTTON] Mouse entered button '%s'\n", btn->text);
            break;

        case EVENT_MOUSE_LEAVE:
            btn->state = BUTTON_STATE_NORMAL;
            widget_invalidate(w);
            event->handled = true;
            kprintf("[BUTTON] Mouse left button '%s'\n", btn->text);
            break;

        case EVENT_MOUSE_DOWN:
            if (event->mouse.buttons & MOUSE_BUTTON_LEFT) {
                btn->state = BUTTON_STATE_PRESSED;
                widget_set_focus(w);
                widget_invalidate(w);
                event->handled = true;
                kprintf("[BUTTON] Button '%s' pressed\n", btn->text);
            }
            break;

        case EVENT_MOUSE_UP:
            if (btn->state == BUTTON_STATE_PRESSED) {
                btn->state = BUTTON_STATE_HOVER;
                widget_invalidate(w);

                /* Check if mouse is still over button (click completed) */
                if (widget_contains_point(w, event->mouse.global_x, event->mouse.global_y)) {
                    /* Trigger click callback */
                    if (btn->on_click_callback) {
                        kprintf("[BUTTON] Executing click callback for '%s'\n", btn->text);
                        btn->on_click_callback(btn->callback_data);
                    }

                    /* Also trigger the base on_click handler */
                    if (w->on_click) {
                        event_t click_event;
                        memset(&click_event, 0, sizeof(event_t));
                        click_event.type = EVENT_MOUSE_CLICK;
                        click_event.target = w;
                        click_event.mouse = event->mouse;
                        w->on_click(w, &click_event);
                    }
                }
                event->handled = true;
                kprintf("[BUTTON] Button '%s' released\n", btn->text);
            }
            break;

        default:
            break;
    }
}

/**
 * Key event handler for buttons
 * Handles Space and Enter to activate button
 */
static void button_key_handler(widget_t *w, event_t *event) {
    if (!w || !event) return;

    button_t *btn = (button_t *)w;

    /* Don't handle if disabled or not focused */
    if (!(w->flags & WIDGET_FLAG_ENABLED) || !(w->flags & WIDGET_FLAG_FOCUSED)) {
        return;
    }

    if (event->type == EVENT_KEY_DOWN) {
        /* Space or Enter activates the button */
        if (event->key.ascii == ' ' || event->key.ascii == '\n' || event->key.ascii == '\r') {
            btn->state = BUTTON_STATE_PRESSED;
            widget_invalidate(w);
            event->handled = true;
            kprintf("[BUTTON] Button '%s' activated by key\n", btn->text);
        }
    } else if (event->type == EVENT_KEY_UP) {
        if (event->key.ascii == ' ' || event->key.ascii == '\n' || event->key.ascii == '\r') {
            if (btn->state == BUTTON_STATE_PRESSED) {
                btn->state = BUTTON_STATE_NORMAL;
                widget_invalidate(w);

                /* Trigger click callback */
                if (btn->on_click_callback) {
                    kprintf("[BUTTON] Executing click callback for '%s' (keyboard)\n", btn->text);
                    btn->on_click_callback(btn->callback_data);
                }
                event->handled = true;
            }
        }
    }
}

/**
 * Set button text
 */
void button_set_text(button_t *btn, const char *text) {
    if (!btn) {
        kprintf("[BUTTON] ERROR: button_set_text called with NULL button\n");
        return;
    }

    if (text) {
        strncpy(btn->text, text, BUTTON_MAX_TEXT - 1);
        btn->text[BUTTON_MAX_TEXT - 1] = '\0';
    } else {
        btn->text[0] = '\0';
    }

    widget_invalidate(&btn->base);

    kprintf("[BUTTON] Button (widget %u) text set to '%s'\n",
            btn->base.id, btn->text);
}

/**
 * Get button text
 */
const char *button_get_text(button_t *btn) {
    if (!btn) return NULL;
    return btn->text;
}

/**
 * Set button click callback
 */
void button_set_on_click(button_t *btn, button_click_callback_t callback, void *data) {
    if (!btn) {
        kprintf("[BUTTON] ERROR: button_set_on_click called with NULL button\n");
        return;
    }

    btn->on_click_callback = callback;
    btn->callback_data = data;

    kprintf("[BUTTON] Button '%s' click callback set\n", btn->text);
}

/**
 * Set button colors
 */
void button_set_colors(button_t *btn, uint32_t normal, uint32_t hover, uint32_t pressed) {
    if (!btn) return;

    btn->color_normal = normal;
    btn->color_hover = hover;
    btn->color_pressed = pressed;
    widget_invalidate(&btn->base);

    kprintf("[BUTTON] Button '%s' colors updated\n", btn->text);
}

/**
 * Set button text color
 */
void button_set_text_color(button_t *btn, uint32_t color) {
    if (!btn) return;

    btn->color_text = color;
    widget_invalidate(&btn->base);
}

/**
 * Set button border color
 */
void button_set_border_color(button_t *btn, uint32_t color) {
    if (!btn) return;

    btn->color_border = color;
    widget_invalidate(&btn->base);
}

/**
 * Destroy a button and free its resources
 */
void button_destroy(button_t *btn) {
    if (!btn) return;

    kprintf("[BUTTON] Destroying button '%s'\n", btn->text);

    /* Destroy base widget */
    widget_destroy(&btn->base);

    /* Free the button structure */
    kfree(btn);
}
