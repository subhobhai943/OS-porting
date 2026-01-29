/**
 * AAAos GUI - Desktop Environment Implementation
 *
 * Provides the complete desktop environment including:
 * - Desktop background with wallpaper support
 * - Desktop icons for launching applications
 * - Taskbar with start menu and running apps
 * - System tray with clock
 * - Application launcher/start menu
 */

#include "desktop.h"
#include "../compositor/compositor.h"
#include "../widgets/widget.h"
#include "../../kernel/include/types.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../kernel/proc/process.h"
#include "../../drivers/video/framebuffer.h"
#include "../../drivers/timer/rtc.h"
#include "../../lib/libc/string.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Global desktop instance */
static desktop_t g_desktop;

/* Start menu state */
static bool g_start_menu_open = false;

/* Start menu configuration */
#define START_MENU_WIDTH        200
#define START_MENU_HEIGHT       300
#define START_MENU_ITEM_HEIGHT  32
#define START_MENU_MAX_ITEMS    16

/* Start menu item */
typedef struct {
    char name[DESKTOP_ICON_NAME_MAX];
    char path[DESKTOP_ICON_PATH_MAX];
    bool is_separator;
} start_menu_item_t;

/* Start menu entries */
static start_menu_item_t g_start_menu_items[START_MENU_MAX_ITEMS];
static uint32_t g_start_menu_item_count = 0;
static int32_t g_start_menu_hover_index = -1;

/* Start menu colors */
#define START_MENU_BG_COLOR         0xF0303030
#define START_MENU_ITEM_HOVER       0xFF505050
#define START_MENU_TEXT_COLOR       0xFFFFFFFF
#define START_MENU_SEPARATOR_COLOR  0xFF606060

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void desktop_draw_wallpaper(void);
static void desktop_draw_icons(void);
static void desktop_draw_icon(desktop_icon_t *icon);
static void desktop_draw_taskbar(void);
static void desktop_draw_start_button(void);
static void desktop_draw_taskbar_items(void);
static void desktop_draw_system_tray(void);
static void desktop_draw_clock(void);
static void desktop_draw_start_menu(void);
static bool desktop_handle_icon_click(int32_t x, int32_t y, uint8_t button);
static bool desktop_handle_taskbar_click(int32_t x, int32_t y, uint8_t button);
static bool desktop_handle_start_menu_click(int32_t x, int32_t y, uint8_t button);
static void desktop_update_taskbar(void);
static void desktop_toggle_start_menu(void);
static void desktop_setup_default_icons(void);
static void desktop_setup_start_menu(void);

/* ============================================================================
 * Initialization and Shutdown
 * ============================================================================ */

/**
 * Initialize the desktop environment
 */
int desktop_init(void)
{
    const framebuffer_t *fb = fb_get_info();

    if (!fb || !fb->initialized) {
        kprintf("desktop: framebuffer not initialized\n");
        return -1;
    }

    kprintf("desktop: initializing desktop environment\n");

    /* Clear desktop state */
    memset(&g_desktop, 0, sizeof(desktop_t));

    /* Set screen dimensions */
    g_desktop.screen_width = fb->width;
    g_desktop.screen_height = fb->height;

    /* Initialize wallpaper */
    g_desktop.wallpaper_color = DESKTOP_WALLPAPER_COLOR;
    g_desktop.wallpaper_buffer = NULL;
    g_desktop.has_wallpaper_image = false;

    /* Initialize icons */
    g_desktop.icon_count = 0;
    g_desktop.next_icon_id = 1;
    g_desktop.selected_icon = NULL;
    g_desktop.hovered_icon = NULL;

    /* Initialize taskbar */
    if (taskbar_init() < 0) {
        kprintf("desktop: failed to initialize taskbar\n");
        return -1;
    }

    /* Initialize context menu */
    memset(&g_desktop.context_menu, 0, sizeof(context_menu_t));
    g_desktop.context_menu.visible = false;
    g_desktop.context_menu.hovered_item = -1;

    /* Initialize drag state */
    g_desktop.dragging_icon = NULL;
    g_desktop.drag_start_x = 0;
    g_desktop.drag_start_y = 0;

    /* Initialize double-click detection */
    g_desktop.last_click_time = 0;
    g_desktop.last_click_x = -1;
    g_desktop.last_click_y = -1;

    /* Get compositor reference */
    g_desktop.compositor = compositor_get_state();

    /* Initialize statistics */
    g_desktop.apps_launched = 0;
    g_desktop.icons_created = 0;
    g_desktop.icons_removed = 0;

    /* Set flags */
    g_desktop.flags = DESKTOP_FLAG_INITIALIZED | DESKTOP_FLAG_NEED_REDRAW;

    /* Setup default desktop icons */
    desktop_setup_default_icons();

    /* Setup start menu */
    desktop_setup_start_menu();

    kprintf("desktop: initialized (%ux%u)\n",
            g_desktop.screen_width, g_desktop.screen_height);

    return 0;
}

/**
 * Shutdown the desktop environment
 */
void desktop_shutdown(void)
{
    kprintf("desktop: shutting down\n");

    /* Free wallpaper buffer if allocated */
    if (g_desktop.wallpaper_buffer) {
        kfree(g_desktop.wallpaper_buffer);
        g_desktop.wallpaper_buffer = NULL;
    }

    /* Shutdown taskbar */
    taskbar_shutdown();

    /* Clear flags */
    g_desktop.flags = 0;

    kprintf("desktop: shutdown complete (launched %llu apps)\n",
            g_desktop.apps_launched);
}

/* ============================================================================
 * Taskbar Functions
 * ============================================================================ */

/**
 * Initialize the taskbar
 */
int taskbar_init(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    memset(taskbar, 0, sizeof(taskbar_t));

    /* Position taskbar at bottom of screen */
    taskbar->x = 0;
    taskbar->y = g_desktop.screen_height - TASKBAR_HEIGHT;
    taskbar->width = g_desktop.screen_width;
    taskbar->height = TASKBAR_HEIGHT;

    /* Initialize items */
    taskbar->item_count = 0;

    /* Initialize clock */
    taskbar->clock.hour = 0;
    taskbar->clock.minute = 0;
    taskbar->clock.second = 0;
    strcpy(taskbar->clock.display_string, "00:00");

    /* Initialize state */
    taskbar->start_hovered = false;
    taskbar->start_clicked = false;
    taskbar->initialized = true;
    taskbar->needs_redraw = true;

    /* Allocate taskbar buffer */
    taskbar->buffer = kmalloc(taskbar->width * taskbar->height * sizeof(uint32_t));
    if (!taskbar->buffer) {
        kprintf("desktop: failed to allocate taskbar buffer\n");
        return -1;
    }

    kprintf("desktop: taskbar initialized at y=%d\n", taskbar->y);

    return 0;
}

/**
 * Shutdown the taskbar
 */
void taskbar_shutdown(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    if (taskbar->buffer) {
        kfree(taskbar->buffer);
        taskbar->buffer = NULL;
    }

    taskbar->initialized = false;
}

/**
 * Add a window to the taskbar
 */
int taskbar_add_window(window_t *win)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    if (!win) return -1;
    if (taskbar->item_count >= TASKBAR_MAX_ITEMS) return -1;

    /* Check if window is already in taskbar */
    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        if (taskbar->items[i].window == win) {
            return 0; /* Already exists */
        }
    }

    /* Add new item */
    taskbar_item_t *item = &taskbar->items[taskbar->item_count];
    item->window = win;
    strncpy(item->title, win->title, WINDOW_TITLE_MAX_LEN - 1);
    item->title[WINDOW_TITLE_MAX_LEN - 1] = '\0';
    item->hovered = false;
    item->active = (win->flags & WINDOW_FLAG_ACTIVE) != 0;

    /* Calculate position */
    int32_t start_offset = TASKBAR_START_BUTTON_WIDTH + TASKBAR_ITEM_PADDING;
    item->x = start_offset + (taskbar->item_count * (TASKBAR_ITEM_WIDTH + TASKBAR_ITEM_PADDING));
    item->width = TASKBAR_ITEM_WIDTH;

    taskbar->item_count++;
    taskbar->needs_redraw = true;

    return 0;
}

/**
 * Remove a window from the taskbar
 */
int taskbar_remove_window(window_t *win)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    if (!win) return -1;

    /* Find window in taskbar */
    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        if (taskbar->items[i].window == win) {
            /* Shift remaining items */
            for (uint32_t j = i; j < taskbar->item_count - 1; j++) {
                taskbar->items[j] = taskbar->items[j + 1];
                /* Recalculate position */
                int32_t start_offset = TASKBAR_START_BUTTON_WIDTH + TASKBAR_ITEM_PADDING;
                taskbar->items[j].x = start_offset + (j * (TASKBAR_ITEM_WIDTH + TASKBAR_ITEM_PADDING));
            }
            taskbar->item_count--;
            taskbar->needs_redraw = true;
            return 0;
        }
    }

    return -1; /* Not found */
}

/**
 * Update window information in taskbar
 */
void taskbar_update_window(window_t *win)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    if (!win) return;

    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        if (taskbar->items[i].window == win) {
            strncpy(taskbar->items[i].title, win->title, WINDOW_TITLE_MAX_LEN - 1);
            taskbar->items[i].title[WINDOW_TITLE_MAX_LEN - 1] = '\0';
            taskbar->items[i].active = (win->flags & WINDOW_FLAG_ACTIVE) != 0;
            taskbar->needs_redraw = true;
            break;
        }
    }
}

/**
 * Update taskbar from current windows
 */
static void desktop_update_taskbar(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;
    const compositor_t *comp = g_desktop.compositor;

    if (!comp || !comp->initialized) return;

    /* Mark all items as potentially inactive */
    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        taskbar->items[i].active = false;
    }

    /* Update active window */
    window_t *active = comp->active_window;
    if (active) {
        for (uint32_t i = 0; i < taskbar->item_count; i++) {
            if (taskbar->items[i].window == active) {
                taskbar->items[i].active = true;
                break;
            }
        }
    }

    taskbar->needs_redraw = true;
}

/**
 * Update the taskbar clock
 */
void taskbar_update_clock(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;
    rtc_time_t time;

    if (rtc_get_time(&time) == 0) {
        taskbar->clock.hour = time.hour;
        taskbar->clock.minute = time.minute;
        taskbar->clock.second = time.second;

        /* Format time string */
        taskbar->clock.display_string[0] = '0' + (time.hour / 10);
        taskbar->clock.display_string[1] = '0' + (time.hour % 10);
        taskbar->clock.display_string[2] = ':';
        taskbar->clock.display_string[3] = '0' + (time.minute / 10);
        taskbar->clock.display_string[4] = '0' + (time.minute % 10);
        taskbar->clock.display_string[5] = '\0';

        taskbar->needs_redraw = true;
    }
}

/**
 * Find taskbar item at coordinates
 */
taskbar_item_t *taskbar_find_item_at(int32_t x)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        taskbar_item_t *item = &taskbar->items[i];
        if (x >= item->x && x < item->x + item->width) {
            return item;
        }
    }

    return NULL;
}

/**
 * Handle taskbar click event
 */
bool taskbar_handle_click(int32_t x, int32_t y, uint8_t button)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    (void)y; /* Y is relative to taskbar */

    if (button != MOUSE_BUTTON_LEFT) return false;

    /* Check start button */
    if (x >= 0 && x < TASKBAR_START_BUTTON_WIDTH) {
        desktop_toggle_start_menu();
        return true;
    }

    /* Check taskbar items */
    taskbar_item_t *item = taskbar_find_item_at(x);
    if (item && item->window) {
        /* Activate the window */
        compositor_raise_window(item->window);
        compositor_set_active_window(item->window);

        /* If minimized, restore it */
        if (item->window->flags & WINDOW_FLAG_MINIMIZED) {
            compositor_restore_window(item->window);
        }

        desktop_update_taskbar();
        return true;
    }

    return false;
}

/**
 * Handle taskbar mouse move event
 */
void taskbar_handle_mouse_move(int32_t x, int32_t y)
{
    taskbar_t *taskbar = &g_desktop.taskbar;
    bool changed = false;

    (void)y;

    /* Check start button hover */
    bool start_hover = (x >= 0 && x < TASKBAR_START_BUTTON_WIDTH);
    if (start_hover != taskbar->start_hovered) {
        taskbar->start_hovered = start_hover;
        changed = true;
    }

    /* Check taskbar items hover */
    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        taskbar_item_t *item = &taskbar->items[i];
        bool hover = (x >= item->x && x < item->x + item->width);
        if (hover != item->hovered) {
            item->hovered = hover;
            changed = true;
        }
    }

    if (changed) {
        taskbar->needs_redraw = true;
    }
}

/**
 * Invalidate taskbar (request redraw)
 */
void taskbar_invalidate(void)
{
    g_desktop.taskbar.needs_redraw = true;
}

/* ============================================================================
 * Icon Functions
 * ============================================================================ */

/**
 * Add an icon to the desktop
 */
desktop_icon_t *desktop_add_icon(const char *name, int x, int y, const char *path)
{
    if (g_desktop.icon_count >= DESKTOP_MAX_ICONS) {
        kprintf("desktop: maximum icons reached\n");
        return NULL;
    }

    desktop_icon_t *icon = &g_desktop.icons[g_desktop.icon_count];

    /* Initialize icon */
    memset(icon, 0, sizeof(desktop_icon_t));

    strncpy(icon->name, name, DESKTOP_ICON_NAME_MAX - 1);
    icon->name[DESKTOP_ICON_NAME_MAX - 1] = '\0';

    strncpy(icon->path, path, DESKTOP_ICON_PATH_MAX - 1);
    icon->path[DESKTOP_ICON_PATH_MAX - 1] = '\0';

    icon->x = x;
    icon->y = y;
    icon->flags = ICON_FLAG_VISIBLE;
    icon->id = g_desktop.next_icon_id++;

    /* Generate a simple colored icon based on name hash */
    uint32_t hash = 0;
    for (const char *p = name; *p; p++) {
        hash = hash * 31 + *p;
    }

    /* Create gradient icon */
    uint32_t color1 = 0xFF000000 | ((hash & 0xFF) << 16) | (((hash >> 8) & 0xFF) << 8) | ((hash >> 16) & 0xFF);
    uint32_t color2 = 0xFF000000 | (((hash >> 4) & 0xFF) << 16) | (((hash >> 12) & 0xFF) << 8) | ((hash >> 20) & 0xFF);

    for (int32_t iy = 0; iy < DESKTOP_ICON_SIZE; iy++) {
        for (int32_t ix = 0; ix < DESKTOP_ICON_SIZE; ix++) {
            /* Simple gradient */
            uint32_t r1 = FB_GET_RED(color1), g1 = FB_GET_GREEN(color1), b1 = FB_GET_BLUE(color1);
            uint32_t r2 = FB_GET_RED(color2), g2 = FB_GET_GREEN(color2), b2 = FB_GET_BLUE(color2);

            uint32_t t = (iy * 255) / DESKTOP_ICON_SIZE;
            uint32_t r = r1 + ((r2 - r1) * t) / 255;
            uint32_t g = g1 + ((g2 - g1) * t) / 255;
            uint32_t b = b1 + ((b2 - b1) * t) / 255;

            icon->icon_data[iy * DESKTOP_ICON_SIZE + ix] = FB_MAKE_COLOR(255, r, g, b);
        }
    }

    g_desktop.icon_count++;
    g_desktop.icons_created++;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;

    kprintf("desktop: added icon '%s' at (%d, %d)\n", name, x, y);

    return icon;
}

/**
 * Remove an icon from the desktop
 */
int desktop_remove_icon(desktop_icon_t *icon)
{
    if (!icon) return -1;

    /* Find icon index */
    int32_t index = -1;
    for (uint32_t i = 0; i < g_desktop.icon_count; i++) {
        if (&g_desktop.icons[i] == icon) {
            index = i;
            break;
        }
    }

    if (index < 0) return -1;

    /* Clear selection if this icon was selected */
    if (g_desktop.selected_icon == icon) {
        g_desktop.selected_icon = NULL;
    }
    if (g_desktop.hovered_icon == icon) {
        g_desktop.hovered_icon = NULL;
    }

    /* Shift remaining icons */
    for (uint32_t i = index; i < g_desktop.icon_count - 1; i++) {
        g_desktop.icons[i] = g_desktop.icons[i + 1];
    }

    g_desktop.icon_count--;
    g_desktop.icons_removed++;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;

    return 0;
}

/**
 * Find icon at screen coordinates
 */
desktop_icon_t *desktop_find_icon_at(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < g_desktop.icon_count; i++) {
        desktop_icon_t *icon = &g_desktop.icons[i];

        if (!(icon->flags & ICON_FLAG_VISIBLE)) continue;

        /* Check icon bounds (including text area) */
        int32_t icon_width = DESKTOP_ICON_SIZE;
        int32_t icon_height = DESKTOP_ICON_SIZE + DESKTOP_ICON_TEXT_HEIGHT;

        if (x >= icon->x && x < icon->x + icon_width &&
            y >= icon->y && y < icon->y + icon_height) {
            return icon;
        }
    }

    return NULL;
}

/**
 * Select an icon
 */
void desktop_select_icon(desktop_icon_t *icon)
{
    /* Deselect previous */
    if (g_desktop.selected_icon) {
        g_desktop.selected_icon->flags &= ~ICON_FLAG_SELECTED;
    }

    /* Select new */
    g_desktop.selected_icon = icon;
    if (icon) {
        icon->flags |= ICON_FLAG_SELECTED;
    }

    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Setup default desktop icons
 */
static void desktop_setup_default_icons(void)
{
    int32_t x = DESKTOP_ICON_SPACING;
    int32_t y = DESKTOP_ICON_SPACING;
    int32_t row_height = DESKTOP_ICON_GRID_SIZE;

    /* Add some default icons */
    desktop_add_icon("Terminal", x, y, "/apps/terminal");
    y += row_height;

    desktop_add_icon("Files", x, y, "/apps/files");
    y += row_height;

    desktop_add_icon("Settings", x, y, "/apps/settings");
    y += row_height;

    desktop_add_icon("Text Editor", x, y, "/apps/editor");
    y += row_height;

    desktop_add_icon("Calculator", x, y, "/apps/calculator");
}

/* ============================================================================
 * Start Menu Functions
 * ============================================================================ */

/**
 * Setup the start menu
 */
static void desktop_setup_start_menu(void)
{
    g_start_menu_item_count = 0;

    /* Add menu items */
    strncpy(g_start_menu_items[g_start_menu_item_count].name, "Terminal", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/apps/terminal", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;

    strncpy(g_start_menu_items[g_start_menu_item_count].name, "File Manager", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/apps/files", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;

    strncpy(g_start_menu_items[g_start_menu_item_count].name, "Text Editor", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/apps/editor", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;

    strncpy(g_start_menu_items[g_start_menu_item_count].name, "Calculator", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/apps/calculator", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;

    /* Separator */
    g_start_menu_items[g_start_menu_item_count].name[0] = '\0';
    g_start_menu_items[g_start_menu_item_count].path[0] = '\0';
    g_start_menu_items[g_start_menu_item_count].is_separator = true;
    g_start_menu_item_count++;

    strncpy(g_start_menu_items[g_start_menu_item_count].name, "Settings", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/apps/settings", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;

    /* Separator */
    g_start_menu_items[g_start_menu_item_count].name[0] = '\0';
    g_start_menu_items[g_start_menu_item_count].path[0] = '\0';
    g_start_menu_items[g_start_menu_item_count].is_separator = true;
    g_start_menu_item_count++;

    strncpy(g_start_menu_items[g_start_menu_item_count].name, "Shutdown", DESKTOP_ICON_NAME_MAX);
    strncpy(g_start_menu_items[g_start_menu_item_count].path, "/system/shutdown", DESKTOP_ICON_PATH_MAX);
    g_start_menu_items[g_start_menu_item_count].is_separator = false;
    g_start_menu_item_count++;
}

/**
 * Toggle the start menu
 */
static void desktop_toggle_start_menu(void)
{
    g_start_menu_open = !g_start_menu_open;
    g_start_menu_hover_index = -1;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;

    kprintf("desktop: start menu %s\n", g_start_menu_open ? "opened" : "closed");
}

/**
 * Handle start menu click
 */
static bool desktop_handle_start_menu_click(int32_t x, int32_t y, uint8_t button)
{
    if (!g_start_menu_open) return false;
    if (button != MOUSE_BUTTON_LEFT) return false;

    /* Calculate start menu bounds */
    int32_t menu_x = 0;
    int32_t menu_y = g_desktop.taskbar.y - START_MENU_HEIGHT;

    /* Check if click is inside menu */
    if (x < menu_x || x >= menu_x + START_MENU_WIDTH ||
        y < menu_y || y >= menu_y + START_MENU_HEIGHT) {
        /* Click outside menu - close it */
        g_start_menu_open = false;
        g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
        return false;
    }

    /* Find which item was clicked */
    int32_t item_y = menu_y + TASKBAR_ITEM_PADDING;
    for (uint32_t i = 0; i < g_start_menu_item_count; i++) {
        if (g_start_menu_items[i].is_separator) {
            item_y += 8; /* Separator height */
            continue;
        }

        if (y >= item_y && y < item_y + START_MENU_ITEM_HEIGHT) {
            /* Launch the application */
            desktop_launch_app(g_start_menu_items[i].path);
            g_start_menu_open = false;
            g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
            return true;
        }

        item_y += START_MENU_ITEM_HEIGHT;
    }

    return true;
}

/* ============================================================================
 * Drawing Functions
 * ============================================================================ */

/**
 * Draw the desktop
 */
void desktop_draw(void)
{
    if (!(g_desktop.flags & DESKTOP_FLAG_INITIALIZED)) return;

    /* Draw wallpaper */
    desktop_draw_wallpaper();

    /* Draw desktop icons */
    desktop_draw_icons();

    /* Draw taskbar */
    desktop_draw_taskbar();

    /* Draw start menu if open */
    if (g_start_menu_open) {
        desktop_draw_start_menu();
    }

    /* Draw context menu if visible */
    if (g_desktop.context_menu.visible) {
        desktop_draw_context_menu();
    }

    /* Clear redraw flag */
    g_desktop.flags &= ~DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Draw the wallpaper
 */
static void desktop_draw_wallpaper(void)
{
    if (g_desktop.has_wallpaper_image && g_desktop.wallpaper_buffer) {
        /* Draw wallpaper image */
        const framebuffer_t *fb = fb_get_info();
        if (fb && fb->address) {
            memcpy(fb->address, g_desktop.wallpaper_buffer,
                   g_desktop.screen_width * g_desktop.screen_height * sizeof(uint32_t));
        }
    } else {
        /* Draw solid color wallpaper */
        fb_fill_rect(0, 0, g_desktop.screen_width,
                     g_desktop.screen_height - TASKBAR_HEIGHT,
                     g_desktop.wallpaper_color);
    }
}

/**
 * Draw all desktop icons
 */
static void desktop_draw_icons(void)
{
    for (uint32_t i = 0; i < g_desktop.icon_count; i++) {
        desktop_icon_t *icon = &g_desktop.icons[i];
        if (icon->flags & ICON_FLAG_VISIBLE) {
            desktop_draw_icon(icon);
        }
    }
}

/**
 * Draw a single desktop icon
 */
static void desktop_draw_icon(desktop_icon_t *icon)
{
    int32_t x = icon->x;
    int32_t y = icon->y;

    /* Draw selection/hover background */
    if (icon->flags & ICON_FLAG_SELECTED) {
        fb_fill_rect(x - 2, y - 2,
                     DESKTOP_ICON_SIZE + 4, DESKTOP_ICON_SIZE + DESKTOP_ICON_TEXT_HEIGHT + 4,
                     DESKTOP_ICON_SELECT_COLOR);
    } else if (icon->flags & ICON_FLAG_HOVERED) {
        fb_fill_rect(x - 2, y - 2,
                     DESKTOP_ICON_SIZE + 4, DESKTOP_ICON_SIZE + DESKTOP_ICON_TEXT_HEIGHT + 4,
                     DESKTOP_ICON_HOVER_COLOR);
    }

    /* Draw icon image */
    for (int32_t iy = 0; iy < DESKTOP_ICON_SIZE; iy++) {
        for (int32_t ix = 0; ix < DESKTOP_ICON_SIZE; ix++) {
            uint32_t color = icon->icon_data[iy * DESKTOP_ICON_SIZE + ix];
            fb_put_pixel(x + ix, y + iy, color);
        }
    }

    /* Draw icon border */
    fb_draw_rect(x, y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, 0xFF404040);

    /* Draw icon name (centered below icon) */
    size_t name_len = strlen(icon->name);
    int32_t text_width = name_len * FB_FONT_WIDTH;
    int32_t text_x = x + (DESKTOP_ICON_SIZE - text_width) / 2;
    int32_t text_y = y + DESKTOP_ICON_SIZE + 2;

    /* Draw text shadow */
    fb_draw_string(text_x + 1, text_y + 1, icon->name, 0xFF000000, 0x00000000);
    /* Draw text */
    fb_draw_string(text_x, text_y, icon->name, DESKTOP_ICON_TEXT_COLOR, 0x00000000);
}

/**
 * Draw the taskbar
 */
static void desktop_draw_taskbar(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    /* Draw taskbar background */
    fb_fill_rect(taskbar->x, taskbar->y, taskbar->width, taskbar->height, TASKBAR_BG_COLOR);

    /* Draw top border */
    fb_draw_hline(taskbar->x, taskbar->y, taskbar->width, TASKBAR_BORDER_COLOR);

    /* Draw start button */
    desktop_draw_start_button();

    /* Draw taskbar items */
    desktop_draw_taskbar_items();

    /* Draw system tray and clock */
    desktop_draw_system_tray();
    desktop_draw_clock();

    taskbar->needs_redraw = false;
}

/**
 * Draw the start button
 */
static void desktop_draw_start_button(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    int32_t x = taskbar->x + 2;
    int32_t y = taskbar->y + 4;
    int32_t w = TASKBAR_START_BUTTON_WIDTH - 4;
    int32_t h = taskbar->height - 8;

    /* Draw button background */
    uint32_t bg_color = taskbar->start_hovered ? TASKBAR_ITEM_HOVER_COLOR : TASKBAR_START_COLOR;
    if (g_start_menu_open) {
        bg_color = TASKBAR_ITEM_ACTIVE_COLOR;
    }
    fb_fill_rect(x, y, w, h, bg_color);

    /* Draw button border */
    fb_draw_rect(x, y, w, h, TASKBAR_BORDER_COLOR);

    /* Draw "Start" text */
    const char *text = "Start";
    size_t text_len = strlen(text);
    int32_t text_x = x + (w - text_len * FB_FONT_WIDTH) / 2;
    int32_t text_y = y + (h - FB_FONT_HEIGHT) / 2;
    fb_draw_string(text_x, text_y, text, TASKBAR_TEXT_COLOR, 0x00000000);
}

/**
 * Draw taskbar items
 */
static void desktop_draw_taskbar_items(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    for (uint32_t i = 0; i < taskbar->item_count; i++) {
        taskbar_item_t *item = &taskbar->items[i];

        int32_t x = item->x;
        int32_t y = taskbar->y + 4;
        int32_t h = taskbar->height - 8;

        /* Draw item background */
        uint32_t bg_color = TASKBAR_ITEM_COLOR;
        if (item->active) {
            bg_color = TASKBAR_ITEM_ACTIVE_COLOR;
        } else if (item->hovered) {
            bg_color = TASKBAR_ITEM_HOVER_COLOR;
        }
        fb_fill_rect(x, y, item->width, h, bg_color);

        /* Draw border */
        fb_draw_rect(x, y, item->width, h, TASKBAR_BORDER_COLOR);

        /* Draw title (truncated if necessary) */
        char truncated[16];
        size_t max_chars = (item->width - 8) / FB_FONT_WIDTH;
        if (max_chars > 15) max_chars = 15;

        strncpy(truncated, item->title, max_chars);
        truncated[max_chars] = '\0';

        int32_t text_x = x + 4;
        int32_t text_y = y + (h - FB_FONT_HEIGHT) / 2;
        fb_draw_string(text_x, text_y, truncated, TASKBAR_TEXT_COLOR, 0x00000000);
    }
}

/**
 * Draw system tray
 */
static void desktop_draw_system_tray(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    int32_t tray_x = taskbar->width - TASKBAR_CLOCK_WIDTH - TASKBAR_SYSTRAY_WIDTH;
    int32_t tray_y = taskbar->y + 4;
    int32_t tray_h = taskbar->height - 8;

    /* Draw tray background */
    fb_fill_rect(tray_x, tray_y, TASKBAR_SYSTRAY_WIDTH, tray_h, TASKBAR_ITEM_COLOR);

    /* Draw separator */
    fb_draw_vline(tray_x, taskbar->y + 2, taskbar->height - 4, TASKBAR_BORDER_COLOR);
}

/**
 * Draw the clock
 */
static void desktop_draw_clock(void)
{
    taskbar_t *taskbar = &g_desktop.taskbar;

    /* Update clock time */
    taskbar_update_clock();

    int32_t clock_x = taskbar->width - TASKBAR_CLOCK_WIDTH;
    int32_t clock_y = taskbar->y + 4;
    int32_t clock_h = taskbar->height - 8;

    /* Draw clock background */
    fb_fill_rect(clock_x, clock_y, TASKBAR_CLOCK_WIDTH, clock_h, TASKBAR_ITEM_COLOR);

    /* Draw separator */
    fb_draw_vline(clock_x, taskbar->y + 2, taskbar->height - 4, TASKBAR_BORDER_COLOR);

    /* Draw time string */
    size_t time_len = strlen(taskbar->clock.display_string);
    int32_t text_x = clock_x + (TASKBAR_CLOCK_WIDTH - time_len * FB_FONT_WIDTH) / 2;
    int32_t text_y = clock_y + (clock_h - FB_FONT_HEIGHT) / 2;
    fb_draw_string(text_x, text_y, taskbar->clock.display_string, TASKBAR_CLOCK_COLOR, 0x00000000);
}

/**
 * Draw the start menu
 */
static void desktop_draw_start_menu(void)
{
    int32_t menu_x = 0;
    int32_t menu_y = g_desktop.taskbar.y - START_MENU_HEIGHT;

    /* Draw menu background */
    fb_fill_rect(menu_x, menu_y, START_MENU_WIDTH, START_MENU_HEIGHT, START_MENU_BG_COLOR);

    /* Draw border */
    fb_draw_rect(menu_x, menu_y, START_MENU_WIDTH, START_MENU_HEIGHT, TASKBAR_BORDER_COLOR);

    /* Draw menu items */
    int32_t item_y = menu_y + TASKBAR_ITEM_PADDING;

    for (uint32_t i = 0; i < g_start_menu_item_count; i++) {
        if (g_start_menu_items[i].is_separator) {
            /* Draw separator line */
            fb_draw_hline(menu_x + 8, item_y + 4, START_MENU_WIDTH - 16, START_MENU_SEPARATOR_COLOR);
            item_y += 8;
            continue;
        }

        /* Draw item background if hovered */
        if ((int32_t)i == g_start_menu_hover_index) {
            fb_fill_rect(menu_x + 2, item_y, START_MENU_WIDTH - 4, START_MENU_ITEM_HEIGHT, START_MENU_ITEM_HOVER);
        }

        /* Draw item text */
        int32_t text_x = menu_x + 12;
        int32_t text_y = item_y + (START_MENU_ITEM_HEIGHT - FB_FONT_HEIGHT) / 2;
        fb_draw_string(text_x, text_y, g_start_menu_items[i].name, START_MENU_TEXT_COLOR, 0x00000000);

        item_y += START_MENU_ITEM_HEIGHT;
    }
}

/* ============================================================================
 * Context Menu Functions
 * ============================================================================ */

/**
 * Show the desktop context menu
 */
void desktop_show_context_menu(int32_t x, int32_t y)
{
    context_menu_t *menu = &g_desktop.context_menu;

    /* Setup menu items */
    menu->item_count = 0;

    strcpy(menu->items[menu->item_count].label, "Refresh");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    menu->items[menu->item_count].separator = true;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "New Folder");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "New File");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    menu->items[menu->item_count].separator = true;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "Display Settings");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    /* Calculate menu size */
    menu->width = 150;
    menu->height = 0;
    for (uint32_t i = 0; i < menu->item_count; i++) {
        if (menu->items[i].separator) {
            menu->height += 8;
        } else {
            menu->height += 24;
        }
    }
    menu->height += 8; /* Padding */

    /* Position menu (ensure it stays on screen) */
    menu->x = x;
    menu->y = y;

    if (menu->x + menu->width > (int32_t)g_desktop.screen_width) {
        menu->x = g_desktop.screen_width - menu->width;
    }
    if (menu->y + menu->height > (int32_t)g_desktop.screen_height - TASKBAR_HEIGHT) {
        menu->y = g_desktop.screen_height - TASKBAR_HEIGHT - menu->height;
    }

    menu->hovered_item = -1;
    menu->visible = true;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Show icon context menu
 */
void desktop_show_icon_context_menu(desktop_icon_t *icon, int32_t x, int32_t y)
{
    context_menu_t *menu = &g_desktop.context_menu;

    (void)icon; /* Use icon info for context-specific items */

    /* Setup menu items */
    menu->item_count = 0;

    strcpy(menu->items[menu->item_count].label, "Open");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    menu->items[menu->item_count].separator = true;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "Rename");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "Delete");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    menu->items[menu->item_count].separator = true;
    menu->item_count++;

    strcpy(menu->items[menu->item_count].label, "Properties");
    menu->items[menu->item_count].enabled = true;
    menu->items[menu->item_count].separator = false;
    menu->item_count++;

    /* Calculate menu size */
    menu->width = 120;
    menu->height = 0;
    for (uint32_t i = 0; i < menu->item_count; i++) {
        if (menu->items[i].separator) {
            menu->height += 8;
        } else {
            menu->height += 24;
        }
    }
    menu->height += 8;

    /* Position menu */
    menu->x = x;
    menu->y = y;

    if (menu->x + menu->width > (int32_t)g_desktop.screen_width) {
        menu->x = g_desktop.screen_width - menu->width;
    }
    if (menu->y + menu->height > (int32_t)g_desktop.screen_height - TASKBAR_HEIGHT) {
        menu->y = g_desktop.screen_height - TASKBAR_HEIGHT - menu->height;
    }

    menu->hovered_item = -1;
    menu->visible = true;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Hide the context menu
 */
void desktop_hide_context_menu(void)
{
    g_desktop.context_menu.visible = false;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Draw the context menu
 */
void desktop_draw_context_menu(void)
{
    context_menu_t *menu = &g_desktop.context_menu;

    if (!menu->visible) return;

    /* Draw background */
    fb_fill_rect(menu->x, menu->y, menu->width, menu->height, START_MENU_BG_COLOR);

    /* Draw border */
    fb_draw_rect(menu->x, menu->y, menu->width, menu->height, TASKBAR_BORDER_COLOR);

    /* Draw items */
    int32_t item_y = menu->y + 4;

    for (uint32_t i = 0; i < menu->item_count; i++) {
        if (menu->items[i].separator) {
            fb_draw_hline(menu->x + 4, item_y + 4, menu->width - 8, START_MENU_SEPARATOR_COLOR);
            item_y += 8;
            continue;
        }

        /* Draw hover background */
        if ((int32_t)i == menu->hovered_item && menu->items[i].enabled) {
            fb_fill_rect(menu->x + 2, item_y, menu->width - 4, 24, START_MENU_ITEM_HOVER);
        }

        /* Draw text */
        uint32_t text_color = menu->items[i].enabled ? START_MENU_TEXT_COLOR : 0xFF808080;
        fb_draw_string(menu->x + 8, item_y + 4, menu->items[i].label, text_color, 0x00000000);

        item_y += 24;
    }
}

/**
 * Handle context menu click
 */
bool desktop_context_menu_handle_click(int32_t x, int32_t y)
{
    context_menu_t *menu = &g_desktop.context_menu;

    if (!menu->visible) return false;

    /* Check if click is inside menu */
    if (x < menu->x || x >= menu->x + menu->width ||
        y < menu->y || y >= menu->y + menu->height) {
        desktop_hide_context_menu();
        return false;
    }

    /* Find clicked item */
    int32_t item_y = menu->y + 4;

    for (uint32_t i = 0; i < menu->item_count; i++) {
        if (menu->items[i].separator) {
            item_y += 8;
            continue;
        }

        if (y >= item_y && y < item_y + 24) {
            if (menu->items[i].enabled && menu->items[i].action) {
                menu->items[i].action(menu->items[i].user_data);
            }
            desktop_hide_context_menu();
            return true;
        }

        item_y += 24;
    }

    return true;
}

/* ============================================================================
 * Event Handling
 * ============================================================================ */

/**
 * Handle an input event
 */
bool desktop_handle_event(event_t *event)
{
    if (!event) return false;
    if (!(g_desktop.flags & DESKTOP_FLAG_INITIALIZED)) return false;

    switch (event->type) {
        case EVENT_MOUSE_DOWN:
            return desktop_handle_click(event->mouse.global_x, event->mouse.global_y,
                                       event->mouse.buttons);

        case EVENT_MOUSE_MOVE: {
            int32_t x = event->mouse.global_x;
            int32_t y = event->mouse.global_y;

            /* Update icon hover state */
            desktop_icon_t *old_hover = g_desktop.hovered_icon;
            g_desktop.hovered_icon = desktop_find_icon_at(x, y);

            if (old_hover != g_desktop.hovered_icon) {
                if (old_hover) old_hover->flags &= ~ICON_FLAG_HOVERED;
                if (g_desktop.hovered_icon) g_desktop.hovered_icon->flags |= ICON_FLAG_HOVERED;
                g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
            }

            /* Update taskbar hover state */
            if (y >= g_desktop.taskbar.y) {
                taskbar_handle_mouse_move(x, y - g_desktop.taskbar.y);
            }

            return true;
        }

        case EVENT_KEY_DOWN:
            return desktop_handle_key(event->key.keycode);

        default:
            break;
    }

    return false;
}

/**
 * Handle mouse clicks
 */
static bool desktop_handle_click(int32_t x, int32_t y, uint8_t button)
{
    /* Check context menu first */
    if (g_desktop.context_menu.visible) {
        if (desktop_context_menu_handle_click(x, y)) {
            return true;
        }
    }

    /* Check start menu */
    if (g_start_menu_open) {
        if (desktop_handle_start_menu_click(x, y, button)) {
            return true;
        }
    }

    /* Check taskbar */
    if (y >= g_desktop.taskbar.y) {
        if (taskbar_handle_click(x, y - g_desktop.taskbar.y, button)) {
            return true;
        }
    }

    /* Check desktop icons */
    if (desktop_handle_icon_click(x, y, button)) {
        return true;
    }

    /* Right-click on empty desktop */
    if (button == MOUSE_BUTTON_RIGHT) {
        desktop_show_context_menu(x, y);
        return true;
    }

    /* Left-click on empty desktop - deselect icons */
    if (button == MOUSE_BUTTON_LEFT) {
        desktop_select_icon(NULL);

        /* Close start menu if open */
        if (g_start_menu_open) {
            g_start_menu_open = false;
            g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
        }
    }

    return false;
}

/**
 * Handle icon clicks
 */
static bool desktop_handle_icon_click(int32_t x, int32_t y, uint8_t button)
{
    desktop_icon_t *icon = desktop_find_icon_at(x, y);

    if (!icon) return false;

    if (button == MOUSE_BUTTON_LEFT) {
        /* Select the icon */
        desktop_select_icon(icon);

        /* Check for double-click */
        uint64_t current_time = rtc_get_unix_timestamp();
        if (g_desktop.selected_icon == icon &&
            (current_time - g_desktop.last_click_time) < 2 && /* ~2 seconds for double-click */
            (x - g_desktop.last_click_x) < 5 && (x - g_desktop.last_click_x) > -5 &&
            (y - g_desktop.last_click_y) < 5 && (y - g_desktop.last_click_y) > -5) {
            /* Double-click - launch application */
            desktop_launch_app(icon->path);
        }

        g_desktop.last_click_time = current_time;
        g_desktop.last_click_x = x;
        g_desktop.last_click_y = y;

        return true;
    } else if (button == MOUSE_BUTTON_RIGHT) {
        /* Select and show context menu */
        desktop_select_icon(icon);
        desktop_show_icon_context_menu(icon, x, y);
        return true;
    }

    return false;
}

/**
 * Handle keyboard input
 */
bool desktop_handle_key(uint16_t key)
{
    /* Handle keyboard shortcuts */

    /* Enter key - launch selected icon */
    if (key == 0x1C && g_desktop.selected_icon) { /* Enter scancode */
        desktop_launch_app(g_desktop.selected_icon->path);
        return true;
    }

    /* Delete key - (would delete selected icon) */
    if (key == 0x53 && g_desktop.selected_icon) { /* Delete scancode */
        /* Could implement icon deletion here */
        return true;
    }

    /* Escape - close menus */
    if (key == 0x01) { /* Escape scancode */
        if (g_start_menu_open) {
            g_start_menu_open = false;
            g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
            return true;
        }
        if (g_desktop.context_menu.visible) {
            desktop_hide_context_menu();
            return true;
        }
        desktop_select_icon(NULL);
        return true;
    }

    return false;
}

/* ============================================================================
 * Application Launching
 * ============================================================================ */

/**
 * Launch an application
 */
int desktop_launch_app(const char *path)
{
    if (!path || path[0] == '\0') {
        kprintf("desktop: cannot launch empty path\n");
        return -1;
    }

    kprintf("desktop: launching application: %s\n", path);

    /* Check for special system commands */
    if (strcmp(path, "/system/shutdown") == 0) {
        kprintf("desktop: shutdown requested\n");
        /* Implement shutdown here */
        return 0;
    }

    /* Create a new process for the application */
    /* Note: In a real implementation, this would load and execute the binary */
    process_t *proc = process_create(path, NULL);

    if (!proc) {
        kprintf("desktop: failed to create process for %s\n", path);
        return -1;
    }

    g_desktop.apps_launched++;

    kprintf("desktop: launched %s (PID %d)\n", path, proc->pid);

    return 0;
}

/* ============================================================================
 * Wallpaper Functions
 * ============================================================================ */

/**
 * Set wallpaper color
 */
void desktop_set_wallpaper_color(uint32_t color)
{
    g_desktop.wallpaper_color = color;
    g_desktop.has_wallpaper_image = false;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;

    kprintf("desktop: wallpaper color set to 0x%08X\n", color);
}

/**
 * Set wallpaper image
 */
int desktop_set_wallpaper_image(const uint32_t *image_data, uint32_t width, uint32_t height)
{
    if (!image_data || width == 0 || height == 0) {
        return -1;
    }

    /* Free existing wallpaper buffer */
    if (g_desktop.wallpaper_buffer) {
        kfree(g_desktop.wallpaper_buffer);
    }

    /* Allocate new buffer */
    size_t buffer_size = g_desktop.screen_width * g_desktop.screen_height * sizeof(uint32_t);
    g_desktop.wallpaper_buffer = kmalloc(buffer_size);

    if (!g_desktop.wallpaper_buffer) {
        kprintf("desktop: failed to allocate wallpaper buffer\n");
        return -1;
    }

    /* Scale or crop image to fit screen */
    for (uint32_t y = 0; y < g_desktop.screen_height; y++) {
        for (uint32_t x = 0; x < g_desktop.screen_width; x++) {
            /* Simple nearest-neighbor scaling */
            uint32_t src_x = (x * width) / g_desktop.screen_width;
            uint32_t src_y = (y * height) / g_desktop.screen_height;

            if (src_x < width && src_y < height) {
                g_desktop.wallpaper_buffer[y * g_desktop.screen_width + x] =
                    image_data[src_y * width + src_x];
            } else {
                g_desktop.wallpaper_buffer[y * g_desktop.screen_width + x] =
                    g_desktop.wallpaper_color;
            }
        }
    }

    g_desktop.has_wallpaper_image = true;
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;

    kprintf("desktop: wallpaper image set (%ux%u)\n", width, height);

    return 0;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Invalidate desktop (request redraw)
 */
void desktop_invalidate(void)
{
    g_desktop.flags |= DESKTOP_FLAG_NEED_REDRAW;
}

/**
 * Get the desktop state
 */
const desktop_t *desktop_get_state(void)
{
    return &g_desktop;
}

/* ============================================================================
 * Main Desktop Loop
 * ============================================================================ */

/**
 * Run the desktop main loop
 */
void desktop_run(void)
{
    if (!(g_desktop.flags & DESKTOP_FLAG_INITIALIZED)) {
        kprintf("desktop: not initialized, cannot run\n");
        return;
    }

    kprintf("desktop: starting main loop\n");

    g_desktop.flags |= DESKTOP_FLAG_RUNNING;

    /* Initial draw */
    desktop_draw();
    compositor_render();

    /* Main loop */
    while (g_desktop.flags & DESKTOP_FLAG_RUNNING) {
        /* Update taskbar (window list and clock) */
        desktop_update_taskbar();
        taskbar_update_clock();

        /* Check if redraw is needed */
        if (g_desktop.flags & DESKTOP_FLAG_NEED_REDRAW || g_desktop.taskbar.needs_redraw) {
            desktop_draw();
            compositor_render();
        }

        /* Small delay to prevent busy-waiting */
        /* In a real implementation, this would yield to the scheduler */
        for (volatile int i = 0; i < 100000; i++);
    }

    kprintf("desktop: main loop ended\n");
}

/**
 * Stop the desktop main loop
 */
void desktop_stop(void)
{
    g_desktop.flags &= ~DESKTOP_FLAG_RUNNING;
}
