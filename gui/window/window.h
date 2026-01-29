/**
 * AAAos GUI - Window Manager
 *
 * Provides window creation, management, decorations, and input routing.
 * Works with the compositor for rendering windows to the framebuffer.
 */

#ifndef _AAAOS_WINDOW_H
#define _AAAOS_WINDOW_H

#include "../../kernel/include/types.h"

/* Window manager limits */
#define WINDOW_MAX_COUNT            64
#define WINDOW_MAX_TITLE_LEN        128
#define WINDOW_MAX_CHILDREN         16

/* Window decoration dimensions */
#define WINDOW_TITLEBAR_HEIGHT      24
#define WINDOW_BORDER_WIDTH         2
#define WINDOW_BUTTON_SIZE          16
#define WINDOW_BUTTON_PADDING       4
#define WINDOW_MIN_WIDTH            100
#define WINDOW_MIN_HEIGHT           50

/* Window decoration colors */
#define WINDOW_COLOR_TITLEBAR_ACTIVE    0xFF3366FF  /* Blue for active */
#define WINDOW_COLOR_TITLEBAR_INACTIVE  0xFF808080  /* Gray for inactive */
#define WINDOW_COLOR_TITLE_TEXT         0xFFFFFFFF  /* White text */
#define WINDOW_COLOR_BORDER_ACTIVE      0xFF5588FF  /* Blue border */
#define WINDOW_COLOR_BORDER_INACTIVE    0xFF404040  /* Dark gray border */
#define WINDOW_COLOR_CONTENT_BG         0xFFFFFFFF  /* White background */
#define WINDOW_COLOR_BUTTON_CLOSE       0xFFFF4444  /* Red close button */
#define WINDOW_COLOR_BUTTON_MAXIMIZE    0xFF44FF44  /* Green maximize */
#define WINDOW_COLOR_BUTTON_MINIMIZE    0xFFFFFF44  /* Yellow minimize */
#define WINDOW_COLOR_BUTTON_HOVER       0xFFCCCCCC  /* Hover highlight */

/* Mouse button definitions */
#define MOUSE_BUTTON_LEFT           0x01
#define MOUSE_BUTTON_RIGHT          0x02
#define MOUSE_BUTTON_MIDDLE         0x04

/**
 * Window state enumeration
 */
typedef enum window_state {
    WINDOW_STATE_NORMAL = 0,        /* Normal window state */
    WINDOW_STATE_MINIMIZED,         /* Window is minimized to taskbar */
    WINDOW_STATE_MAXIMIZED,         /* Window fills the screen */
    WINDOW_STATE_HIDDEN             /* Window is hidden from view */
} window_state_t;

/**
 * Window flags for properties and behavior
 */
typedef enum window_flags {
    WINDOW_FLAG_NONE        = 0,
    WINDOW_HAS_TITLEBAR     = (1 << 0),     /* Window has title bar */
    WINDOW_HAS_BORDER       = (1 << 1),     /* Window has border */
    WINDOW_RESIZABLE        = (1 << 2),     /* Window can be resized */
    WINDOW_MOVABLE          = (1 << 3),     /* Window can be moved */
    WINDOW_HAS_CLOSE_BTN    = (1 << 4),     /* Window has close button */
    WINDOW_HAS_MINIMIZE_BTN = (1 << 5),     /* Window has minimize button */
    WINDOW_HAS_MAXIMIZE_BTN = (1 << 6),     /* Window has maximize button */
    WINDOW_TRANSPARENT      = (1 << 7),     /* Window supports transparency */
    WINDOW_TOPMOST          = (1 << 8),     /* Window stays on top */
    WINDOW_NO_FOCUS         = (1 << 9),     /* Window cannot receive focus */
    WINDOW_DIRTY            = (1 << 10),    /* Window needs redraw */

    /* Common flag combinations */
    WINDOW_FLAGS_DEFAULT    = WINDOW_HAS_TITLEBAR | WINDOW_HAS_BORDER |
                              WINDOW_RESIZABLE | WINDOW_MOVABLE |
                              WINDOW_HAS_CLOSE_BTN | WINDOW_HAS_MINIMIZE_BTN |
                              WINDOW_HAS_MAXIMIZE_BTN,
    WINDOW_FLAGS_DIALOG     = WINDOW_HAS_TITLEBAR | WINDOW_HAS_BORDER |
                              WINDOW_MOVABLE | WINDOW_HAS_CLOSE_BTN,
    WINDOW_FLAGS_POPUP      = WINDOW_HAS_BORDER,
    WINDOW_FLAGS_BORDERLESS = WINDOW_FLAG_NONE
} window_flags_t;

/**
 * Hit test regions for window decoration interaction
 */
typedef enum window_hit_region {
    WINDOW_HIT_NONE = 0,            /* Outside window */
    WINDOW_HIT_CLIENT,              /* Client/content area */
    WINDOW_HIT_TITLEBAR,            /* Title bar (for dragging) */
    WINDOW_HIT_BORDER_LEFT,         /* Left border (resize) */
    WINDOW_HIT_BORDER_RIGHT,        /* Right border (resize) */
    WINDOW_HIT_BORDER_TOP,          /* Top border (resize) */
    WINDOW_HIT_BORDER_BOTTOM,       /* Bottom border (resize) */
    WINDOW_HIT_CORNER_TL,           /* Top-left corner (resize) */
    WINDOW_HIT_CORNER_TR,           /* Top-right corner (resize) */
    WINDOW_HIT_CORNER_BL,           /* Bottom-left corner (resize) */
    WINDOW_HIT_CORNER_BR,           /* Bottom-right corner (resize) */
    WINDOW_HIT_BUTTON_CLOSE,        /* Close button */
    WINDOW_HIT_BUTTON_MAXIMIZE,     /* Maximize button */
    WINDOW_HIT_BUTTON_MINIMIZE      /* Minimize button */
} window_hit_region_t;

/**
 * Window event types
 */
typedef enum window_event_type {
    WINDOW_EVENT_NONE = 0,
    WINDOW_EVENT_CREATE,            /* Window was created */
    WINDOW_EVENT_DESTROY,           /* Window is being destroyed */
    WINDOW_EVENT_SHOW,              /* Window was shown */
    WINDOW_EVENT_HIDE,              /* Window was hidden */
    WINDOW_EVENT_MOVE,              /* Window was moved */
    WINDOW_EVENT_RESIZE,            /* Window was resized */
    WINDOW_EVENT_FOCUS,             /* Window gained focus */
    WINDOW_EVENT_BLUR,              /* Window lost focus */
    WINDOW_EVENT_MINIMIZE,          /* Window was minimized */
    WINDOW_EVENT_MAXIMIZE,          /* Window was maximized */
    WINDOW_EVENT_RESTORE,           /* Window was restored */
    WINDOW_EVENT_CLOSE,             /* Window close requested */
    WINDOW_EVENT_MOUSE_DOWN,        /* Mouse button pressed */
    WINDOW_EVENT_MOUSE_UP,          /* Mouse button released */
    WINDOW_EVENT_MOUSE_MOVE,        /* Mouse moved */
    WINDOW_EVENT_MOUSE_ENTER,       /* Mouse entered window */
    WINDOW_EVENT_MOUSE_LEAVE,       /* Mouse left window */
    WINDOW_EVENT_KEY_DOWN,          /* Key pressed */
    WINDOW_EVENT_KEY_UP,            /* Key released */
    WINDOW_EVENT_PAINT              /* Window needs painting */
} window_event_type_t;

/* Forward declaration */
struct window;

/**
 * Window event structure
 */
typedef struct window_event {
    window_event_type_t type;       /* Event type */
    struct window *window;          /* Window that received event */
    union {
        struct {
            int32_t x;              /* Mouse X position (relative to window) */
            int32_t y;              /* Mouse Y position (relative to window) */
            uint8_t buttons;        /* Mouse button state */
        } mouse;
        struct {
            uint8_t keycode;        /* Keyboard scan code */
            uint8_t modifiers;      /* Shift/Ctrl/Alt state */
            char character;         /* ASCII character (if applicable) */
        } key;
        struct {
            int32_t old_x;          /* Previous X position */
            int32_t old_y;          /* Previous Y position */
            int32_t new_x;          /* New X position */
            int32_t new_y;          /* New Y position */
        } move;
        struct {
            int32_t old_width;      /* Previous width */
            int32_t old_height;     /* Previous height */
            int32_t new_width;      /* New width */
            int32_t new_height;     /* New height */
        } resize;
    } data;
} window_event_t;

/**
 * Window event handler callback type
 */
typedef void (*window_event_handler_t)(window_event_t *event);

/**
 * Window structure
 * Represents a single window managed by the window manager
 */
typedef struct window {
    /* Identification */
    uint32_t id;                            /* Unique window ID */
    char title[WINDOW_MAX_TITLE_LEN];       /* Window title */

    /* Position and size */
    int32_t x;                              /* X position on screen */
    int32_t y;                              /* Y position on screen */
    int32_t width;                          /* Content area width */
    int32_t height;                         /* Content area height */

    /* State and flags */
    window_state_t state;                   /* Current window state */
    uint32_t flags;                         /* Window flags */
    int32_t z_order;                        /* Z-order (higher = on top) */

    /* Content buffer */
    uint32_t *content_buffer;               /* Pixel buffer for content (ARGB) */
    size_t buffer_size;                     /* Size of content buffer in bytes */

    /* Saved position for restore from maximized/minimized */
    int32_t saved_x;
    int32_t saved_y;
    int32_t saved_width;
    int32_t saved_height;

    /* Window hierarchy */
    struct window *parent;                  /* Parent window (NULL for top-level) */
    struct window *children[WINDOW_MAX_CHILDREN]; /* Child windows */
    uint32_t child_count;                   /* Number of child windows */

    /* Event handling */
    window_event_handler_t event_handler;   /* Event callback function */
    void *user_data;                        /* User-defined data pointer */

    /* Internal linked list */
    struct window *next;                    /* Next window in manager list */
    struct window *prev;                    /* Previous window in manager list */
} window_t;

/**
 * Window manager state structure
 */
typedef struct window_manager {
    window_t *window_list;                  /* Head of window list (back) */
    window_t *window_tail;                  /* Tail of window list (front) */
    window_t *focused_window;               /* Currently focused window */
    window_t *dragging_window;              /* Window being dragged */
    window_t *resizing_window;              /* Window being resized */
    window_t *hover_window;                 /* Window under mouse cursor */

    uint32_t window_count;                  /* Total number of windows */
    uint32_t next_window_id;                /* Next available window ID */
    int32_t next_z_order;                   /* Next available Z-order */

    /* Screen dimensions */
    uint32_t screen_width;
    uint32_t screen_height;

    /* Drag/resize state */
    int32_t drag_start_x;                   /* Mouse X at drag start */
    int32_t drag_start_y;                   /* Mouse Y at drag start */
    int32_t drag_window_x;                  /* Window X at drag start */
    int32_t drag_window_y;                  /* Window Y at drag start */
    int32_t drag_window_width;              /* Window width at resize start */
    int32_t drag_window_height;             /* Window height at resize start */
    window_hit_region_t resize_edge;        /* Which edge is being resized */

    /* Mouse state */
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    uint8_t prev_mouse_buttons;

    bool initialized;                       /* Manager initialization status */
    bool needs_redraw;                      /* Flag for screen redraw needed */
} window_manager_t;

/* ============================================================================
 * Window Manager Initialization
 * ============================================================================ */

/**
 * Initialize the window manager
 * Must be called before any other window functions.
 * @return 0 on success, negative error code on failure
 */
int window_init(void);

/**
 * Shutdown the window manager and destroy all windows
 */
void window_shutdown(void);

/**
 * Check if window manager is initialized
 * @return true if initialized
 */
bool window_is_initialized(void);

/* ============================================================================
 * Window Creation and Destruction
 * ============================================================================ */

/**
 * Create a new window
 * @param title Window title (NULL for no title)
 * @param x Initial X position
 * @param y Initial Y position
 * @param width Content area width
 * @param height Content area height
 * @param flags Window flags (WINDOW_FLAGS_DEFAULT if 0)
 * @return Pointer to created window, or NULL on failure
 */
window_t *window_create(const char *title, int x, int y, int width, int height, uint32_t flags);

/**
 * Create a child window
 * @param parent Parent window
 * @param title Window title
 * @param x X position relative to parent
 * @param y Y position relative to parent
 * @param width Content area width
 * @param height Content area height
 * @param flags Window flags
 * @return Pointer to created window, or NULL on failure
 */
window_t *window_create_child(window_t *parent, const char *title, int x, int y,
                               int width, int height, uint32_t flags);

/**
 * Destroy a window and all its children
 * @param win Window to destroy
 */
void window_destroy(window_t *win);

/* ============================================================================
 * Window Visibility
 * ============================================================================ */

/**
 * Show a window (make visible)
 * @param win Window to show
 */
void window_show(window_t *win);

/**
 * Hide a window (make invisible)
 * @param win Window to hide
 */
void window_hide(window_t *win);

/**
 * Check if window is visible
 * @param win Window to check
 * @return true if visible
 */
bool window_is_visible(window_t *win);

/* ============================================================================
 * Window Position and Size
 * ============================================================================ */

/**
 * Move a window to a new position
 * @param win Window to move
 * @param x New X position
 * @param y New Y position
 */
void window_move(window_t *win, int x, int y);

/**
 * Resize a window
 * @param win Window to resize
 * @param width New content width
 * @param height New content height
 * @return 0 on success, negative error code on failure
 */
int window_resize(window_t *win, int width, int height);

/**
 * Set both position and size
 * @param win Window to modify
 * @param x New X position
 * @param y New Y position
 * @param width New content width
 * @param height New content height
 * @return 0 on success, negative error code on failure
 */
int window_set_bounds(window_t *win, int x, int y, int width, int height);

/**
 * Get window content area bounds
 * @param win Window to query
 * @param x Output: X position
 * @param y Output: Y position
 * @param width Output: content width
 * @param height Output: content height
 */
void window_get_bounds(window_t *win, int *x, int *y, int *width, int *height);

/**
 * Get total window bounds including decorations
 * @param win Window to query
 * @param x Output: X position
 * @param y Output: Y position
 * @param width Output: total width
 * @param height Output: total height
 */
void window_get_frame_bounds(window_t *win, int *x, int *y, int *width, int *height);

/* ============================================================================
 * Window State
 * ============================================================================ */

/**
 * Minimize a window
 * @param win Window to minimize
 */
void window_minimize(window_t *win);

/**
 * Maximize a window to fill the screen
 * @param win Window to maximize
 */
void window_maximize(window_t *win);

/**
 * Restore a window from minimized or maximized state
 * @param win Window to restore
 */
void window_restore(window_t *win);

/**
 * Get current window state
 * @param win Window to query
 * @return Current window state
 */
window_state_t window_get_state(window_t *win);

/* ============================================================================
 * Focus and Z-Order
 * ============================================================================ */

/**
 * Set input focus to a window
 * @param win Window to focus (NULL to unfocus all)
 */
void window_set_focus(window_t *win);

/**
 * Get the currently focused window
 * @return Focused window, or NULL if none
 */
window_t *window_get_focus(void);

/**
 * Bring a window to the front (top of Z-order)
 * @param win Window to raise
 */
void window_bring_to_front(window_t *win);

/**
 * Send a window to the back (bottom of Z-order)
 * @param win Window to lower
 */
void window_send_to_back(window_t *win);

/* ============================================================================
 * Window Properties
 * ============================================================================ */

/**
 * Set window title
 * @param win Window to modify
 * @param title New title string
 */
void window_set_title(window_t *win, const char *title);

/**
 * Get window title
 * @param win Window to query
 * @return Window title string
 */
const char *window_get_title(window_t *win);

/**
 * Set window flags
 * @param win Window to modify
 * @param flags New flags
 */
void window_set_flags(window_t *win, uint32_t flags);

/**
 * Get window flags
 * @param win Window to query
 * @return Current window flags
 */
uint32_t window_get_flags(window_t *win);

/**
 * Set event handler for a window
 * @param win Window to modify
 * @param handler Event handler callback
 */
void window_set_event_handler(window_t *win, window_event_handler_t handler);

/**
 * Set user data pointer for a window
 * @param win Window to modify
 * @param data User data pointer
 */
void window_set_user_data(window_t *win, void *data);

/**
 * Get user data pointer from a window
 * @param win Window to query
 * @return User data pointer
 */
void *window_get_user_data(window_t *win);

/* ============================================================================
 * Window Drawing
 * ============================================================================ */

/**
 * Draw a window and its decorations
 * @param win Window to draw
 */
void window_draw(window_t *win);

/**
 * Mark a window as needing redraw
 * @param win Window to invalidate
 */
void window_invalidate(window_t *win);

/**
 * Mark a rectangular region of a window as needing redraw
 * @param win Window containing the region
 * @param x Region X (relative to content area)
 * @param y Region Y (relative to content area)
 * @param width Region width
 * @param height Region height
 */
void window_invalidate_rect(window_t *win, int x, int y, int width, int height);

/**
 * Get pointer to window's content buffer
 * @param win Window to query
 * @return Pointer to content buffer (ARGB pixels)
 */
uint32_t *window_get_buffer(window_t *win);

/**
 * Clear window content with a color
 * @param win Window to clear
 * @param color Fill color (0xAARRGGBB)
 */
void window_clear(window_t *win, uint32_t color);

/* ============================================================================
 * Input Event Handling
 * ============================================================================ */

/**
 * Handle mouse input and route to appropriate window
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @param buttons Mouse button state
 */
void window_handle_mouse(int x, int y, uint8_t buttons);

/**
 * Handle keyboard input and route to focused window
 * @param keycode Keyboard scan code
 * @param modifiers Modifier key state (shift/ctrl/alt)
 * @param pressed true if key pressed, false if released
 */
void window_handle_key(uint8_t keycode, uint8_t modifiers, bool pressed);

/**
 * Find window at screen coordinates
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @return Window at coordinates, or NULL
 */
window_t *window_find_at(int x, int y);

/**
 * Hit test to determine which part of window is at coordinates
 * @param win Window to test
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @return Hit test region
 */
window_hit_region_t window_hit_test(window_t *win, int x, int y);

/* ============================================================================
 * Window Manager Operations
 * ============================================================================ */

/**
 * Render all windows to the screen
 * Should be called each frame by the compositor
 */
void window_render_all(void);

/**
 * Get window manager state (read-only)
 * @return Pointer to window manager state
 */
const window_manager_t *window_get_manager(void);

/**
 * Get window by ID
 * @param id Window ID
 * @return Window with given ID, or NULL
 */
window_t *window_get_by_id(uint32_t id);

/**
 * Iterate through all windows (from back to front)
 * @param callback Function to call for each window
 * @param user_data User data passed to callback
 */
void window_foreach(void (*callback)(window_t *win, void *data), void *user_data);

#endif /* _AAAOS_WINDOW_H */
