/**
 * AAAos GUI - Window Manager Implementation
 *
 * Provides window creation, management, decorations, and input routing.
 */

#include "window.h"
#include "../../drivers/video/framebuffer.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/* Global window manager state */
static window_manager_t g_wm;

/* Forward declarations for internal functions */
static void wm_link_window(window_t *win);
static void wm_unlink_window(window_t *win);
static void wm_draw_decorations(window_t *win);
static void wm_draw_titlebar(window_t *win);
static void wm_draw_borders(window_t *win);
static void wm_draw_buttons(window_t *win);
static void wm_blit_content(window_t *win);
static void wm_send_event(window_t *win, window_event_type_t type);
static void wm_handle_drag(int x, int y);
static void wm_handle_resize(int x, int y);
static void wm_start_drag(window_t *win, int x, int y);
static void wm_start_resize(window_t *win, int x, int y, window_hit_region_t edge);
static void wm_end_drag(void);
static void wm_end_resize(void);
static void wm_clamp_to_screen(window_t *win);
static int wm_get_decoration_height(window_t *win);
static int wm_get_decoration_width(window_t *win);

/* ============================================================================
 * Window Manager Initialization
 * ============================================================================ */

int window_init(void) {
    kprintf("[WINDOW] Initializing window manager\n");

    if (g_wm.initialized) {
        kprintf("[WINDOW] Warning: Already initialized, reinitializing\n");
        window_shutdown();
    }

    /* Clear state */
    memset(&g_wm, 0, sizeof(window_manager_t));

    /* Get screen dimensions from framebuffer */
    const framebuffer_t *fb = fb_get_info();
    if (!fb || !fb->initialized) {
        kprintf("[WINDOW] Error: Framebuffer not initialized\n");
        return -1;
    }

    g_wm.screen_width = fb->width;
    g_wm.screen_height = fb->height;
    g_wm.next_window_id = 1;
    g_wm.next_z_order = 0;
    g_wm.initialized = true;
    g_wm.needs_redraw = true;

    kprintf("[WINDOW] Window manager initialized (%u x %u)\n",
            g_wm.screen_width, g_wm.screen_height);
    return 0;
}

void window_shutdown(void) {
    kprintf("[WINDOW] Shutting down window manager\n");

    if (!g_wm.initialized) {
        return;
    }

    /* Destroy all windows */
    window_t *win = g_wm.window_list;
    while (win) {
        window_t *next = win->next;

        /* Free content buffer */
        if (win->content_buffer) {
            kfree(win->content_buffer);
        }

        /* Free window structure */
        kfree(win);
        win = next;
    }

    memset(&g_wm, 0, sizeof(window_manager_t));
    kprintf("[WINDOW] Window manager shutdown complete\n");
}

bool window_is_initialized(void) {
    return g_wm.initialized;
}

/* ============================================================================
 * Window Creation and Destruction
 * ============================================================================ */

window_t *window_create(const char *title, int x, int y, int width, int height, uint32_t flags) {
    if (!g_wm.initialized) {
        kprintf("[WINDOW] Error: Window manager not initialized\n");
        return NULL;
    }

    if (g_wm.window_count >= WINDOW_MAX_COUNT) {
        kprintf("[WINDOW] Error: Maximum window count reached (%u)\n", WINDOW_MAX_COUNT);
        return NULL;
    }

    /* Enforce minimum dimensions */
    if (width < WINDOW_MIN_WIDTH) {
        width = WINDOW_MIN_WIDTH;
    }
    if (height < WINDOW_MIN_HEIGHT) {
        height = WINDOW_MIN_HEIGHT;
    }

    /* Use default flags if none specified */
    if (flags == 0) {
        flags = WINDOW_FLAGS_DEFAULT;
    }

    /* Allocate window structure */
    window_t *win = (window_t *)kmalloc(sizeof(window_t));
    if (!win) {
        kprintf("[WINDOW] Error: Failed to allocate window structure\n");
        return NULL;
    }
    memset(win, 0, sizeof(window_t));

    /* Allocate content buffer */
    size_t buffer_size = (size_t)width * height * sizeof(uint32_t);
    win->content_buffer = (uint32_t *)kmalloc(buffer_size);
    if (!win->content_buffer) {
        kprintf("[WINDOW] Error: Failed to allocate content buffer (%u bytes)\n",
                (uint32_t)buffer_size);
        kfree(win);
        return NULL;
    }
    win->buffer_size = buffer_size;

    /* Initialize content to white */
    for (int i = 0; i < width * height; i++) {
        win->content_buffer[i] = WINDOW_COLOR_CONTENT_BG;
    }

    /* Set window properties */
    win->id = g_wm.next_window_id++;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->state = WINDOW_STATE_NORMAL;
    win->flags = flags | WINDOW_DIRTY;
    win->z_order = g_wm.next_z_order++;

    /* Save initial position for restore */
    win->saved_x = x;
    win->saved_y = y;
    win->saved_width = width;
    win->saved_height = height;

    /* Set title */
    if (title) {
        strncpy(win->title, title, WINDOW_MAX_TITLE_LEN - 1);
        win->title[WINDOW_MAX_TITLE_LEN - 1] = '\0';
    } else {
        win->title[0] = '\0';
    }

    /* Link into window list */
    wm_link_window(win);

    /* Set focus to new window */
    window_set_focus(win);

    g_wm.window_count++;
    g_wm.needs_redraw = true;

    /* Send create event */
    wm_send_event(win, WINDOW_EVENT_CREATE);

    kprintf("[WINDOW] Created window %u: \"%s\" at (%d, %d) size %dx%d\n",
            win->id, title ? title : "(untitled)", x, y, width, height);

    return win;
}

window_t *window_create_child(window_t *parent, const char *title, int x, int y,
                               int width, int height, uint32_t flags) {
    if (!parent) {
        kprintf("[WINDOW] Error: Cannot create child without parent\n");
        return NULL;
    }

    if (parent->child_count >= WINDOW_MAX_CHILDREN) {
        kprintf("[WINDOW] Error: Parent has maximum children (%u)\n", WINDOW_MAX_CHILDREN);
        return NULL;
    }

    /* Create window at absolute position */
    int abs_x = parent->x + x;
    int abs_y = parent->y + y;
    window_t *child = window_create(title, abs_x, abs_y, width, height, flags);

    if (child) {
        child->parent = parent;
        parent->children[parent->child_count++] = child;

        /* Child inherits parent's z-order + 1 to stay on top */
        child->z_order = parent->z_order + 1;
    }

    return child;
}

void window_destroy(window_t *win) {
    if (!win) {
        return;
    }

    kprintf("[WINDOW] Destroying window %u: \"%s\"\n", win->id, win->title);

    /* Send destroy event before cleanup */
    wm_send_event(win, WINDOW_EVENT_DESTROY);

    /* Destroy all children first */
    while (win->child_count > 0) {
        window_destroy(win->children[--win->child_count]);
    }

    /* Remove from parent's child list */
    if (win->parent) {
        for (uint32_t i = 0; i < win->parent->child_count; i++) {
            if (win->parent->children[i] == win) {
                /* Shift remaining children */
                for (uint32_t j = i; j < win->parent->child_count - 1; j++) {
                    win->parent->children[j] = win->parent->children[j + 1];
                }
                win->parent->child_count--;
                break;
            }
        }
    }

    /* Clear any references in window manager */
    if (g_wm.focused_window == win) {
        g_wm.focused_window = NULL;
    }
    if (g_wm.dragging_window == win) {
        g_wm.dragging_window = NULL;
    }
    if (g_wm.resizing_window == win) {
        g_wm.resizing_window = NULL;
    }
    if (g_wm.hover_window == win) {
        g_wm.hover_window = NULL;
    }

    /* Unlink from list */
    wm_unlink_window(win);

    /* Free content buffer */
    if (win->content_buffer) {
        kfree(win->content_buffer);
    }

    /* Free window structure */
    kfree(win);

    g_wm.window_count--;
    g_wm.needs_redraw = true;

    /* Set focus to next window */
    if (!g_wm.focused_window && g_wm.window_tail) {
        window_set_focus(g_wm.window_tail);
    }
}

/* ============================================================================
 * Window Visibility
 * ============================================================================ */

void window_show(window_t *win) {
    if (!win) {
        return;
    }

    if (win->state == WINDOW_STATE_HIDDEN) {
        win->state = WINDOW_STATE_NORMAL;
        win->flags |= WINDOW_DIRTY;
        g_wm.needs_redraw = true;
        wm_send_event(win, WINDOW_EVENT_SHOW);
        kprintf("[WINDOW] Showing window %u\n", win->id);
    }
}

void window_hide(window_t *win) {
    if (!win) {
        return;
    }

    if (win->state != WINDOW_STATE_HIDDEN) {
        win->state = WINDOW_STATE_HIDDEN;
        g_wm.needs_redraw = true;
        wm_send_event(win, WINDOW_EVENT_HIDE);

        /* Remove focus if this window had it */
        if (g_wm.focused_window == win) {
            window_set_focus(NULL);
        }

        kprintf("[WINDOW] Hiding window %u\n", win->id);
    }
}

bool window_is_visible(window_t *win) {
    if (!win) {
        return false;
    }
    return win->state != WINDOW_STATE_HIDDEN && win->state != WINDOW_STATE_MINIMIZED;
}

/* ============================================================================
 * Window Position and Size
 * ============================================================================ */

void window_move(window_t *win, int x, int y) {
    if (!win) {
        return;
    }

    if (!(win->flags & WINDOW_MOVABLE)) {
        kprintf("[WINDOW] Warning: Window %u is not movable\n", win->id);
        return;
    }

    int old_x = win->x;
    int old_y = win->y;

    win->x = x;
    win->y = y;

    /* Clamp to screen bounds */
    wm_clamp_to_screen(win);

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    /* Send move event */
    window_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = WINDOW_EVENT_MOVE;
    event.window = win;
    event.data.move.old_x = old_x;
    event.data.move.old_y = old_y;
    event.data.move.new_x = win->x;
    event.data.move.new_y = win->y;

    if (win->event_handler) {
        win->event_handler(&event);
    }
}

int window_resize(window_t *win, int width, int height) {
    if (!win) {
        return -1;
    }

    if (!(win->flags & WINDOW_RESIZABLE)) {
        kprintf("[WINDOW] Warning: Window %u is not resizable\n", win->id);
        return -2;
    }

    /* Enforce minimum dimensions */
    if (width < WINDOW_MIN_WIDTH) {
        width = WINDOW_MIN_WIDTH;
    }
    if (height < WINDOW_MIN_HEIGHT) {
        height = WINDOW_MIN_HEIGHT;
    }

    /* No change needed */
    if (width == win->width && height == win->height) {
        return 0;
    }

    int old_width = win->width;
    int old_height = win->height;

    /* Allocate new buffer */
    size_t new_size = (size_t)width * height * sizeof(uint32_t);
    uint32_t *new_buffer = (uint32_t *)kmalloc(new_size);
    if (!new_buffer) {
        kprintf("[WINDOW] Error: Failed to allocate resize buffer\n");
        return -3;
    }

    /* Clear new buffer to background color */
    for (int i = 0; i < width * height; i++) {
        new_buffer[i] = WINDOW_COLOR_CONTENT_BG;
    }

    /* Copy old content (as much as fits) */
    int copy_w = MIN(old_width, width);
    int copy_h = MIN(old_height, height);
    for (int row = 0; row < copy_h; row++) {
        memcpy(&new_buffer[row * width],
               &win->content_buffer[row * old_width],
               copy_w * sizeof(uint32_t));
    }

    /* Free old buffer and update */
    kfree(win->content_buffer);
    win->content_buffer = new_buffer;
    win->buffer_size = new_size;
    win->width = width;
    win->height = height;

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    /* Send resize event */
    window_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = WINDOW_EVENT_RESIZE;
    event.window = win;
    event.data.resize.old_width = old_width;
    event.data.resize.old_height = old_height;
    event.data.resize.new_width = width;
    event.data.resize.new_height = height;

    if (win->event_handler) {
        win->event_handler(&event);
    }

    kprintf("[WINDOW] Resized window %u to %dx%d\n", win->id, width, height);
    return 0;
}

int window_set_bounds(window_t *win, int x, int y, int width, int height) {
    if (!win) {
        return -1;
    }

    window_move(win, x, y);
    return window_resize(win, width, height);
}

void window_get_bounds(window_t *win, int *x, int *y, int *width, int *height) {
    if (!win) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    if (x) *x = win->x;
    if (y) *y = win->y;
    if (width) *width = win->width;
    if (height) *height = win->height;
}

void window_get_frame_bounds(window_t *win, int *x, int *y, int *width, int *height) {
    if (!win) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    int deco_h = wm_get_decoration_height(win);
    int deco_w = wm_get_decoration_width(win);

    if (x) *x = win->x - ((win->flags & WINDOW_HAS_BORDER) ? WINDOW_BORDER_WIDTH : 0);
    if (y) *y = win->y - ((win->flags & WINDOW_HAS_TITLEBAR) ? WINDOW_TITLEBAR_HEIGHT : 0);
    if (width) *width = win->width + deco_w;
    if (height) *height = win->height + deco_h;
}

/* ============================================================================
 * Window State
 * ============================================================================ */

void window_minimize(window_t *win) {
    if (!win || win->state == WINDOW_STATE_MINIMIZED) {
        return;
    }

    /* Save current state if not already saved */
    if (win->state == WINDOW_STATE_NORMAL) {
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_width = win->width;
        win->saved_height = win->height;
    }

    win->state = WINDOW_STATE_MINIMIZED;
    g_wm.needs_redraw = true;

    /* Remove focus */
    if (g_wm.focused_window == win) {
        /* Find next visible window */
        window_t *next = win->prev;
        while (next && !window_is_visible(next)) {
            next = next->prev;
        }
        window_set_focus(next);
    }

    wm_send_event(win, WINDOW_EVENT_MINIMIZE);
    kprintf("[WINDOW] Minimized window %u\n", win->id);
}

void window_maximize(window_t *win) {
    if (!win || win->state == WINDOW_STATE_MAXIMIZED) {
        return;
    }

    /* Save current state for restore */
    if (win->state == WINDOW_STATE_NORMAL) {
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_width = win->width;
        win->saved_height = win->height;
    }

    win->state = WINDOW_STATE_MAXIMIZED;

    /* Calculate maximized size accounting for decorations */
    int deco_h = wm_get_decoration_height(win);
    int deco_w = wm_get_decoration_width(win);
    int title_h = (win->flags & WINDOW_HAS_TITLEBAR) ? WINDOW_TITLEBAR_HEIGHT : 0;
    int border_w = (win->flags & WINDOW_HAS_BORDER) ? WINDOW_BORDER_WIDTH : 0;

    /* Move to top-left and resize */
    win->x = border_w;
    win->y = title_h;
    window_resize(win, (int)g_wm.screen_width - deco_w, (int)g_wm.screen_height - deco_h);

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    wm_send_event(win, WINDOW_EVENT_MAXIMIZE);
    kprintf("[WINDOW] Maximized window %u\n", win->id);
}

void window_restore(window_t *win) {
    if (!win || win->state == WINDOW_STATE_NORMAL) {
        return;
    }

    window_state_t old_state = win->state;
    win->state = WINDOW_STATE_NORMAL;

    /* Restore saved position and size */
    win->x = win->saved_x;
    win->y = win->saved_y;
    window_resize(win, win->saved_width, win->saved_height);

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    /* Bring to front if was minimized */
    if (old_state == WINDOW_STATE_MINIMIZED) {
        window_bring_to_front(win);
    }

    wm_send_event(win, WINDOW_EVENT_RESTORE);
    kprintf("[WINDOW] Restored window %u\n", win->id);
}

window_state_t window_get_state(window_t *win) {
    if (!win) {
        return WINDOW_STATE_HIDDEN;
    }
    return win->state;
}

/* ============================================================================
 * Focus and Z-Order
 * ============================================================================ */

void window_set_focus(window_t *win) {
    /* Check if window can receive focus */
    if (win && (win->flags & WINDOW_NO_FOCUS)) {
        return;
    }

    /* Blur previous focused window */
    if (g_wm.focused_window && g_wm.focused_window != win) {
        g_wm.focused_window->flags |= WINDOW_DIRTY;
        wm_send_event(g_wm.focused_window, WINDOW_EVENT_BLUR);
    }

    g_wm.focused_window = win;

    if (win) {
        win->flags |= WINDOW_DIRTY;
        wm_send_event(win, WINDOW_EVENT_FOCUS);
    }

    g_wm.needs_redraw = true;
}

window_t *window_get_focus(void) {
    return g_wm.focused_window;
}

void window_bring_to_front(window_t *win) {
    if (!win) {
        return;
    }

    /* Already at front? */
    if (win == g_wm.window_tail) {
        window_set_focus(win);
        return;
    }

    /* Unlink and re-link at end */
    wm_unlink_window(win);
    win->z_order = g_wm.next_z_order++;
    wm_link_window(win);

    /* Set focus */
    window_set_focus(win);

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    kprintf("[WINDOW] Raised window %u to front (z=%d)\n", win->id, win->z_order);
}

void window_send_to_back(window_t *win) {
    if (!win) {
        return;
    }

    /* Already at back? */
    if (win == g_wm.window_list) {
        return;
    }

    /* Unlink from current position */
    wm_unlink_window(win);

    /* Set lowest z-order */
    window_t *lowest = g_wm.window_list;
    win->z_order = lowest ? lowest->z_order - 1 : 0;

    /* Link at front of list (back of z-order) */
    win->prev = NULL;
    win->next = g_wm.window_list;
    if (g_wm.window_list) {
        g_wm.window_list->prev = win;
    }
    g_wm.window_list = win;
    if (!g_wm.window_tail) {
        g_wm.window_tail = win;
    }

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;

    kprintf("[WINDOW] Sent window %u to back (z=%d)\n", win->id, win->z_order);
}

/* ============================================================================
 * Window Properties
 * ============================================================================ */

void window_set_title(window_t *win, const char *title) {
    if (!win) {
        return;
    }

    if (title) {
        strncpy(win->title, title, WINDOW_MAX_TITLE_LEN - 1);
        win->title[WINDOW_MAX_TITLE_LEN - 1] = '\0';
    } else {
        win->title[0] = '\0';
    }

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;
}

const char *window_get_title(window_t *win) {
    if (!win) {
        return "";
    }
    return win->title;
}

void window_set_flags(window_t *win, uint32_t flags) {
    if (!win) {
        return;
    }
    win->flags = flags | WINDOW_DIRTY;
    g_wm.needs_redraw = true;
}

uint32_t window_get_flags(window_t *win) {
    if (!win) {
        return 0;
    }
    return win->flags;
}

void window_set_event_handler(window_t *win, window_event_handler_t handler) {
    if (win) {
        win->event_handler = handler;
    }
}

void window_set_user_data(window_t *win, void *data) {
    if (win) {
        win->user_data = data;
    }
}

void *window_get_user_data(window_t *win) {
    if (!win) {
        return NULL;
    }
    return win->user_data;
}

/* ============================================================================
 * Window Drawing
 * ============================================================================ */

void window_draw(window_t *win) {
    if (!win || !window_is_visible(win)) {
        return;
    }

    /* Draw decorations first */
    wm_draw_decorations(win);

    /* Then blit content buffer */
    wm_blit_content(win);

    /* Clear dirty flag */
    win->flags &= ~WINDOW_DIRTY;
}

void window_invalidate(window_t *win) {
    if (win) {
        win->flags |= WINDOW_DIRTY;
        g_wm.needs_redraw = true;
    }
}

void window_invalidate_rect(window_t *win, int x, int y, int width, int height) {
    /* For now, just invalidate the whole window */
    /* A more sophisticated implementation would track dirty rectangles */
    UNUSED(x);
    UNUSED(y);
    UNUSED(width);
    UNUSED(height);
    window_invalidate(win);
}

uint32_t *window_get_buffer(window_t *win) {
    if (!win) {
        return NULL;
    }
    return win->content_buffer;
}

void window_clear(window_t *win, uint32_t color) {
    if (!win || !win->content_buffer) {
        return;
    }

    int pixels = win->width * win->height;
    for (int i = 0; i < pixels; i++) {
        win->content_buffer[i] = color;
    }

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;
}

/* ============================================================================
 * Input Event Handling
 * ============================================================================ */

void window_handle_mouse(int x, int y, uint8_t buttons) {
    if (!g_wm.initialized) {
        return;
    }

    /* Save previous state */
    int prev_x = g_wm.mouse_x;
    int prev_y = g_wm.mouse_y;
    g_wm.prev_mouse_buttons = g_wm.mouse_buttons;
    g_wm.mouse_x = x;
    g_wm.mouse_y = y;
    g_wm.mouse_buttons = buttons;

    /* Check for button press/release */
    bool left_pressed = (buttons & MOUSE_BUTTON_LEFT) && !(g_wm.prev_mouse_buttons & MOUSE_BUTTON_LEFT);
    bool left_released = !(buttons & MOUSE_BUTTON_LEFT) && (g_wm.prev_mouse_buttons & MOUSE_BUTTON_LEFT);

    /* Handle ongoing drag */
    if (g_wm.dragging_window) {
        if (left_released) {
            wm_end_drag();
        } else {
            wm_handle_drag(x, y);
        }
        return;
    }

    /* Handle ongoing resize */
    if (g_wm.resizing_window) {
        if (left_released) {
            wm_end_resize();
        } else {
            wm_handle_resize(x, y);
        }
        return;
    }

    /* Find window under cursor */
    window_t *win = window_find_at(x, y);

    /* Handle hover state changes */
    if (win != g_wm.hover_window) {
        if (g_wm.hover_window) {
            wm_send_event(g_wm.hover_window, WINDOW_EVENT_MOUSE_LEAVE);
        }
        g_wm.hover_window = win;
        if (win) {
            wm_send_event(win, WINDOW_EVENT_MOUSE_ENTER);
        }
    }

    /* Handle button press */
    if (left_pressed && win) {
        /* Bring window to front and focus */
        window_bring_to_front(win);

        /* Determine what was clicked */
        window_hit_region_t hit = window_hit_test(win, x, y);

        switch (hit) {
            case WINDOW_HIT_BUTTON_CLOSE:
                wm_send_event(win, WINDOW_EVENT_CLOSE);
                /* Let event handler decide whether to destroy */
                break;

            case WINDOW_HIT_BUTTON_MINIMIZE:
                window_minimize(win);
                break;

            case WINDOW_HIT_BUTTON_MAXIMIZE:
                if (win->state == WINDOW_STATE_MAXIMIZED) {
                    window_restore(win);
                } else {
                    window_maximize(win);
                }
                break;

            case WINDOW_HIT_TITLEBAR:
                if (win->flags & WINDOW_MOVABLE) {
                    wm_start_drag(win, x, y);
                }
                break;

            case WINDOW_HIT_BORDER_LEFT:
            case WINDOW_HIT_BORDER_RIGHT:
            case WINDOW_HIT_BORDER_TOP:
            case WINDOW_HIT_BORDER_BOTTOM:
            case WINDOW_HIT_CORNER_TL:
            case WINDOW_HIT_CORNER_TR:
            case WINDOW_HIT_CORNER_BL:
            case WINDOW_HIT_CORNER_BR:
                if (win->flags & WINDOW_RESIZABLE) {
                    wm_start_resize(win, x, y, hit);
                }
                break;

            case WINDOW_HIT_CLIENT:
                /* Send mouse event to window */
                {
                    window_event_t event;
                    memset(&event, 0, sizeof(event));
                    event.type = WINDOW_EVENT_MOUSE_DOWN;
                    event.window = win;
                    event.data.mouse.x = x - win->x;
                    event.data.mouse.y = y - win->y;
                    event.data.mouse.buttons = buttons;
                    if (win->event_handler) {
                        win->event_handler(&event);
                    }
                }
                break;

            default:
                break;
        }
    } else if (left_released && win) {
        /* Send mouse up event */
        window_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = WINDOW_EVENT_MOUSE_UP;
        event.window = win;
        event.data.mouse.x = x - win->x;
        event.data.mouse.y = y - win->y;
        event.data.mouse.buttons = buttons;
        if (win->event_handler) {
            win->event_handler(&event);
        }
    } else if (win && (x != prev_x || y != prev_y)) {
        /* Send mouse move event */
        window_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = WINDOW_EVENT_MOUSE_MOVE;
        event.window = win;
        event.data.mouse.x = x - win->x;
        event.data.mouse.y = y - win->y;
        event.data.mouse.buttons = buttons;
        if (win->event_handler) {
            win->event_handler(&event);
        }
    }
}

void window_handle_key(uint8_t keycode, uint8_t modifiers, bool pressed) {
    if (!g_wm.initialized || !g_wm.focused_window) {
        return;
    }

    window_t *win = g_wm.focused_window;

    window_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = pressed ? WINDOW_EVENT_KEY_DOWN : WINDOW_EVENT_KEY_UP;
    event.window = win;
    event.data.key.keycode = keycode;
    event.data.key.modifiers = modifiers;
    /* TODO: Convert keycode to ASCII character */
    event.data.key.character = 0;

    if (win->event_handler) {
        win->event_handler(&event);
    }
}

window_t *window_find_at(int x, int y) {
    /* Search from front to back (highest z-order first) */
    window_t *win = g_wm.window_tail;
    while (win) {
        if (!window_is_visible(win)) {
            win = win->prev;
            continue;
        }

        int frame_x, frame_y, frame_w, frame_h;
        window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

        if (x >= frame_x && x < frame_x + frame_w &&
            y >= frame_y && y < frame_y + frame_h) {
            return win;
        }

        win = win->prev;
    }
    return NULL;
}

window_hit_region_t window_hit_test(window_t *win, int x, int y) {
    if (!win) {
        return WINDOW_HIT_NONE;
    }

    int frame_x, frame_y, frame_w, frame_h;
    window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

    /* Check if outside window */
    if (x < frame_x || x >= frame_x + frame_w ||
        y < frame_y || y >= frame_y + frame_h) {
        return WINDOW_HIT_NONE;
    }

    bool has_titlebar = (win->flags & WINDOW_HAS_TITLEBAR) != 0;
    bool has_border = (win->flags & WINDOW_HAS_BORDER) != 0;

    /* Check title bar buttons (from right to left) */
    if (has_titlebar) {
        int title_y = frame_y;
        int title_h = WINDOW_TITLEBAR_HEIGHT;

        if (y >= title_y && y < title_y + title_h) {
            int btn_y = title_y + (WINDOW_TITLEBAR_HEIGHT - WINDOW_BUTTON_SIZE) / 2;
            int btn_right = frame_x + frame_w - WINDOW_BUTTON_PADDING;

            /* Close button */
            if ((win->flags & WINDOW_HAS_CLOSE_BTN) &&
                x >= btn_right - WINDOW_BUTTON_SIZE && x < btn_right &&
                y >= btn_y && y < btn_y + WINDOW_BUTTON_SIZE) {
                return WINDOW_HIT_BUTTON_CLOSE;
            }
            btn_right -= WINDOW_BUTTON_SIZE + WINDOW_BUTTON_PADDING;

            /* Maximize button */
            if ((win->flags & WINDOW_HAS_MAXIMIZE_BTN) &&
                x >= btn_right - WINDOW_BUTTON_SIZE && x < btn_right &&
                y >= btn_y && y < btn_y + WINDOW_BUTTON_SIZE) {
                return WINDOW_HIT_BUTTON_MAXIMIZE;
            }
            btn_right -= WINDOW_BUTTON_SIZE + WINDOW_BUTTON_PADDING;

            /* Minimize button */
            if ((win->flags & WINDOW_HAS_MINIMIZE_BTN) &&
                x >= btn_right - WINDOW_BUTTON_SIZE && x < btn_right &&
                y >= btn_y && y < btn_y + WINDOW_BUTTON_SIZE) {
                return WINDOW_HIT_BUTTON_MINIMIZE;
            }

            /* Rest of title bar is for dragging */
            return WINDOW_HIT_TITLEBAR;
        }
    }

    /* Check borders for resize */
    if (has_border) {
        int corner_size = WINDOW_BORDER_WIDTH * 4;  /* Larger hit area for corners */

        bool at_left = x < frame_x + WINDOW_BORDER_WIDTH + 2;
        bool at_right = x >= frame_x + frame_w - WINDOW_BORDER_WIDTH - 2;
        bool at_top = y < frame_y + (has_titlebar ? WINDOW_TITLEBAR_HEIGHT : WINDOW_BORDER_WIDTH + 2);
        bool at_bottom = y >= frame_y + frame_h - WINDOW_BORDER_WIDTH - 2;

        /* Corners */
        if (at_left && at_top && x < frame_x + corner_size && y < frame_y + corner_size) {
            return WINDOW_HIT_CORNER_TL;
        }
        if (at_right && at_top && x >= frame_x + frame_w - corner_size && y < frame_y + corner_size) {
            return WINDOW_HIT_CORNER_TR;
        }
        if (at_left && at_bottom && x < frame_x + corner_size && y >= frame_y + frame_h - corner_size) {
            return WINDOW_HIT_CORNER_BL;
        }
        if (at_right && at_bottom && x >= frame_x + frame_w - corner_size && y >= frame_y + frame_h - corner_size) {
            return WINDOW_HIT_CORNER_BR;
        }

        /* Edges */
        if (at_left) return WINDOW_HIT_BORDER_LEFT;
        if (at_right) return WINDOW_HIT_BORDER_RIGHT;
        if (at_bottom) return WINDOW_HIT_BORDER_BOTTOM;
    }

    /* Must be in client area */
    return WINDOW_HIT_CLIENT;
}

/* ============================================================================
 * Window Manager Operations
 * ============================================================================ */

void window_render_all(void) {
    if (!g_wm.initialized) {
        return;
    }

    /* Draw windows from back to front */
    window_t *win = g_wm.window_list;
    while (win) {
        if (window_is_visible(win)) {
            window_draw(win);
        }
        win = win->next;
    }

    g_wm.needs_redraw = false;
}

const window_manager_t *window_get_manager(void) {
    return &g_wm;
}

window_t *window_get_by_id(uint32_t id) {
    window_t *win = g_wm.window_list;
    while (win) {
        if (win->id == id) {
            return win;
        }
        win = win->next;
    }
    return NULL;
}

void window_foreach(void (*callback)(window_t *win, void *data), void *user_data) {
    if (!callback) {
        return;
    }

    window_t *win = g_wm.window_list;
    while (win) {
        window_t *next = win->next;  /* Save next in case callback destroys window */
        callback(win, user_data);
        win = next;
    }
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

static void wm_link_window(window_t *win) {
    if (!win) {
        return;
    }

    win->prev = g_wm.window_tail;
    win->next = NULL;

    if (g_wm.window_tail) {
        g_wm.window_tail->next = win;
    } else {
        g_wm.window_list = win;
    }

    g_wm.window_tail = win;
}

static void wm_unlink_window(window_t *win) {
    if (!win) {
        return;
    }

    if (win->prev) {
        win->prev->next = win->next;
    } else {
        g_wm.window_list = win->next;
    }

    if (win->next) {
        win->next->prev = win->prev;
    } else {
        g_wm.window_tail = win->prev;
    }

    win->prev = NULL;
    win->next = NULL;
}

static void wm_draw_decorations(window_t *win) {
    if (!win) {
        return;
    }

    if (win->flags & WINDOW_HAS_TITLEBAR) {
        wm_draw_titlebar(win);
        wm_draw_buttons(win);
    }

    if (win->flags & WINDOW_HAS_BORDER) {
        wm_draw_borders(win);
    }
}

static void wm_draw_titlebar(window_t *win) {
    if (!win || !(win->flags & WINDOW_HAS_TITLEBAR)) {
        return;
    }

    bool is_focused = (win == g_wm.focused_window);
    uint32_t title_color = is_focused ? WINDOW_COLOR_TITLEBAR_ACTIVE : WINDOW_COLOR_TITLEBAR_INACTIVE;

    int frame_x, frame_y, frame_w, frame_h;
    window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

    /* Draw title bar background */
    fb_fill_rect(frame_x, frame_y, frame_w, WINDOW_TITLEBAR_HEIGHT, title_color);

    /* Draw title text */
    if (win->title[0] != '\0') {
        int text_x = frame_x + WINDOW_BUTTON_PADDING + 4;
        int text_y = frame_y + (WINDOW_TITLEBAR_HEIGHT - FB_FONT_HEIGHT) / 2;
        fb_draw_string(text_x, text_y, win->title, WINDOW_COLOR_TITLE_TEXT, title_color);
    }
}

static void wm_draw_borders(window_t *win) {
    if (!win || !(win->flags & WINDOW_HAS_BORDER)) {
        return;
    }

    bool is_focused = (win == g_wm.focused_window);
    uint32_t border_color = is_focused ? WINDOW_COLOR_BORDER_ACTIVE : WINDOW_COLOR_BORDER_INACTIVE;

    int frame_x, frame_y, frame_w, frame_h;
    window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

    int content_top = frame_y + ((win->flags & WINDOW_HAS_TITLEBAR) ? WINDOW_TITLEBAR_HEIGHT : 0);

    /* Left border */
    fb_fill_rect(frame_x, content_top, WINDOW_BORDER_WIDTH,
                 frame_h - (content_top - frame_y), border_color);

    /* Right border */
    fb_fill_rect(frame_x + frame_w - WINDOW_BORDER_WIDTH, content_top,
                 WINDOW_BORDER_WIDTH, frame_h - (content_top - frame_y), border_color);

    /* Bottom border */
    fb_fill_rect(frame_x, frame_y + frame_h - WINDOW_BORDER_WIDTH,
                 frame_w, WINDOW_BORDER_WIDTH, border_color);
}

static void wm_draw_buttons(window_t *win) {
    if (!win || !(win->flags & WINDOW_HAS_TITLEBAR)) {
        return;
    }

    int frame_x, frame_y, frame_w, frame_h;
    window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

    int btn_y = frame_y + (WINDOW_TITLEBAR_HEIGHT - WINDOW_BUTTON_SIZE) / 2;
    int btn_x = frame_x + frame_w - WINDOW_BUTTON_PADDING - WINDOW_BUTTON_SIZE;

    /* Close button (red) */
    if (win->flags & WINDOW_HAS_CLOSE_BTN) {
        fb_fill_rect(btn_x, btn_y, WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE, WINDOW_COLOR_BUTTON_CLOSE);
        /* Draw X */
        for (int i = 2; i < WINDOW_BUTTON_SIZE - 2; i++) {
            fb_put_pixel(btn_x + i, btn_y + i, WINDOW_COLOR_TITLE_TEXT);
            fb_put_pixel(btn_x + WINDOW_BUTTON_SIZE - 1 - i, btn_y + i, WINDOW_COLOR_TITLE_TEXT);
        }
        btn_x -= WINDOW_BUTTON_SIZE + WINDOW_BUTTON_PADDING;
    }

    /* Maximize button (green) */
    if (win->flags & WINDOW_HAS_MAXIMIZE_BTN) {
        fb_fill_rect(btn_x, btn_y, WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE, WINDOW_COLOR_BUTTON_MAXIMIZE);
        /* Draw square outline */
        fb_draw_rect(btn_x + 3, btn_y + 3, WINDOW_BUTTON_SIZE - 6, WINDOW_BUTTON_SIZE - 6, WINDOW_COLOR_TITLE_TEXT);
        btn_x -= WINDOW_BUTTON_SIZE + WINDOW_BUTTON_PADDING;
    }

    /* Minimize button (yellow) */
    if (win->flags & WINDOW_HAS_MINIMIZE_BTN) {
        fb_fill_rect(btn_x, btn_y, WINDOW_BUTTON_SIZE, WINDOW_BUTTON_SIZE, WINDOW_COLOR_BUTTON_MINIMIZE);
        /* Draw horizontal line */
        fb_draw_hline(btn_x + 3, btn_y + WINDOW_BUTTON_SIZE / 2, WINDOW_BUTTON_SIZE - 6, 0xFF000000);
    }
}

static void wm_blit_content(window_t *win) {
    if (!win || !win->content_buffer) {
        return;
    }

    uint32_t *src = win->content_buffer;

    for (int row = 0; row < win->height; row++) {
        int dst_y = win->y + row;
        if (dst_y < 0 || dst_y >= (int)g_wm.screen_height) {
            continue;
        }

        for (int col = 0; col < win->width; col++) {
            int dst_x = win->x + col;
            if (dst_x < 0 || dst_x >= (int)g_wm.screen_width) {
                continue;
            }

            uint32_t pixel = src[row * win->width + col];

            /* Handle transparency if enabled */
            if (win->flags & WINDOW_TRANSPARENT) {
                uint8_t alpha = FB_GET_ALPHA(pixel);
                if (alpha == 0) {
                    continue;  /* Fully transparent */
                } else if (alpha < 255) {
                    /* Alpha blend with existing pixel */
                    uint32_t bg = fb_get_pixel(dst_x, dst_y);
                    uint8_t inv_a = 255 - alpha;
                    uint8_t r = (FB_GET_RED(pixel) * alpha + FB_GET_RED(bg) * inv_a) / 255;
                    uint8_t g = (FB_GET_GREEN(pixel) * alpha + FB_GET_GREEN(bg) * inv_a) / 255;
                    uint8_t b = (FB_GET_BLUE(pixel) * alpha + FB_GET_BLUE(bg) * inv_a) / 255;
                    pixel = FB_MAKE_COLOR(255, r, g, b);
                }
            }

            fb_put_pixel(dst_x, dst_y, pixel);
        }
    }
}

static void wm_send_event(window_t *win, window_event_type_t type) {
    if (!win || !win->event_handler) {
        return;
    }

    window_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.window = win;

    win->event_handler(&event);
}

static void wm_handle_drag(int x, int y) {
    if (!g_wm.dragging_window) {
        return;
    }

    window_t *win = g_wm.dragging_window;

    int dx = x - g_wm.drag_start_x;
    int dy = y - g_wm.drag_start_y;

    int new_x = g_wm.drag_window_x + dx;
    int new_y = g_wm.drag_window_y + dy;

    /* Ensure title bar stays on screen */
    int title_h = (win->flags & WINDOW_HAS_TITLEBAR) ? WINDOW_TITLEBAR_HEIGHT : 0;
    if (new_y < title_h) {
        new_y = title_h;
    }

    win->x = new_x;
    win->y = new_y;
    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;
}

static void wm_handle_resize(int x, int y) {
    if (!g_wm.resizing_window) {
        return;
    }

    window_t *win = g_wm.resizing_window;

    int dx = x - g_wm.drag_start_x;
    int dy = y - g_wm.drag_start_y;

    int new_x = win->x;
    int new_y = win->y;
    int new_w = win->width;
    int new_h = win->height;

    switch (g_wm.resize_edge) {
        case WINDOW_HIT_BORDER_LEFT:
            new_x = g_wm.drag_window_x + dx;
            new_w = g_wm.drag_window_width - dx;
            break;

        case WINDOW_HIT_BORDER_RIGHT:
            new_w = g_wm.drag_window_width + dx;
            break;

        case WINDOW_HIT_BORDER_BOTTOM:
            new_h = g_wm.drag_window_height + dy;
            break;

        case WINDOW_HIT_CORNER_TL:
            new_x = g_wm.drag_window_x + dx;
            new_y = g_wm.drag_window_y + dy;
            new_w = g_wm.drag_window_width - dx;
            new_h = g_wm.drag_window_height - dy;
            break;

        case WINDOW_HIT_CORNER_TR:
            new_y = g_wm.drag_window_y + dy;
            new_w = g_wm.drag_window_width + dx;
            new_h = g_wm.drag_window_height - dy;
            break;

        case WINDOW_HIT_CORNER_BL:
            new_x = g_wm.drag_window_x + dx;
            new_w = g_wm.drag_window_width - dx;
            new_h = g_wm.drag_window_height + dy;
            break;

        case WINDOW_HIT_CORNER_BR:
            new_w = g_wm.drag_window_width + dx;
            new_h = g_wm.drag_window_height + dy;
            break;

        default:
            break;
    }

    /* Enforce minimum size */
    if (new_w < WINDOW_MIN_WIDTH) {
        if (g_wm.resize_edge == WINDOW_HIT_BORDER_LEFT ||
            g_wm.resize_edge == WINDOW_HIT_CORNER_TL ||
            g_wm.resize_edge == WINDOW_HIT_CORNER_BL) {
            new_x = win->x + win->width - WINDOW_MIN_WIDTH;
        }
        new_w = WINDOW_MIN_WIDTH;
    }
    if (new_h < WINDOW_MIN_HEIGHT) {
        if (g_wm.resize_edge == WINDOW_HIT_CORNER_TL ||
            g_wm.resize_edge == WINDOW_HIT_CORNER_TR) {
            new_y = win->y + win->height - WINDOW_MIN_HEIGHT;
        }
        new_h = WINDOW_MIN_HEIGHT;
    }

    /* Apply changes */
    if (new_x != win->x || new_y != win->y) {
        win->x = new_x;
        win->y = new_y;
    }
    if (new_w != win->width || new_h != win->height) {
        window_resize(win, new_w, new_h);
    }

    win->flags |= WINDOW_DIRTY;
    g_wm.needs_redraw = true;
}

static void wm_start_drag(window_t *win, int x, int y) {
    g_wm.dragging_window = win;
    g_wm.drag_start_x = x;
    g_wm.drag_start_y = y;
    g_wm.drag_window_x = win->x;
    g_wm.drag_window_y = win->y;
}

static void wm_start_resize(window_t *win, int x, int y, window_hit_region_t edge) {
    g_wm.resizing_window = win;
    g_wm.resize_edge = edge;
    g_wm.drag_start_x = x;
    g_wm.drag_start_y = y;
    g_wm.drag_window_x = win->x;
    g_wm.drag_window_y = win->y;
    g_wm.drag_window_width = win->width;
    g_wm.drag_window_height = win->height;
}

static void wm_end_drag(void) {
    if (g_wm.dragging_window) {
        wm_send_event(g_wm.dragging_window, WINDOW_EVENT_MOVE);
        g_wm.dragging_window = NULL;
    }
}

static void wm_end_resize(void) {
    if (g_wm.resizing_window) {
        wm_send_event(g_wm.resizing_window, WINDOW_EVENT_RESIZE);
        g_wm.resizing_window = NULL;
    }
}

static void wm_clamp_to_screen(window_t *win) {
    if (!win) {
        return;
    }

    int frame_x, frame_y, frame_w, frame_h;
    window_get_frame_bounds(win, &frame_x, &frame_y, &frame_w, &frame_h);

    /* Ensure at least part of title bar is visible */
    int min_visible = 50;

    if (frame_x + frame_w < min_visible) {
        win->x = min_visible - frame_w + (win->x - frame_x);
    }
    if (frame_x > (int)g_wm.screen_width - min_visible) {
        win->x = (int)g_wm.screen_width - min_visible + (win->x - frame_x);
    }
    if (frame_y < 0) {
        win->y = (win->y - frame_y);
    }
    if (frame_y > (int)g_wm.screen_height - min_visible) {
        win->y = (int)g_wm.screen_height - min_visible + (win->y - frame_y);
    }
}

static int wm_get_decoration_height(window_t *win) {
    int h = 0;
    if (win->flags & WINDOW_HAS_TITLEBAR) {
        h += WINDOW_TITLEBAR_HEIGHT;
    }
    if (win->flags & WINDOW_HAS_BORDER) {
        h += WINDOW_BORDER_WIDTH;  /* Bottom border only (top is title bar) */
    }
    return h;
}

static int wm_get_decoration_width(window_t *win) {
    int w = 0;
    if (win->flags & WINDOW_HAS_BORDER) {
        w += 2 * WINDOW_BORDER_WIDTH;  /* Left and right borders */
    }
    return w;
}
