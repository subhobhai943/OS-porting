/**
 * AAAos GUI - Window Compositor Implementation
 *
 * Manages multiple windows with Z-ordering, double buffering,
 * and compositing all windows to the framebuffer.
 */

#include "compositor.h"
#include "../../drivers/video/framebuffer.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/* Global compositor state */
static compositor_t g_compositor;

/* Forward declarations for internal functions */
static void compositor_draw_desktop_background(void);
static void compositor_draw_window(window_t *win);
static void compositor_draw_window_decorations(window_t *win);
static void compositor_blit_window_buffer(window_t *win);
static void compositor_swap_buffers(void);
static void compositor_unlink_window(window_t *win);
static void compositor_link_window(window_t *win);
static void compositor_reorder_windows(void);
static uint32_t alpha_blend(uint32_t fg, uint32_t bg);

/**
 * Initialize the compositor
 */
int compositor_init(uint32_t width, uint32_t height) {
    kprintf("[COMPOSITOR] Initializing compositor (%u x %u)\n", width, height);

    if (g_compositor.initialized) {
        kprintf("[COMPOSITOR] Warning: Already initialized, reinitializing\n");
        compositor_shutdown();
    }

    /* Clear compositor state */
    memset(&g_compositor, 0, sizeof(compositor_t));

    g_compositor.screen_width = width;
    g_compositor.screen_height = height;

    /* Allocate back buffer for double buffering */
    size_t buffer_size = width * height * sizeof(uint32_t);
    g_compositor.back_buffer = (uint32_t *)kmalloc(buffer_size);
    if (!g_compositor.back_buffer) {
        kprintf("[COMPOSITOR] Error: Failed to allocate back buffer (%u bytes)\n",
                (uint32_t)buffer_size);
        return -1;
    }
    kprintf("[COMPOSITOR] Allocated back buffer at %p (%u bytes)\n",
            g_compositor.back_buffer, (uint32_t)buffer_size);

    /* Get framebuffer address */
    const framebuffer_t *fb = fb_get_info();
    if (!fb || !fb->initialized) {
        kprintf("[COMPOSITOR] Error: Framebuffer not initialized\n");
        kfree(g_compositor.back_buffer);
        g_compositor.back_buffer = NULL;
        return -2;
    }
    g_compositor.front_buffer = fb->address;

    /* Initialize state */
    g_compositor.window_list = NULL;
    g_compositor.window_tail = NULL;
    g_compositor.active_window = NULL;
    g_compositor.window_count = 0;
    g_compositor.next_window_id = 1;
    g_compositor.next_z_order = 0;
    g_compositor.needs_full_redraw = true;
    g_compositor.initialized = true;

    /* Clear dirty region */
    g_compositor.dirty_region.valid = false;

    kprintf("[COMPOSITOR] Initialization complete\n");
    return 0;
}

/**
 * Shutdown the compositor
 */
void compositor_shutdown(void) {
    kprintf("[COMPOSITOR] Shutting down compositor\n");

    if (!g_compositor.initialized) {
        return;
    }

    /* Destroy all windows */
    window_t *win = g_compositor.window_list;
    while (win) {
        window_t *next = win->next;
        if (win->buffer) {
            kfree(win->buffer);
        }
        kfree(win);
        win = next;
    }

    /* Free back buffer */
    if (g_compositor.back_buffer) {
        kfree(g_compositor.back_buffer);
        g_compositor.back_buffer = NULL;
    }

    memset(&g_compositor, 0, sizeof(compositor_t));
    kprintf("[COMPOSITOR] Shutdown complete\n");
}

/**
 * Create a new window
 */
window_t *compositor_create_window(int x, int y, int w, int h, const char *title) {
    if (!g_compositor.initialized) {
        kprintf("[COMPOSITOR] Error: Compositor not initialized\n");
        return NULL;
    }

    if (g_compositor.window_count >= COMPOSITOR_MAX_WINDOWS) {
        kprintf("[COMPOSITOR] Error: Maximum window count reached\n");
        return NULL;
    }

    if (w <= 0 || h <= 0) {
        kprintf("[COMPOSITOR] Error: Invalid window dimensions (%d x %d)\n", w, h);
        return NULL;
    }

    /* Allocate window structure */
    window_t *win = (window_t *)kmalloc(sizeof(window_t));
    if (!win) {
        kprintf("[COMPOSITOR] Error: Failed to allocate window structure\n");
        return NULL;
    }
    memset(win, 0, sizeof(window_t));

    /* Allocate window buffer */
    size_t buffer_size = (size_t)w * h * sizeof(uint32_t);
    win->buffer = (uint32_t *)kmalloc(buffer_size);
    if (!win->buffer) {
        kprintf("[COMPOSITOR] Error: Failed to allocate window buffer (%u bytes)\n",
                (uint32_t)buffer_size);
        kfree(win);
        return NULL;
    }

    /* Clear buffer to white */
    for (int i = 0; i < w * h; i++) {
        win->buffer[i] = 0xFFFFFFFF;
    }

    /* Initialize window properties */
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->z_order = g_compositor.next_z_order++;
    win->flags = WINDOW_FLAGS_DEFAULT;
    win->id = g_compositor.next_window_id++;

    /* Set title */
    if (title) {
        strncpy(win->title, title, WINDOW_TITLE_MAX_LEN - 1);
        win->title[WINDOW_TITLE_MAX_LEN - 1] = '\0';
    } else {
        win->title[0] = '\0';
    }

    /* Save initial position for restore */
    win->saved_x = x;
    win->saved_y = y;
    win->saved_width = w;
    win->saved_height = h;

    /* Link window into list (at end = top of Z-order) */
    compositor_link_window(win);

    /* Set as active window */
    compositor_set_active_window(win);

    /* Mark screen dirty */
    compositor_invalidate_all();

    g_compositor.window_count++;
    g_compositor.windows_created++;

    kprintf("[COMPOSITOR] Created window %u: \"%s\" at (%d, %d) size %d x %d\n",
            win->id, title ? title : "(untitled)", x, y, w, h);

    return win;
}

/**
 * Destroy a window
 */
void compositor_destroy_window(window_t *win) {
    if (!win) {
        return;
    }

    kprintf("[COMPOSITOR] Destroying window %u: \"%s\"\n", win->id, win->title);

    /* Mark dirty region where window was */
    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);
    compositor_invalidate(win->x, win->y, total_w, total_h);

    /* Unlink from list */
    compositor_unlink_window(win);

    /* If this was the active window, activate the next one */
    if (g_compositor.active_window == win) {
        g_compositor.active_window = g_compositor.window_tail;
        if (g_compositor.active_window) {
            g_compositor.active_window->flags |= WINDOW_FLAG_ACTIVE;
        }
    }

    /* Free resources */
    if (win->buffer) {
        kfree(win->buffer);
    }
    kfree(win);

    g_compositor.window_count--;
    g_compositor.windows_destroyed++;
}

/**
 * Move a window
 */
void compositor_move_window(window_t *win, int x, int y) {
    if (!win) {
        return;
    }

    if (!(win->flags & WINDOW_FLAG_MOVABLE)) {
        kprintf("[COMPOSITOR] Warning: Window %u is not movable\n", win->id);
        return;
    }

    /* Mark old position dirty */
    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);
    compositor_invalidate(win->x, win->y, total_w, total_h);

    /* Update position */
    win->x = x;
    win->y = y;

    /* Mark new position dirty */
    compositor_invalidate(win->x, win->y, total_w, total_h);
}

/**
 * Resize a window
 */
int compositor_resize_window(window_t *win, int w, int h) {
    if (!win) {
        return -1;
    }

    if (!(win->flags & WINDOW_FLAG_RESIZABLE)) {
        kprintf("[COMPOSITOR] Warning: Window %u is not resizable\n", win->id);
        return -2;
    }

    if (w <= 0 || h <= 0) {
        kprintf("[COMPOSITOR] Error: Invalid resize dimensions (%d x %d)\n", w, h);
        return -3;
    }

    /* Mark old area dirty */
    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);
    compositor_invalidate(win->x, win->y, total_w, total_h);

    /* Allocate new buffer */
    size_t new_size = (size_t)w * h * sizeof(uint32_t);
    uint32_t *new_buffer = (uint32_t *)kmalloc(new_size);
    if (!new_buffer) {
        kprintf("[COMPOSITOR] Error: Failed to allocate resize buffer\n");
        return -4;
    }

    /* Clear new buffer */
    for (int i = 0; i < w * h; i++) {
        new_buffer[i] = 0xFFFFFFFF;
    }

    /* Copy old content (as much as fits) */
    int copy_w = MIN(win->width, w);
    int copy_h = MIN(win->height, h);
    for (int row = 0; row < copy_h; row++) {
        memcpy(&new_buffer[row * w],
               &win->buffer[row * win->width],
               copy_w * sizeof(uint32_t));
    }

    /* Free old buffer and update */
    kfree(win->buffer);
    win->buffer = new_buffer;
    win->width = w;
    win->height = h;

    /* Mark new area dirty */
    compositor_get_window_total_size(win, &total_w, &total_h);
    compositor_invalidate(win->x, win->y, total_w, total_h);

    kprintf("[COMPOSITOR] Resized window %u to %d x %d\n", win->id, w, h);
    return 0;
}

/**
 * Bring window to front
 */
void compositor_raise_window(window_t *win) {
    if (!win) {
        return;
    }

    /* Already at front? */
    if (win == g_compositor.window_tail) {
        return;
    }

    /* Unlink and re-link at end */
    compositor_unlink_window(win);
    win->z_order = g_compositor.next_z_order++;
    compositor_link_window(win);

    /* Set as active */
    compositor_set_active_window(win);

    /* Mark window area dirty */
    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);
    compositor_invalidate(win->x, win->y, total_w, total_h);

    kprintf("[COMPOSITOR] Raised window %u to front (z=%d)\n", win->id, win->z_order);
}

/**
 * Send window to back
 */
void compositor_lower_window(window_t *win) {
    if (!win) {
        return;
    }

    /* Already at back? */
    if (win == g_compositor.window_list) {
        return;
    }

    /* Unlink from current position */
    compositor_unlink_window(win);

    /* Set lowest Z-order */
    window_t *lowest = g_compositor.window_list;
    win->z_order = lowest ? lowest->z_order - 1 : 0;

    /* Link at front of list */
    win->prev = NULL;
    win->next = g_compositor.window_list;
    if (g_compositor.window_list) {
        g_compositor.window_list->prev = win;
    }
    g_compositor.window_list = win;
    if (!g_compositor.window_tail) {
        g_compositor.window_tail = win;
    }

    /* Mark dirty */
    compositor_invalidate_all();

    kprintf("[COMPOSITOR] Lowered window %u to back (z=%d)\n", win->id, win->z_order);
}

/**
 * Set active window
 */
void compositor_set_active_window(window_t *win) {
    /* Deactivate current active window */
    if (g_compositor.active_window) {
        g_compositor.active_window->flags &= ~WINDOW_FLAG_ACTIVE;

        /* Mark old active window dirty (title bar changed) */
        int total_w, total_h;
        compositor_get_window_total_size(g_compositor.active_window, &total_w, &total_h);
        compositor_invalidate(g_compositor.active_window->x,
                             g_compositor.active_window->y,
                             total_w, total_h);
    }

    /* Activate new window */
    g_compositor.active_window = win;
    if (win) {
        win->flags |= WINDOW_FLAG_ACTIVE;

        /* Mark new active window dirty */
        int total_w, total_h;
        compositor_get_window_total_size(win, &total_w, &total_h);
        compositor_invalidate(win->x, win->y, total_w, total_h);
    }
}

/**
 * Get active window
 */
window_t *compositor_get_active_window(void) {
    return g_compositor.active_window;
}

/**
 * Render all windows to screen
 */
void compositor_render(void) {
    if (!g_compositor.initialized) {
        return;
    }

    /* Draw desktop background */
    compositor_draw_desktop_background();

    /* Draw windows from back to front (lowest z_order first) */
    window_t *win = g_compositor.window_list;
    while (win) {
        if (win->flags & WINDOW_FLAG_VISIBLE && !(win->flags & WINDOW_FLAG_MINIMIZED)) {
            compositor_draw_window(win);
        }
        win = win->next;
    }

    /* Swap buffers (copy back buffer to front buffer) */
    compositor_swap_buffers();

    /* Clear dirty region */
    g_compositor.dirty_region.valid = false;
    g_compositor.needs_full_redraw = false;

    g_compositor.frame_count++;
}

/**
 * Mark region dirty
 */
void compositor_invalidate(int x, int y, int w, int h) {
    if (!g_compositor.initialized) {
        return;
    }

    /* Clip to screen bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g_compositor.screen_width) {
        w = (int)g_compositor.screen_width - x;
    }
    if (y + h > (int)g_compositor.screen_height) {
        h = (int)g_compositor.screen_height - y;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    /* Merge with existing dirty region */
    if (g_compositor.dirty_region.valid) {
        int x1 = MIN(g_compositor.dirty_region.x, x);
        int y1 = MIN(g_compositor.dirty_region.y, y);
        int x2 = MAX(g_compositor.dirty_region.x + g_compositor.dirty_region.width, x + w);
        int y2 = MAX(g_compositor.dirty_region.y + g_compositor.dirty_region.height, y + h);
        g_compositor.dirty_region.x = x1;
        g_compositor.dirty_region.y = y1;
        g_compositor.dirty_region.width = x2 - x1;
        g_compositor.dirty_region.height = y2 - y1;
    } else {
        g_compositor.dirty_region.x = x;
        g_compositor.dirty_region.y = y;
        g_compositor.dirty_region.width = w;
        g_compositor.dirty_region.height = h;
        g_compositor.dirty_region.valid = true;
    }
}

/**
 * Mark entire screen dirty
 */
void compositor_invalidate_all(void) {
    g_compositor.needs_full_redraw = true;
    g_compositor.dirty_region.x = 0;
    g_compositor.dirty_region.y = 0;
    g_compositor.dirty_region.width = g_compositor.screen_width;
    g_compositor.dirty_region.height = g_compositor.screen_height;
    g_compositor.dirty_region.valid = true;
}

/**
 * Find window at screen coordinates
 */
window_t *compositor_find_window_at(int x, int y) {
    /* Search from front to back (highest z_order first) */
    window_t *win = g_compositor.window_tail;
    while (win) {
        if (!(win->flags & WINDOW_FLAG_VISIBLE) || (win->flags & WINDOW_FLAG_MINIMIZED)) {
            win = win->prev;
            continue;
        }

        int total_w, total_h;
        compositor_get_window_total_size(win, &total_w, &total_h);

        int win_x = win->x;
        int win_y = win->y;
        if (win->flags & WINDOW_FLAG_DECORATED) {
            win_y -= WINDOW_TITLE_BAR_HEIGHT;
        }

        if (x >= win_x && x < win_x + total_w &&
            y >= win_y && y < win_y + total_h) {
            return win;
        }
        win = win->prev;
    }
    return NULL;
}

/**
 * Get window by ID
 */
window_t *compositor_get_window_by_id(uint32_t id) {
    window_t *win = g_compositor.window_list;
    while (win) {
        if (win->id == id) {
            return win;
        }
        win = win->next;
    }
    return NULL;
}

/**
 * Show or hide window
 */
void compositor_set_window_visible(window_t *win, bool visible) {
    if (!win) {
        return;
    }

    bool was_visible = (win->flags & WINDOW_FLAG_VISIBLE) != 0;
    if (visible == was_visible) {
        return;
    }

    if (visible) {
        win->flags |= WINDOW_FLAG_VISIBLE;
    } else {
        win->flags &= ~WINDOW_FLAG_VISIBLE;
    }

    /* Mark window area dirty */
    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);
    int win_y = win->y;
    if (win->flags & WINDOW_FLAG_DECORATED) {
        win_y -= WINDOW_TITLE_BAR_HEIGHT;
    }
    compositor_invalidate(win->x, win_y, total_w, total_h);
}

/**
 * Minimize window
 */
void compositor_minimize_window(window_t *win) {
    if (!win || (win->flags & WINDOW_FLAG_MINIMIZED)) {
        return;
    }

    win->flags |= WINDOW_FLAG_MINIMIZED;

    /* If this was active, activate next window */
    if (g_compositor.active_window == win) {
        /* Find next visible window */
        window_t *next = win->prev;
        while (next && ((next->flags & WINDOW_FLAG_MINIMIZED) ||
               !(next->flags & WINDOW_FLAG_VISIBLE))) {
            next = next->prev;
        }
        compositor_set_active_window(next);
    }

    compositor_invalidate_all();
    kprintf("[COMPOSITOR] Minimized window %u\n", win->id);
}

/**
 * Maximize window
 */
void compositor_maximize_window(window_t *win) {
    if (!win || (win->flags & WINDOW_FLAG_MAXIMIZED)) {
        return;
    }

    /* Save current position/size */
    if (!(win->flags & WINDOW_FLAG_MAXIMIZED)) {
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_width = win->width;
        win->saved_height = win->height;
    }

    win->flags |= WINDOW_FLAG_MAXIMIZED;
    win->flags &= ~WINDOW_FLAG_MINIMIZED;

    /* Move to top-left and resize to screen */
    int new_height = g_compositor.screen_height;
    if (win->flags & WINDOW_FLAG_DECORATED) {
        new_height -= WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_WIDTH;
    }

    win->x = 0;
    win->y = (win->flags & WINDOW_FLAG_DECORATED) ? WINDOW_TITLE_BAR_HEIGHT : 0;
    compositor_resize_window(win, g_compositor.screen_width - 2 * WINDOW_BORDER_WIDTH,
                            new_height);

    compositor_invalidate_all();
    kprintf("[COMPOSITOR] Maximized window %u\n", win->id);
}

/**
 * Restore window
 */
void compositor_restore_window(window_t *win) {
    if (!win) {
        return;
    }

    if (win->flags & WINDOW_FLAG_MINIMIZED) {
        win->flags &= ~WINDOW_FLAG_MINIMIZED;
        compositor_raise_window(win);
    }

    if (win->flags & WINDOW_FLAG_MAXIMIZED) {
        win->flags &= ~WINDOW_FLAG_MAXIMIZED;
        win->x = win->saved_x;
        win->y = win->saved_y;
        compositor_resize_window(win, win->saved_width, win->saved_height);
    }

    compositor_invalidate_all();
    kprintf("[COMPOSITOR] Restored window %u\n", win->id);
}

/**
 * Get compositor state
 */
const compositor_t *compositor_get_state(void) {
    return &g_compositor;
}

/**
 * Check if compositor is initialized
 */
bool compositor_is_initialized(void) {
    return g_compositor.initialized;
}

/**
 * Get total window size including decorations
 */
void compositor_get_window_total_size(window_t *win, int *total_width, int *total_height) {
    if (!win) {
        *total_width = 0;
        *total_height = 0;
        return;
    }

    *total_width = win->width;
    *total_height = win->height;

    if (win->flags & WINDOW_FLAG_DECORATED) {
        *total_width += 2 * WINDOW_BORDER_WIDTH;
        *total_height += WINDOW_TITLE_BAR_HEIGHT + WINDOW_BORDER_WIDTH;
    }
}

/**
 * Get window content area position
 */
void compositor_get_window_content_pos(window_t *win, int *content_x, int *content_y) {
    if (!win) {
        *content_x = 0;
        *content_y = 0;
        return;
    }

    *content_x = win->x;
    *content_y = win->y;

    if (win->flags & WINDOW_FLAG_DECORATED) {
        *content_x += WINDOW_BORDER_WIDTH;
    }
}

/* ============================================================================
 * Internal Functions
 * ============================================================================ */

/**
 * Draw desktop background
 */
static void compositor_draw_desktop_background(void) {
    uint32_t *buffer = g_compositor.back_buffer;
    uint32_t width = g_compositor.screen_width;
    uint32_t height = g_compositor.screen_height;

    /* Fill with solid color for now */
    for (uint32_t i = 0; i < width * height; i++) {
        buffer[i] = DESKTOP_BACKGROUND_COLOR;
    }
}

/**
 * Draw a single window (decorations + content)
 */
static void compositor_draw_window(window_t *win) {
    if (!win) {
        return;
    }

    /* Draw decorations first (below content) */
    if (win->flags & WINDOW_FLAG_DECORATED) {
        compositor_draw_window_decorations(win);
    }

    /* Then blit window buffer */
    compositor_blit_window_buffer(win);
}

/**
 * Draw window decorations (title bar, borders)
 */
static void compositor_draw_window_decorations(window_t *win) {
    if (!win || !(win->flags & WINDOW_FLAG_DECORATED)) {
        return;
    }

    uint32_t *buffer = g_compositor.back_buffer;
    int screen_w = (int)g_compositor.screen_width;
    int screen_h = (int)g_compositor.screen_height;

    int total_w, total_h;
    compositor_get_window_total_size(win, &total_w, &total_h);

    /* Calculate title bar position */
    int title_x = win->x;
    int title_y = win->y - WINDOW_TITLE_BAR_HEIGHT;
    int title_w = total_w;
    int title_h = WINDOW_TITLE_BAR_HEIGHT;

    /* Title bar color depends on active state */
    uint32_t title_color = (win->flags & WINDOW_FLAG_ACTIVE) ?
                           WINDOW_TITLE_BAR_ACTIVE : WINDOW_TITLE_BAR_INACTIVE;
    uint32_t border_color = (win->flags & WINDOW_FLAG_ACTIVE) ?
                            WINDOW_BORDER_ACTIVE : WINDOW_BORDER_COLOR;

    /* Draw title bar */
    for (int y = title_y; y < title_y + title_h; y++) {
        if (y < 0 || y >= screen_h) continue;
        for (int x = title_x; x < title_x + title_w; x++) {
            if (x < 0 || x >= screen_w) continue;
            buffer[y * screen_w + x] = title_color;
        }
    }

    /* Draw title text */
    if (win->title[0] != '\0') {
        int text_x = title_x + WINDOW_BUTTON_PADDING + 4;
        int text_y = title_y + (WINDOW_TITLE_BAR_HEIGHT - 16) / 2;

        /* Simple text rendering - each character is 8 pixels wide */
        for (int i = 0; win->title[i] && text_x < title_x + title_w - 60; i++) {
            /* Draw character placeholder (actual font rendering would go here) */
            for (int cy = 0; cy < 12 && text_y + cy < screen_h; cy++) {
                if (text_y + cy < 0) continue;
                for (int cx = 0; cx < 7 && text_x + cx < screen_w; cx++) {
                    if (text_x + cx < 0) continue;
                    /* Simple character representation */
                    buffer[(text_y + cy) * screen_w + (text_x + cx)] = WINDOW_TITLE_TEXT_COLOR;
                }
            }
            text_x += 8;
        }
    }

    /* Draw window close button (red X in top-right) */
    int btn_x = title_x + title_w - WINDOW_BUTTON_SIZE - WINDOW_BUTTON_PADDING;
    int btn_y = title_y + (WINDOW_TITLE_BAR_HEIGHT - WINDOW_BUTTON_SIZE) / 2;

    for (int y = btn_y; y < btn_y + WINDOW_BUTTON_SIZE && y < screen_h; y++) {
        if (y < 0) continue;
        for (int x = btn_x; x < btn_x + WINDOW_BUTTON_SIZE && x < screen_w; x++) {
            if (x < 0) continue;
            buffer[y * screen_w + x] = 0xFFFF4444;  /* Red */
        }
    }

    /* Draw left border */
    for (int y = win->y; y < win->y + win->height + WINDOW_BORDER_WIDTH && y < screen_h; y++) {
        if (y < 0) continue;
        for (int x = win->x - WINDOW_BORDER_WIDTH; x < win->x && x < screen_w; x++) {
            if (x < 0) continue;
            buffer[y * screen_w + x] = border_color;
        }
    }

    /* Draw right border */
    for (int y = win->y; y < win->y + win->height + WINDOW_BORDER_WIDTH && y < screen_h; y++) {
        if (y < 0) continue;
        for (int x = win->x + win->width; x < win->x + win->width + WINDOW_BORDER_WIDTH && x < screen_w; x++) {
            if (x < 0) continue;
            buffer[y * screen_w + x] = border_color;
        }
    }

    /* Draw bottom border */
    for (int y = win->y + win->height; y < win->y + win->height + WINDOW_BORDER_WIDTH && y < screen_h; y++) {
        if (y < 0) continue;
        for (int x = win->x - WINDOW_BORDER_WIDTH; x < win->x + win->width + WINDOW_BORDER_WIDTH && x < screen_w; x++) {
            if (x < 0) continue;
            buffer[y * screen_w + x] = border_color;
        }
    }
}

/**
 * Blit window buffer to back buffer with optional alpha blending
 */
static void compositor_blit_window_buffer(window_t *win) {
    if (!win || !win->buffer) {
        return;
    }

    uint32_t *dst = g_compositor.back_buffer;
    uint32_t *src = win->buffer;
    int screen_w = (int)g_compositor.screen_width;
    int screen_h = (int)g_compositor.screen_height;

    bool use_alpha = (win->flags & WINDOW_FLAG_TRANSPARENT) != 0;

    for (int row = 0; row < win->height; row++) {
        int dst_y = win->y + row;
        if (dst_y < 0 || dst_y >= screen_h) continue;

        for (int col = 0; col < win->width; col++) {
            int dst_x = win->x + col;
            if (dst_x < 0 || dst_x >= screen_w) continue;

            uint32_t pixel = src[row * win->width + col];
            int dst_idx = dst_y * screen_w + dst_x;

            if (use_alpha) {
                uint8_t alpha = FB_GET_ALPHA(pixel);
                if (alpha == 255) {
                    dst[dst_idx] = pixel;
                } else if (alpha > 0) {
                    dst[dst_idx] = alpha_blend(pixel, dst[dst_idx]);
                }
                /* alpha == 0: fully transparent, don't draw */
            } else {
                dst[dst_idx] = pixel;
            }
        }
    }
}

/**
 * Swap back buffer to front buffer
 */
static void compositor_swap_buffers(void) {
    if (!g_compositor.back_buffer || !g_compositor.front_buffer) {
        return;
    }

    /* Copy entire back buffer to front buffer */
    size_t buffer_size = g_compositor.screen_width * g_compositor.screen_height * sizeof(uint32_t);
    memcpy(g_compositor.front_buffer, g_compositor.back_buffer, buffer_size);
}

/**
 * Alpha blend foreground over background
 * Returns blended color
 */
static uint32_t alpha_blend(uint32_t fg, uint32_t bg) {
    uint8_t fg_a = FB_GET_ALPHA(fg);
    uint8_t fg_r = FB_GET_RED(fg);
    uint8_t fg_g = FB_GET_GREEN(fg);
    uint8_t fg_b = FB_GET_BLUE(fg);

    uint8_t bg_r = FB_GET_RED(bg);
    uint8_t bg_g = FB_GET_GREEN(bg);
    uint8_t bg_b = FB_GET_BLUE(bg);

    /* Standard alpha blending: out = fg * alpha + bg * (1 - alpha) */
    uint8_t inv_a = 255 - fg_a;
    uint8_t out_r = (fg_r * fg_a + bg_r * inv_a) / 255;
    uint8_t out_g = (fg_g * fg_a + bg_g * inv_a) / 255;
    uint8_t out_b = (fg_b * fg_a + bg_b * inv_a) / 255;

    return FB_MAKE_COLOR(255, out_r, out_g, out_b);
}

/**
 * Unlink window from list
 */
static void compositor_unlink_window(window_t *win) {
    if (!win) {
        return;
    }

    if (win->prev) {
        win->prev->next = win->next;
    } else {
        g_compositor.window_list = win->next;
    }

    if (win->next) {
        win->next->prev = win->prev;
    } else {
        g_compositor.window_tail = win->prev;
    }

    win->prev = NULL;
    win->next = NULL;
}

/**
 * Link window into list (at end, highest Z-order)
 */
static void compositor_link_window(window_t *win) {
    if (!win) {
        return;
    }

    win->prev = g_compositor.window_tail;
    win->next = NULL;

    if (g_compositor.window_tail) {
        g_compositor.window_tail->next = win;
    } else {
        g_compositor.window_list = win;
    }

    g_compositor.window_tail = win;
}

/**
 * Reorder windows by Z-order (not currently used, but available if needed)
 */
static void compositor_reorder_windows(void) {
    /* Simple bubble sort by z_order */
    if (!g_compositor.window_list || !g_compositor.window_list->next) {
        return;
    }

    bool swapped;
    do {
        swapped = false;
        window_t *win = g_compositor.window_list;
        while (win && win->next) {
            if (win->z_order > win->next->z_order) {
                /* Swap z_orders (simpler than relinking) */
                int32_t tmp = win->z_order;
                win->z_order = win->next->z_order;
                win->next->z_order = tmp;

                /* Actually swap window positions in list */
                window_t *a = win;
                window_t *b = win->next;

                if (a->prev) a->prev->next = b;
                else g_compositor.window_list = b;

                if (b->next) b->next->prev = a;
                else g_compositor.window_tail = a;

                a->next = b->next;
                b->prev = a->prev;
                a->prev = b;
                b->next = a;

                swapped = true;
            } else {
                win = win->next;
            }
        }
    } while (swapped);

    UNUSED(compositor_reorder_windows);  /* Silence unused warning */
}
