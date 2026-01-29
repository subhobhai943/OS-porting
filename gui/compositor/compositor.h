/**
 * AAAos GUI - Window Compositor
 *
 * Manages multiple windows with Z-ordering, double buffering,
 * and compositing all windows to the framebuffer.
 */

#ifndef _AAAOS_COMPOSITOR_H
#define _AAAOS_COMPOSITOR_H

#include "../../kernel/include/types.h"

/* Maximum number of windows */
#define COMPOSITOR_MAX_WINDOWS      64

/* Maximum window title length */
#define WINDOW_TITLE_MAX_LEN        128

/* Window decoration dimensions */
#define WINDOW_TITLE_BAR_HEIGHT     24
#define WINDOW_BORDER_WIDTH         2
#define WINDOW_BUTTON_SIZE          16
#define WINDOW_BUTTON_PADDING       4

/* Window decoration colors */
#define WINDOW_TITLE_BAR_ACTIVE     0xFF3366FF  /* Blue title bar for active window */
#define WINDOW_TITLE_BAR_INACTIVE   0xFF808080  /* Gray title bar for inactive */
#define WINDOW_TITLE_TEXT_COLOR     0xFFFFFFFF  /* White text */
#define WINDOW_BORDER_COLOR         0xFF404040  /* Dark gray border */
#define WINDOW_BORDER_ACTIVE        0xFF5588FF  /* Highlighted border for active */

/* Desktop background color */
#define DESKTOP_BACKGROUND_COLOR    0xFF2B5278  /* Dark blue */

/* Window flags */
#define WINDOW_FLAG_VISIBLE         (1 << 0)    /* Window is visible */
#define WINDOW_FLAG_ACTIVE          (1 << 1)    /* Window is active (focused) */
#define WINDOW_FLAG_MOVABLE         (1 << 2)    /* Window can be moved */
#define WINDOW_FLAG_RESIZABLE       (1 << 3)    /* Window can be resized */
#define WINDOW_FLAG_DECORATED       (1 << 4)    /* Window has title bar and borders */
#define WINDOW_FLAG_TRANSPARENT     (1 << 5)    /* Window supports alpha blending */
#define WINDOW_FLAG_MINIMIZED       (1 << 6)    /* Window is minimized */
#define WINDOW_FLAG_MAXIMIZED       (1 << 7)    /* Window is maximized */
#define WINDOW_FLAG_DIRTY           (1 << 8)    /* Window needs redraw */

/* Default window flags for normal windows */
#define WINDOW_FLAGS_DEFAULT        (WINDOW_FLAG_VISIBLE | WINDOW_FLAG_MOVABLE | \
                                     WINDOW_FLAG_RESIZABLE | WINDOW_FLAG_DECORATED)

/**
 * Window structure
 * Represents a single window in the compositor
 */
typedef struct window {
    int32_t x;                              /* X position on screen */
    int32_t y;                              /* Y position on screen */
    int32_t width;                          /* Window content width */
    int32_t height;                         /* Window content height */
    uint32_t *buffer;                       /* Window pixel buffer (32-bit ARGB) */
    int32_t z_order;                        /* Z-order (higher = on top) */
    uint32_t flags;                         /* Window flags */
    char title[WINDOW_TITLE_MAX_LEN];       /* Window title */
    uint32_t id;                            /* Unique window ID */

    /* Saved position for restore from maximized */
    int32_t saved_x;
    int32_t saved_y;
    int32_t saved_width;
    int32_t saved_height;

    /* Linked list pointers for window management */
    struct window *next;
    struct window *prev;
} window_t;

/**
 * Dirty region structure
 * Represents a rectangular region that needs to be redrawn
 */
typedef struct dirty_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool valid;
} dirty_rect_t;

/**
 * Compositor state structure
 * Global state for the window compositor
 */
typedef struct compositor {
    uint32_t screen_width;                  /* Screen width in pixels */
    uint32_t screen_height;                 /* Screen height in pixels */
    uint32_t *back_buffer;                  /* Double buffer for compositing */
    uint32_t *front_buffer;                 /* Pointer to actual framebuffer */

    window_t *window_list;                  /* Head of window linked list (back to front) */
    window_t *window_tail;                  /* Tail of window linked list (frontmost) */
    window_t *active_window;                /* Currently active/focused window */

    uint32_t window_count;                  /* Number of active windows */
    uint32_t next_window_id;                /* Next available window ID */
    int32_t next_z_order;                   /* Next available Z-order value */

    dirty_rect_t dirty_region;              /* Current dirty region */
    bool needs_full_redraw;                 /* Flag for full screen redraw */
    bool initialized;                       /* Compositor initialization status */

    /* Statistics */
    uint64_t frame_count;                   /* Number of frames rendered */
    uint64_t windows_created;               /* Total windows created */
    uint64_t windows_destroyed;             /* Total windows destroyed */
} compositor_t;

/**
 * Initialize the compositor
 * @param width Screen width in pixels
 * @param height Screen height in pixels
 * @return 0 on success, negative error code on failure
 */
int compositor_init(uint32_t width, uint32_t height);

/**
 * Shutdown the compositor and free all resources
 */
void compositor_shutdown(void);

/**
 * Create a new window
 * @param x Initial X position
 * @param y Initial Y position
 * @param w Window content width
 * @param h Window content height
 * @param title Window title (NULL for no title)
 * @return Pointer to new window, or NULL on failure
 */
window_t *compositor_create_window(int x, int y, int w, int h, const char *title);

/**
 * Destroy a window and free its resources
 * @param win Window to destroy
 */
void compositor_destroy_window(window_t *win);

/**
 * Move a window to new position
 * @param win Window to move
 * @param x New X position
 * @param y New Y position
 */
void compositor_move_window(window_t *win, int x, int y);

/**
 * Resize a window
 * @param win Window to resize
 * @param w New width
 * @param h New height
 * @return 0 on success, negative error code on failure
 */
int compositor_resize_window(window_t *win, int w, int h);

/**
 * Bring a window to the front (highest Z-order)
 * @param win Window to raise
 */
void compositor_raise_window(window_t *win);

/**
 * Send a window to the back (lowest Z-order)
 * @param win Window to lower
 */
void compositor_lower_window(window_t *win);

/**
 * Set the active (focused) window
 * @param win Window to activate (NULL to deactivate all)
 */
void compositor_set_active_window(window_t *win);

/**
 * Get the currently active window
 * @return Pointer to active window, or NULL if none
 */
window_t *compositor_get_active_window(void);

/**
 * Render all windows to the screen
 * Performs compositing and copies back buffer to front buffer
 */
void compositor_render(void);

/**
 * Mark a rectangular region as dirty (needs redraw)
 * @param x Region X coordinate
 * @param y Region Y coordinate
 * @param w Region width
 * @param h Region height
 */
void compositor_invalidate(int x, int y, int w, int h);

/**
 * Mark entire screen as dirty
 */
void compositor_invalidate_all(void);

/**
 * Find window at screen coordinates
 * @param x Screen X coordinate
 * @param y Screen Y coordinate
 * @return Window at coordinates, or NULL if none
 */
window_t *compositor_find_window_at(int x, int y);

/**
 * Get window by ID
 * @param id Window ID
 * @return Window with given ID, or NULL if not found
 */
window_t *compositor_get_window_by_id(uint32_t id);

/**
 * Show or hide a window
 * @param win Window to show/hide
 * @param visible true to show, false to hide
 */
void compositor_set_window_visible(window_t *win, bool visible);

/**
 * Minimize a window
 * @param win Window to minimize
 */
void compositor_minimize_window(window_t *win);

/**
 * Maximize a window
 * @param win Window to maximize
 */
void compositor_maximize_window(window_t *win);

/**
 * Restore a minimized or maximized window
 * @param win Window to restore
 */
void compositor_restore_window(window_t *win);

/**
 * Get compositor state
 * @return Pointer to compositor state structure (read-only)
 */
const compositor_t *compositor_get_state(void);

/**
 * Check if compositor is initialized
 * @return true if initialized, false otherwise
 */
bool compositor_is_initialized(void);

/**
 * Get total window dimensions (including decorations)
 * @param win Window to query
 * @param total_width Output: total width including decorations
 * @param total_height Output: total height including decorations
 */
void compositor_get_window_total_size(window_t *win, int *total_width, int *total_height);

/**
 * Get window content area position (excluding decorations)
 * @param win Window to query
 * @param content_x Output: X position of content area
 * @param content_y Output: Y position of content area
 */
void compositor_get_window_content_pos(window_t *win, int *content_x, int *content_y);

#endif /* _AAAOS_COMPOSITOR_H */
