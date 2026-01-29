/**
 * AAAos GUI - Base Widget System
 *
 * Provides the foundational widget structure and common functions
 * for the AAAos graphical user interface toolkit.
 */

#ifndef _AAAOS_GUI_WIDGET_H
#define _AAAOS_GUI_WIDGET_H

#include "../../kernel/include/types.h"

/* Maximum number of children per widget */
#define WIDGET_MAX_CHILDREN     32

/* Widget flags */
#define WIDGET_FLAG_VISIBLE     0x01    /* Widget is visible */
#define WIDGET_FLAG_ENABLED     0x02    /* Widget is enabled (accepts input) */
#define WIDGET_FLAG_FOCUSED     0x04    /* Widget has keyboard focus */
#define WIDGET_FLAG_DIRTY       0x08    /* Widget needs redraw */
#define WIDGET_FLAG_CONTAINER   0x10    /* Widget can contain children */

/* Default widget flags */
#define WIDGET_FLAGS_DEFAULT    (WIDGET_FLAG_VISIBLE | WIDGET_FLAG_ENABLED)

/* Event types */
typedef enum {
    EVENT_NONE = 0,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_CLICK,
    EVENT_MOUSE_ENTER,
    EVENT_MOUSE_LEAVE,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_KEY_CHAR,
    EVENT_FOCUS_GAINED,
    EVENT_FOCUS_LOST,
    EVENT_PAINT,
    EVENT_RESIZE,
    EVENT_CLOSE,
    EVENT_MAX
} event_type_t;

/* Mouse button codes */
#define MOUSE_BUTTON_LEFT       0x01
#define MOUSE_BUTTON_RIGHT      0x02
#define MOUSE_BUTTON_MIDDLE     0x04

/* Forward declarations */
typedef struct widget widget_t;
typedef struct event event_t;

/**
 * Event structure
 * Contains all information about a GUI event
 */
struct event {
    event_type_t type;          /* Event type */
    widget_t    *target;        /* Target widget */

    union {
        /* Mouse event data */
        struct {
            int32_t x;          /* Mouse X position (relative to widget) */
            int32_t y;          /* Mouse Y position (relative to widget) */
            int32_t global_x;   /* Mouse X position (absolute) */
            int32_t global_y;   /* Mouse Y position (absolute) */
            uint8_t buttons;    /* Mouse button state */
        } mouse;

        /* Keyboard event data */
        struct {
            uint16_t keycode;   /* Virtual keycode */
            uint8_t  scancode;  /* Raw scancode */
            uint16_t modifiers; /* Modifier keys */
            char     ascii;     /* ASCII character (if printable) */
        } key;

        /* Paint event data */
        struct {
            void    *buffer;    /* Framebuffer to draw to */
            int32_t  clip_x;    /* Clip region X */
            int32_t  clip_y;    /* Clip region Y */
            int32_t  clip_w;    /* Clip region width */
            int32_t  clip_h;    /* Clip region height */
        } paint;

        /* Resize event data */
        struct {
            int32_t old_w;      /* Previous width */
            int32_t old_h;      /* Previous height */
            int32_t new_w;      /* New width */
            int32_t new_h;      /* New height */
        } resize;
    };

    bool handled;               /* Set to true if event was handled */
};

/* Event handler callback type */
typedef void (*event_handler_t)(widget_t *widget, event_t *event);

/* Draw function pointer type */
typedef void (*widget_draw_fn)(widget_t *widget, void *buffer);

/**
 * Base widget structure
 * All specific widgets should embed this as their first member
 */
struct widget {
    /* Geometry */
    int32_t x;                  /* X position relative to parent */
    int32_t y;                  /* Y position relative to parent */
    int32_t width;              /* Widget width */
    int32_t height;             /* Widget height */

    /* Widget tree */
    widget_t *parent;           /* Parent widget (NULL if root) */
    widget_t *children[WIDGET_MAX_CHILDREN];  /* Child widgets */
    uint32_t child_count;       /* Number of children */

    /* State */
    uint32_t flags;             /* Widget flags */
    uint32_t id;                /* Unique widget ID */

    /* Event handlers */
    event_handler_t on_click;   /* Click event handler */
    event_handler_t on_key;     /* Key event handler */
    event_handler_t on_paint;   /* Paint event handler */
    event_handler_t on_focus;   /* Focus change handler */
    event_handler_t on_mouse;   /* Mouse event handler */

    /* User data */
    void *user_data;            /* Application-specific data */

    /* Virtual functions */
    widget_draw_fn draw;        /* Custom draw function */
};

/**
 * Initialize a widget with default values
 * @param w Widget to initialize
 * @param x X position
 * @param y Y position
 * @param width Widget width
 * @param height Widget height
 */
void widget_init(widget_t *w, int32_t x, int32_t y, int32_t width, int32_t height);

/**
 * Add a child widget to a parent
 * @param parent Parent widget
 * @param child Child widget to add
 * @return true on success, false if children array is full
 */
bool widget_add_child(widget_t *parent, widget_t *child);

/**
 * Remove a child widget from a parent
 * @param parent Parent widget
 * @param child Child widget to remove
 * @return true on success, false if child not found
 */
bool widget_remove_child(widget_t *parent, widget_t *child);

/**
 * Draw a widget and all its children
 * @param w Widget to draw
 * @param buffer Framebuffer to draw to
 */
void widget_draw(widget_t *w, void *buffer);

/**
 * Handle an event for a widget
 * @param w Widget to handle event for
 * @param event Event to handle
 * @return true if event was handled
 */
bool widget_handle_event(widget_t *w, event_t *event);

/**
 * Set keyboard focus to a widget
 * @param w Widget to focus (NULL to clear focus)
 */
void widget_set_focus(widget_t *w);

/**
 * Get the currently focused widget
 * @return Focused widget or NULL
 */
widget_t *widget_get_focus(void);

/**
 * Set widget visibility
 * @param w Widget
 * @param visible true to show, false to hide
 */
void widget_set_visible(widget_t *w, bool visible);

/**
 * Check if widget is visible
 * @param w Widget
 * @return true if visible
 */
bool widget_is_visible(widget_t *w);

/**
 * Set widget enabled state
 * @param w Widget
 * @param enabled true to enable, false to disable
 */
void widget_set_enabled(widget_t *w, bool enabled);

/**
 * Check if widget is enabled
 * @param w Widget
 * @return true if enabled
 */
bool widget_is_enabled(widget_t *w);

/**
 * Mark widget as needing redraw
 * @param w Widget to invalidate
 */
void widget_invalidate(widget_t *w);

/**
 * Get widget absolute screen position
 * @param w Widget
 * @param abs_x Pointer to store absolute X
 * @param abs_y Pointer to store absolute Y
 */
void widget_get_absolute_pos(widget_t *w, int32_t *abs_x, int32_t *abs_y);

/**
 * Check if a point is inside a widget
 * @param w Widget
 * @param x X coordinate (absolute)
 * @param y Y coordinate (absolute)
 * @return true if point is inside widget
 */
bool widget_contains_point(widget_t *w, int32_t x, int32_t y);

/**
 * Find the deepest widget at a given point
 * @param root Root widget to start search
 * @param x X coordinate
 * @param y Y coordinate
 * @return Widget at point or NULL
 */
widget_t *widget_find_at(widget_t *root, int32_t x, int32_t y);

/**
 * Set widget bounds (position and size)
 * @param w Widget
 * @param x New X position
 * @param y New Y position
 * @param width New width
 * @param height New height
 */
void widget_set_bounds(widget_t *w, int32_t x, int32_t y, int32_t width, int32_t height);

/**
 * Destroy a widget and free its resources
 * Note: Does not free the widget structure itself
 * @param w Widget to destroy
 */
void widget_destroy(widget_t *w);

#endif /* _AAAOS_GUI_WIDGET_H */
