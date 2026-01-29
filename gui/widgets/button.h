/**
 * AAAos GUI - Button Widget
 *
 * Provides a clickable button widget with text label and
 * customizable click callback.
 */

#ifndef _AAAOS_GUI_BUTTON_H
#define _AAAOS_GUI_BUTTON_H

#include "widget.h"

/* Maximum button text length */
#define BUTTON_MAX_TEXT     64

/* Button states */
typedef enum {
    BUTTON_STATE_NORMAL = 0,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_DISABLED
} button_state_t;

/* Button colors (32-bit ARGB) */
#define BUTTON_COLOR_NORMAL         0xFF4A4A4A  /* Dark gray */
#define BUTTON_COLOR_HOVER          0xFF5A5A5A  /* Lighter gray */
#define BUTTON_COLOR_PRESSED        0xFF3A3A3A  /* Darker gray */
#define BUTTON_COLOR_DISABLED       0xFF2A2A2A  /* Very dark gray */
#define BUTTON_COLOR_BORDER         0xFF6A6A6A  /* Border color */
#define BUTTON_COLOR_BORDER_FOCUS   0xFF007ACC  /* Blue border when focused */
#define BUTTON_COLOR_TEXT           0xFFFFFFFF  /* White text */
#define BUTTON_COLOR_TEXT_DISABLED  0xFF888888  /* Gray text when disabled */

/* Click callback type */
typedef void (*button_click_callback_t)(void *user_data);

/**
 * Button widget structure
 * Extends the base widget with button-specific properties
 */
typedef struct button {
    widget_t base;                      /* Base widget (must be first) */

    char text[BUTTON_MAX_TEXT];         /* Button text */
    button_state_t state;               /* Current button state */

    /* Callback */
    button_click_callback_t on_click_callback;  /* Click callback */
    void *callback_data;                /* Data passed to callback */

    /* Colors */
    uint32_t color_normal;              /* Normal background color */
    uint32_t color_hover;               /* Hover background color */
    uint32_t color_pressed;             /* Pressed background color */
    uint32_t color_text;                /* Text color */
    uint32_t color_border;              /* Border color */
} button_t;

/**
 * Create a new button widget
 * @param x X position
 * @param y Y position
 * @param width Button width
 * @param height Button height
 * @param text Button text
 * @return Pointer to new button or NULL on failure
 */
button_t *button_create(int32_t x, int32_t y, int32_t width, int32_t height, const char *text);

/**
 * Initialize an existing button structure
 * @param btn Button to initialize
 * @param x X position
 * @param y Y position
 * @param width Button width
 * @param height Button height
 * @param text Button text
 */
void button_init(button_t *btn, int32_t x, int32_t y, int32_t width, int32_t height, const char *text);

/**
 * Set button text
 * @param btn Button
 * @param text New text
 */
void button_set_text(button_t *btn, const char *text);

/**
 * Get button text
 * @param btn Button
 * @return Button text
 */
const char *button_get_text(button_t *btn);

/**
 * Set button click callback
 * @param btn Button
 * @param callback Callback function
 * @param data User data passed to callback
 */
void button_set_on_click(button_t *btn, button_click_callback_t callback, void *data);

/**
 * Set button colors
 * @param btn Button
 * @param normal Normal background color
 * @param hover Hover background color
 * @param pressed Pressed background color
 */
void button_set_colors(button_t *btn, uint32_t normal, uint32_t hover, uint32_t pressed);

/**
 * Set button text color
 * @param btn Button
 * @param color Text color
 */
void button_set_text_color(button_t *btn, uint32_t color);

/**
 * Set button border color
 * @param btn Button
 * @param color Border color
 */
void button_set_border_color(button_t *btn, uint32_t color);

/**
 * Destroy a button and free its resources
 * @param btn Button to destroy
 */
void button_destroy(button_t *btn);

#endif /* _AAAOS_GUI_BUTTON_H */
