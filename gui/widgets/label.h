/**
 * AAAos GUI - Label Widget
 *
 * Provides a simple text label widget for displaying static
 * or dynamic text with customizable color and alignment.
 */

#ifndef _AAAOS_GUI_LABEL_H
#define _AAAOS_GUI_LABEL_H

#include "widget.h"

/* Maximum label text length */
#define LABEL_MAX_TEXT      256

/* Text alignment options */
typedef enum {
    LABEL_ALIGN_LEFT = 0,
    LABEL_ALIGN_CENTER,
    LABEL_ALIGN_RIGHT
} label_align_t;

/* Vertical alignment options */
typedef enum {
    LABEL_VALIGN_TOP = 0,
    LABEL_VALIGN_MIDDLE,
    LABEL_VALIGN_BOTTOM
} label_valign_t;

/* Default label colors (32-bit ARGB) */
#define LABEL_COLOR_TEXT            0xFFFFFFFF  /* White text */
#define LABEL_COLOR_BACKGROUND      0x00000000  /* Transparent background */

/**
 * Label widget structure
 * Extends the base widget with label-specific properties
 */
typedef struct label {
    widget_t base;                      /* Base widget (must be first) */

    char text[LABEL_MAX_TEXT];          /* Label text */
    uint32_t color;                     /* Text color */
    uint32_t bg_color;                  /* Background color */
    label_align_t align;                /* Horizontal alignment */
    label_valign_t valign;              /* Vertical alignment */
    bool word_wrap;                     /* Enable word wrapping */
    bool auto_size;                     /* Automatically resize to fit text */
} label_t;

/**
 * Create a new label widget
 * @param x X position
 * @param y Y position
 * @param text Label text
 * @return Pointer to new label or NULL on failure
 */
label_t *label_create(int32_t x, int32_t y, const char *text);

/**
 * Create a new label widget with specific size
 * @param x X position
 * @param y Y position
 * @param width Label width
 * @param height Label height
 * @param text Label text
 * @return Pointer to new label or NULL on failure
 */
label_t *label_create_sized(int32_t x, int32_t y, int32_t width, int32_t height, const char *text);

/**
 * Initialize an existing label structure
 * @param lbl Label to initialize
 * @param x X position
 * @param y Y position
 * @param text Label text
 */
void label_init(label_t *lbl, int32_t x, int32_t y, const char *text);

/**
 * Initialize an existing label structure with specific size
 * @param lbl Label to initialize
 * @param x X position
 * @param y Y position
 * @param width Label width
 * @param height Label height
 * @param text Label text
 */
void label_init_sized(label_t *lbl, int32_t x, int32_t y, int32_t width, int32_t height, const char *text);

/**
 * Set label text
 * @param lbl Label
 * @param text New text
 */
void label_set_text(label_t *lbl, const char *text);

/**
 * Get label text
 * @param lbl Label
 * @return Label text
 */
const char *label_get_text(label_t *lbl);

/**
 * Set label text color
 * @param lbl Label
 * @param color Text color (32-bit ARGB)
 */
void label_set_color(label_t *lbl, uint32_t color);

/**
 * Get label text color
 * @param lbl Label
 * @return Text color
 */
uint32_t label_get_color(label_t *lbl);

/**
 * Set label background color
 * @param lbl Label
 * @param color Background color (32-bit ARGB, use 0x00000000 for transparent)
 */
void label_set_background(label_t *lbl, uint32_t color);

/**
 * Set label text alignment
 * @param lbl Label
 * @param align Horizontal alignment
 */
void label_set_align(label_t *lbl, label_align_t align);

/**
 * Set label vertical alignment
 * @param lbl Label
 * @param valign Vertical alignment
 */
void label_set_valign(label_t *lbl, label_valign_t valign);

/**
 * Enable or disable word wrapping
 * @param lbl Label
 * @param wrap true to enable word wrap
 */
void label_set_word_wrap(label_t *lbl, bool wrap);

/**
 * Enable or disable auto-sizing
 * @param lbl Label
 * @param auto_size true to automatically resize to fit text
 */
void label_set_auto_size(label_t *lbl, bool auto_size);

/**
 * Calculate the size needed to display the text
 * @param lbl Label
 * @param width Pointer to store required width
 * @param height Pointer to store required height
 */
void label_get_text_size(label_t *lbl, int32_t *width, int32_t *height);

/**
 * Destroy a label and free its resources
 * @param lbl Label to destroy
 */
void label_destroy(label_t *lbl);

#endif /* _AAAOS_GUI_LABEL_H */
