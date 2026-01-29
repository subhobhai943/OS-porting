/**
 * AAAos GUI - Label Widget Implementation
 *
 * Implements a simple text label widget for displaying static
 * or dynamic text with customizable color and alignment.
 */

#include "label.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/* Font metrics (assumed fixed-width font) */
#define FONT_CHAR_WIDTH     8
#define FONT_CHAR_HEIGHT    16

/* Forward declarations for internal functions */
static void label_draw_impl(widget_t *w, void *buffer);
static void label_update_size(label_t *lbl);

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
 * Create a new label widget (auto-sized)
 */
label_t *label_create(int32_t x, int32_t y, const char *text) {
    label_t *lbl = (label_t *)kmalloc(sizeof(label_t));
    if (!lbl) {
        kprintf("[LABEL] ERROR: Failed to allocate label\n");
        return NULL;
    }

    label_init(lbl, x, y, text);

    kprintf("[LABEL] Created label '%s' at (%d, %d)\n",
            text ? text : "", x, y);

    return lbl;
}

/**
 * Create a new label widget with specific size
 */
label_t *label_create_sized(int32_t x, int32_t y, int32_t width, int32_t height, const char *text) {
    label_t *lbl = (label_t *)kmalloc(sizeof(label_t));
    if (!lbl) {
        kprintf("[LABEL] ERROR: Failed to allocate label\n");
        return NULL;
    }

    label_init_sized(lbl, x, y, width, height, text);

    kprintf("[LABEL] Created sized label '%s' at (%d, %d) size %dx%d\n",
            text ? text : "", x, y, width, height);

    return lbl;
}

/**
 * Initialize an existing label structure (auto-sized)
 */
void label_init(label_t *lbl, int32_t x, int32_t y, const char *text) {
    if (!lbl) {
        kprintf("[LABEL] ERROR: label_init called with NULL label\n");
        return;
    }

    /* Calculate initial size based on text */
    int32_t width = FONT_CHAR_WIDTH;
    int32_t height = FONT_CHAR_HEIGHT;

    if (text) {
        width = (int32_t)(strlen(text) * FONT_CHAR_WIDTH);
        if (width == 0) width = FONT_CHAR_WIDTH;
    }

    /* Initialize with calculated size */
    label_init_sized(lbl, x, y, width, height, text);

    /* Enable auto-sizing */
    lbl->auto_size = true;
}

/**
 * Initialize an existing label structure with specific size
 */
void label_init_sized(label_t *lbl, int32_t x, int32_t y, int32_t width, int32_t height, const char *text) {
    if (!lbl) {
        kprintf("[LABEL] ERROR: label_init_sized called with NULL label\n");
        return;
    }

    /* Initialize base widget */
    widget_init(&lbl->base, x, y, width, height);

    /* Set label-specific properties */
    lbl->color = LABEL_COLOR_TEXT;
    lbl->bg_color = LABEL_COLOR_BACKGROUND;
    lbl->align = LABEL_ALIGN_LEFT;
    lbl->valign = LABEL_VALIGN_TOP;
    lbl->word_wrap = false;
    lbl->auto_size = false;

    /* Set text */
    if (text) {
        strncpy(lbl->text, text, LABEL_MAX_TEXT - 1);
        lbl->text[LABEL_MAX_TEXT - 1] = '\0';
    } else {
        lbl->text[0] = '\0';
    }

    /* Set up virtual functions */
    lbl->base.draw = label_draw_impl;

    /* Labels typically don't accept input focus */
    lbl->base.flags &= ~WIDGET_FLAG_ENABLED;

    kprintf("[LABEL] Initialized label (widget %u) with text '%s'\n",
            lbl->base.id, lbl->text);
}

/**
 * Update label size based on text (for auto-sizing)
 */
static void label_update_size(label_t *lbl) {
    if (!lbl || !lbl->auto_size) return;

    int32_t width, height;
    label_get_text_size(lbl, &width, &height);

    if (width > 0 && height > 0) {
        lbl->base.width = width;
        lbl->base.height = height;
    }
}

/**
 * Internal draw function for labels
 */
static void label_draw_impl(widget_t *w, void *buffer) {
    if (!w || !buffer) return;

    label_t *lbl = (label_t *)w;

    /* Get absolute position */
    int32_t abs_x, abs_y;
    widget_get_absolute_pos(w, &abs_x, &abs_y);

    /* Draw background if not transparent */
    if ((lbl->bg_color & 0xFF000000) != 0) {
        draw_filled_rect(buffer, abs_x, abs_y, w->width, w->height, lbl->bg_color);
    }

    /* Don't draw if no text */
    if (lbl->text[0] == '\0') {
        return;
    }

    /* Calculate text dimensions */
    size_t text_len = strlen(lbl->text);
    int32_t text_width = (int32_t)(text_len * FONT_CHAR_WIDTH);
    int32_t text_height = FONT_CHAR_HEIGHT;

    /* Calculate X position based on alignment */
    int32_t text_x;
    switch (lbl->align) {
        case LABEL_ALIGN_CENTER:
            text_x = abs_x + (w->width - text_width) / 2;
            break;
        case LABEL_ALIGN_RIGHT:
            text_x = abs_x + w->width - text_width;
            break;
        case LABEL_ALIGN_LEFT:
        default:
            text_x = abs_x;
            break;
    }

    /* Calculate Y position based on vertical alignment */
    int32_t text_y;
    switch (lbl->valign) {
        case LABEL_VALIGN_MIDDLE:
            text_y = abs_y + (w->height - text_height) / 2;
            break;
        case LABEL_VALIGN_BOTTOM:
            text_y = abs_y + w->height - text_height;
            break;
        case LABEL_VALIGN_TOP:
        default:
            text_y = abs_y;
            break;
    }

    /* Draw the text */
    draw_text(buffer, text_x, text_y, lbl->text, lbl->color);

    kprintf("[LABEL] Drew label '%s' at (%d, %d) color=0x%08X\n",
            lbl->text, text_x, text_y, lbl->color);
}

/**
 * Set label text
 */
void label_set_text(label_t *lbl, const char *text) {
    if (!lbl) {
        kprintf("[LABEL] ERROR: label_set_text called with NULL label\n");
        return;
    }

    if (text) {
        strncpy(lbl->text, text, LABEL_MAX_TEXT - 1);
        lbl->text[LABEL_MAX_TEXT - 1] = '\0';
    } else {
        lbl->text[0] = '\0';
    }

    /* Update size if auto-sizing */
    label_update_size(lbl);

    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label (widget %u) text set to '%s'\n",
            lbl->base.id, lbl->text);
}

/**
 * Get label text
 */
const char *label_get_text(label_t *lbl) {
    if (!lbl) return NULL;
    return lbl->text;
}

/**
 * Set label text color
 */
void label_set_color(label_t *lbl, uint32_t color) {
    if (!lbl) {
        kprintf("[LABEL] ERROR: label_set_color called with NULL label\n");
        return;
    }

    lbl->color = color;
    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label '%s' color set to 0x%08X\n", lbl->text, color);
}

/**
 * Get label text color
 */
uint32_t label_get_color(label_t *lbl) {
    if (!lbl) return 0;
    return lbl->color;
}

/**
 * Set label background color
 */
void label_set_background(label_t *lbl, uint32_t color) {
    if (!lbl) return;

    lbl->bg_color = color;
    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label '%s' background set to 0x%08X\n", lbl->text, color);
}

/**
 * Set label text alignment
 */
void label_set_align(label_t *lbl, label_align_t align) {
    if (!lbl) return;

    lbl->align = align;
    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label '%s' alignment set to %d\n", lbl->text, align);
}

/**
 * Set label vertical alignment
 */
void label_set_valign(label_t *lbl, label_valign_t valign) {
    if (!lbl) return;

    lbl->valign = valign;
    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label '%s' vertical alignment set to %d\n", lbl->text, valign);
}

/**
 * Enable or disable word wrapping
 */
void label_set_word_wrap(label_t *lbl, bool wrap) {
    if (!lbl) return;

    lbl->word_wrap = wrap;
    widget_invalidate(&lbl->base);

    kprintf("[LABEL] Label '%s' word wrap set to %s\n",
            lbl->text, wrap ? "true" : "false");
}

/**
 * Enable or disable auto-sizing
 */
void label_set_auto_size(label_t *lbl, bool auto_size) {
    if (!lbl) return;

    lbl->auto_size = auto_size;

    if (auto_size) {
        label_update_size(lbl);
    }

    kprintf("[LABEL] Label '%s' auto-size set to %s\n",
            lbl->text, auto_size ? "true" : "false");
}

/**
 * Calculate the size needed to display the text
 */
void label_get_text_size(label_t *lbl, int32_t *width, int32_t *height) {
    if (!lbl || !width || !height) return;

    if (lbl->text[0] == '\0') {
        *width = 0;
        *height = 0;
        return;
    }

    size_t text_len = strlen(lbl->text);

    if (lbl->word_wrap && lbl->base.width > 0) {
        /* Calculate wrapped text dimensions */
        int32_t chars_per_line = lbl->base.width / FONT_CHAR_WIDTH;
        if (chars_per_line <= 0) chars_per_line = 1;

        int32_t lines = ((int32_t)text_len + chars_per_line - 1) / chars_per_line;

        *width = (int32_t)(MIN((size_t)chars_per_line, text_len) * FONT_CHAR_WIDTH);
        *height = lines * FONT_CHAR_HEIGHT;
    } else {
        /* Single line */
        *width = (int32_t)(text_len * FONT_CHAR_WIDTH);
        *height = FONT_CHAR_HEIGHT;
    }
}

/**
 * Destroy a label and free its resources
 */
void label_destroy(label_t *lbl) {
    if (!lbl) return;

    kprintf("[LABEL] Destroying label '%s'\n", lbl->text);

    /* Destroy base widget */
    widget_destroy(&lbl->base);

    /* Free the label structure */
    kfree(lbl);
}
