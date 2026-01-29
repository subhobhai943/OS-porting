/**
 * AAAos GUI - Desktop Environment
 *
 * Provides the desktop environment with wallpaper display, taskbar/dock,
 * window management, desktop icons, and application launching.
 */

#ifndef _AAAOS_GUI_DESKTOP_H
#define _AAAOS_GUI_DESKTOP_H

#include "../../kernel/include/types.h"
#include "../compositor/compositor.h"
#include "../widgets/widget.h"

/* Desktop configuration */
#define DESKTOP_MAX_ICONS           64      /* Maximum number of desktop icons */
#define DESKTOP_ICON_SIZE           48      /* Icon size in pixels */
#define DESKTOP_ICON_SPACING        16      /* Spacing between icons */
#define DESKTOP_ICON_TEXT_HEIGHT    16      /* Height for icon text */
#define DESKTOP_ICON_GRID_SIZE      (DESKTOP_ICON_SIZE + DESKTOP_ICON_SPACING + DESKTOP_ICON_TEXT_HEIGHT)

/* Desktop icon name and path lengths */
#define DESKTOP_ICON_NAME_MAX       32      /* Maximum icon name length */
#define DESKTOP_ICON_PATH_MAX       256     /* Maximum application path length */

/* Taskbar configuration */
#define TASKBAR_HEIGHT              32      /* Taskbar height in pixels */
#define TASKBAR_ITEM_WIDTH          120     /* Width of each taskbar item */
#define TASKBAR_ITEM_PADDING        4       /* Padding around taskbar items */
#define TASKBAR_MAX_ITEMS           32      /* Maximum items in taskbar */
#define TASKBAR_CLOCK_WIDTH         64      /* Width of clock area */
#define TASKBAR_START_BUTTON_WIDTH  60      /* Width of start button */
#define TASKBAR_SYSTRAY_WIDTH       100     /* Width of system tray area */

/* Desktop colors */
#define DESKTOP_WALLPAPER_COLOR     0xFF2B5278  /* Default wallpaper color (dark blue) */
#define DESKTOP_ICON_TEXT_COLOR     0xFFFFFFFF  /* White icon text */
#define DESKTOP_ICON_SELECT_COLOR   0x803366FF  /* Semi-transparent selection */
#define DESKTOP_ICON_HOVER_COLOR    0x403366FF  /* Subtle hover effect */

/* Taskbar colors */
#define TASKBAR_BG_COLOR            0xE0303030  /* Dark semi-transparent background */
#define TASKBAR_ITEM_COLOR          0xFF404040  /* Item background */
#define TASKBAR_ITEM_ACTIVE_COLOR   0xFF5588FF  /* Active item background */
#define TASKBAR_ITEM_HOVER_COLOR    0xFF505050  /* Hovered item background */
#define TASKBAR_TEXT_COLOR          0xFFFFFFFF  /* White text */
#define TASKBAR_CLOCK_COLOR         0xFFFFFFFF  /* Clock text color */
#define TASKBAR_START_COLOR         0xFF4488FF  /* Start button color */
#define TASKBAR_BORDER_COLOR        0xFF606060  /* Border color */

/* Desktop state flags */
#define DESKTOP_FLAG_INITIALIZED    (1 << 0)    /* Desktop is initialized */
#define DESKTOP_FLAG_RUNNING        (1 << 1)    /* Desktop main loop is running */
#define DESKTOP_FLAG_NEED_REDRAW    (1 << 2)    /* Desktop needs redraw */
#define DESKTOP_FLAG_DRAGGING       (1 << 3)    /* Currently dragging an icon */

/* Icon state flags */
#define ICON_FLAG_VISIBLE           (1 << 0)    /* Icon is visible */
#define ICON_FLAG_SELECTED          (1 << 1)    /* Icon is selected */
#define ICON_FLAG_HOVERED           (1 << 2)    /* Mouse is over icon */

/* Double-click timing (in milliseconds) */
#define DESKTOP_DOUBLE_CLICK_TIME   500

/**
 * Desktop icon structure
 * Represents a shortcut on the desktop
 */
typedef struct desktop_icon {
    char name[DESKTOP_ICON_NAME_MAX];       /* Display name */
    char path[DESKTOP_ICON_PATH_MAX];       /* Application path to launch */
    int32_t x;                              /* X position on desktop */
    int32_t y;                              /* Y position on desktop */
    uint32_t flags;                         /* Icon flags */
    uint32_t icon_data[DESKTOP_ICON_SIZE * DESKTOP_ICON_SIZE];  /* Icon pixel data */
    uint32_t id;                            /* Unique icon ID */
} desktop_icon_t;

/**
 * Taskbar item structure
 * Represents a window entry in the taskbar
 */
typedef struct taskbar_item {
    window_t *window;                       /* Associated window */
    char title[WINDOW_TITLE_MAX_LEN];       /* Cached window title */
    int32_t x;                              /* X position on taskbar */
    int32_t width;                          /* Item width */
    bool hovered;                           /* Mouse is over this item */
    bool active;                            /* This is the active window */
} taskbar_item_t;

/**
 * System tray item structure
 */
typedef struct systray_item {
    uint32_t icon_data[16 * 16];            /* 16x16 icon */
    char tooltip[64];                       /* Tooltip text */
    void (*on_click)(void *data);           /* Click handler */
    void *user_data;                        /* User data for handler */
} systray_item_t;

/**
 * Clock structure for taskbar
 */
typedef struct taskbar_clock {
    int32_t hour;                           /* Current hour (0-23) */
    int32_t minute;                         /* Current minute (0-59) */
    int32_t second;                         /* Current second (0-59) */
    char display_string[16];                /* Formatted time string */
} taskbar_clock_t;

/**
 * Taskbar structure
 * Manages the bottom taskbar/dock
 */
typedef struct taskbar {
    int32_t x;                              /* Taskbar X position (usually 0) */
    int32_t y;                              /* Taskbar Y position (bottom of screen) */
    int32_t width;                          /* Taskbar width (screen width) */
    int32_t height;                         /* Taskbar height */

    taskbar_item_t items[TASKBAR_MAX_ITEMS];    /* Window items */
    uint32_t item_count;                        /* Number of items */

    taskbar_clock_t clock;                  /* Clock display */

    bool start_hovered;                     /* Start button hover state */
    bool start_clicked;                     /* Start button clicked */

    uint32_t *buffer;                       /* Taskbar render buffer */
    bool initialized;                       /* Initialization status */
    bool needs_redraw;                      /* Needs to be redrawn */
} taskbar_t;

/**
 * Context menu item structure
 */
typedef struct context_menu_item {
    char label[32];                         /* Menu item label */
    void (*action)(void *data);             /* Action callback */
    void *user_data;                        /* User data for callback */
    bool enabled;                           /* Item is enabled */
    bool separator;                         /* This is a separator line */
} context_menu_item_t;

/**
 * Context menu structure
 */
typedef struct context_menu {
    int32_t x;                              /* Menu X position */
    int32_t y;                              /* Menu Y position */
    int32_t width;                          /* Menu width */
    int32_t height;                         /* Menu height */
    context_menu_item_t items[16];          /* Menu items */
    uint32_t item_count;                    /* Number of items */
    int32_t hovered_item;                   /* Currently hovered item index (-1 for none) */
    bool visible;                           /* Menu is visible */
} context_menu_t;

/**
 * Desktop state structure
 * Main structure containing all desktop state
 */
typedef struct desktop {
    /* Screen dimensions */
    uint32_t screen_width;                  /* Screen width in pixels */
    uint32_t screen_height;                 /* Screen height in pixels */

    /* Desktop icons */
    desktop_icon_t icons[DESKTOP_MAX_ICONS];    /* Desktop icons */
    uint32_t icon_count;                        /* Number of icons */
    uint32_t next_icon_id;                      /* Next available icon ID */
    desktop_icon_t *selected_icon;              /* Currently selected icon */
    desktop_icon_t *hovered_icon;               /* Currently hovered icon */

    /* Taskbar */
    taskbar_t taskbar;                      /* Taskbar state */

    /* Context menu */
    context_menu_t context_menu;            /* Right-click context menu */

    /* Wallpaper */
    uint32_t wallpaper_color;               /* Solid wallpaper color */
    uint32_t *wallpaper_buffer;             /* Custom wallpaper image (optional) */
    bool has_wallpaper_image;               /* True if custom wallpaper is set */

    /* State flags */
    uint32_t flags;                         /* Desktop flags */

    /* Drag state */
    int32_t drag_start_x;                   /* Drag start X position */
    int32_t drag_start_y;                   /* Drag start Y position */
    int32_t drag_offset_x;                  /* Offset from icon origin */
    int32_t drag_offset_y;                  /* Offset from icon origin */
    desktop_icon_t *dragging_icon;          /* Icon being dragged */

    /* Double-click detection */
    uint64_t last_click_time;               /* Time of last click */
    int32_t last_click_x;                   /* X position of last click */
    int32_t last_click_y;                   /* Y position of last click */

    /* Compositor reference */
    const compositor_t *compositor;         /* Reference to compositor state */

    /* Statistics */
    uint64_t apps_launched;                 /* Number of applications launched */
    uint64_t icons_created;                 /* Total icons created */
    uint64_t icons_removed;                 /* Total icons removed */
} desktop_t;

/* ============================================================================
 * Desktop Functions
 * ============================================================================ */

/**
 * Initialize the desktop environment
 * @return 0 on success, negative error code on failure
 */
int desktop_init(void);

/**
 * Shutdown the desktop environment
 */
void desktop_shutdown(void);

/**
 * Add an icon to the desktop
 * @param name Display name for the icon
 * @param x X position on desktop
 * @param y Y position on desktop
 * @param path Application path to launch
 * @return Pointer to new icon, or NULL on failure
 */
desktop_icon_t *desktop_add_icon(const char *name, int x, int y, const char *path);

/**
 * Remove an icon from the desktop
 * @param icon Icon to remove
 * @return 0 on success, negative error code on failure
 */
int desktop_remove_icon(desktop_icon_t *icon);

/**
 * Launch an application
 * @param path Path to the application
 * @return 0 on success, negative error code on failure
 */
int desktop_launch_app(const char *path);

/**
 * Draw the desktop
 * Renders wallpaper, icons, and taskbar to the screen
 */
void desktop_draw(void);

/**
 * Handle an input event
 * @param event Event to process
 * @return true if event was handled
 */
bool desktop_handle_event(event_t *event);

/**
 * Run the desktop main loop
 * This function does not return until desktop is shut down
 */
void desktop_run(void);

/**
 * Get the desktop state
 * @return Pointer to desktop state (read-only)
 */
const desktop_t *desktop_get_state(void);

/**
 * Set wallpaper color
 * @param color 32-bit ARGB color
 */
void desktop_set_wallpaper_color(uint32_t color);

/**
 * Set wallpaper image
 * @param image_data Pixel data (32-bit ARGB)
 * @param width Image width
 * @param height Image height
 * @return 0 on success, negative error code on failure
 */
int desktop_set_wallpaper_image(const uint32_t *image_data, uint32_t width, uint32_t height);

/**
 * Find icon at screen coordinates
 * @param x X coordinate
 * @param y Y coordinate
 * @return Icon at coordinates, or NULL if none
 */
desktop_icon_t *desktop_find_icon_at(int32_t x, int32_t y);

/**
 * Select an icon
 * @param icon Icon to select (NULL to clear selection)
 */
void desktop_select_icon(desktop_icon_t *icon);

/**
 * Invalidate desktop (request redraw)
 */
void desktop_invalidate(void);

/* ============================================================================
 * Taskbar Functions
 * ============================================================================ */

/**
 * Initialize the taskbar
 * @return 0 on success, negative error code on failure
 */
int taskbar_init(void);

/**
 * Shutdown the taskbar
 */
void taskbar_shutdown(void);

/**
 * Add a window to the taskbar
 * @param win Window to add
 * @return 0 on success, negative error code on failure
 */
int taskbar_add_window(window_t *win);

/**
 * Remove a window from the taskbar
 * @param win Window to remove
 * @return 0 on success, negative error code on failure
 */
int taskbar_remove_window(window_t *win);

/**
 * Update window information in taskbar
 * @param win Window that was updated
 */
void taskbar_update_window(window_t *win);

/**
 * Draw the taskbar
 */
void taskbar_draw(void);

/**
 * Update the taskbar clock
 */
void taskbar_update_clock(void);

/**
 * Handle taskbar click event
 * @param x Click X coordinate
 * @param y Click Y coordinate (relative to taskbar)
 * @param button Mouse button
 * @return true if event was handled
 */
bool taskbar_handle_click(int32_t x, int32_t y, uint8_t button);

/**
 * Handle taskbar mouse move event
 * @param x Mouse X coordinate
 * @param y Mouse Y coordinate (relative to taskbar)
 */
void taskbar_handle_mouse_move(int32_t x, int32_t y);

/**
 * Find taskbar item at coordinates
 * @param x X coordinate
 * @return Taskbar item at coordinates, or NULL if none
 */
taskbar_item_t *taskbar_find_item_at(int32_t x);

/**
 * Invalidate taskbar (request redraw)
 */
void taskbar_invalidate(void);

/* ============================================================================
 * Context Menu Functions
 * ============================================================================ */

/**
 * Show the desktop context menu
 * @param x X position
 * @param y Y position
 */
void desktop_show_context_menu(int32_t x, int32_t y);

/**
 * Show icon context menu
 * @param icon Icon to show menu for
 * @param x X position
 * @param y Y position
 */
void desktop_show_icon_context_menu(desktop_icon_t *icon, int32_t x, int32_t y);

/**
 * Hide the context menu
 */
void desktop_hide_context_menu(void);

/**
 * Draw the context menu
 */
void desktop_draw_context_menu(void);

/**
 * Handle context menu click
 * @param x Click X coordinate
 * @param y Click Y coordinate
 * @return true if event was handled
 */
bool desktop_context_menu_handle_click(int32_t x, int32_t y);

#endif /* _AAAOS_GUI_DESKTOP_H */
