/**
 * AAAos GUI - Base Widget System Implementation
 *
 * Implements the foundational widget structure and common functions
 * for the AAAos graphical user interface toolkit.
 */

#include "widget.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"

/* Global widget ID counter */
static uint32_t g_widget_id_counter = 0;

/* Currently focused widget */
static widget_t *g_focused_widget = NULL;

/**
 * Initialize a widget with default values
 */
void widget_init(widget_t *w, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!w) {
        kprintf("[WIDGET] ERROR: widget_init called with NULL widget\n");
        return;
    }

    /* Clear the structure */
    memset(w, 0, sizeof(widget_t));

    /* Set geometry */
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;

    /* Set default flags */
    w->flags = WIDGET_FLAGS_DEFAULT;

    /* Assign unique ID */
    w->id = ++g_widget_id_counter;

    /* Initialize tree pointers */
    w->parent = NULL;
    w->child_count = 0;

    /* No handlers by default */
    w->on_click = NULL;
    w->on_key = NULL;
    w->on_paint = NULL;
    w->on_focus = NULL;
    w->on_mouse = NULL;
    w->draw = NULL;
    w->user_data = NULL;

    kprintf("[WIDGET] Initialized widget %u at (%d, %d) size %dx%d\n",
            w->id, x, y, width, height);
}

/**
 * Add a child widget to a parent
 */
bool widget_add_child(widget_t *parent, widget_t *child) {
    if (!parent || !child) {
        kprintf("[WIDGET] ERROR: widget_add_child called with NULL pointer\n");
        return false;
    }

    if (parent->child_count >= WIDGET_MAX_CHILDREN) {
        kprintf("[WIDGET] ERROR: Widget %u has reached maximum children (%d)\n",
                parent->id, WIDGET_MAX_CHILDREN);
        return false;
    }

    /* Remove from old parent if any */
    if (child->parent) {
        widget_remove_child(child->parent, child);
    }

    /* Add to new parent */
    parent->children[parent->child_count++] = child;
    child->parent = parent;

    kprintf("[WIDGET] Added widget %u as child of widget %u (child count: %u)\n",
            child->id, parent->id, parent->child_count);

    /* Mark parent as needing redraw */
    widget_invalidate(parent);

    return true;
}

/**
 * Remove a child widget from a parent
 */
bool widget_remove_child(widget_t *parent, widget_t *child) {
    if (!parent || !child) {
        kprintf("[WIDGET] ERROR: widget_remove_child called with NULL pointer\n");
        return false;
    }

    /* Find the child */
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Found it - shift remaining children down */
            for (uint32_t j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            child->parent = NULL;

            kprintf("[WIDGET] Removed widget %u from parent %u (child count: %u)\n",
                    child->id, parent->id, parent->child_count);

            /* Mark parent as needing redraw */
            widget_invalidate(parent);

            return true;
        }
    }

    kprintf("[WIDGET] WARNING: Widget %u not found in parent %u's children\n",
            child->id, parent->id);
    return false;
}

/**
 * Draw a widget and all its children
 */
void widget_draw(widget_t *w, void *buffer) {
    if (!w || !buffer) {
        return;
    }

    /* Don't draw if not visible */
    if (!(w->flags & WIDGET_FLAG_VISIBLE)) {
        return;
    }

    /* Draw this widget first */
    if (w->draw) {
        w->draw(w, buffer);
    }

    /* Then invoke paint handler if set */
    if (w->on_paint) {
        event_t paint_event;
        memset(&paint_event, 0, sizeof(event_t));
        paint_event.type = EVENT_PAINT;
        paint_event.target = w;
        paint_event.paint.buffer = buffer;
        paint_event.paint.clip_x = w->x;
        paint_event.paint.clip_y = w->y;
        paint_event.paint.clip_w = w->width;
        paint_event.paint.clip_h = w->height;
        w->on_paint(w, &paint_event);
    }

    /* Draw children (front to back) */
    for (uint32_t i = 0; i < w->child_count; i++) {
        widget_draw(w->children[i], buffer);
    }

    /* Clear dirty flag */
    w->flags &= ~WIDGET_FLAG_DIRTY;
}

/**
 * Handle an event for a widget
 */
bool widget_handle_event(widget_t *w, event_t *event) {
    if (!w || !event) {
        return false;
    }

    /* Don't handle events if widget is not visible or enabled */
    if (!(w->flags & WIDGET_FLAG_VISIBLE) || !(w->flags & WIDGET_FLAG_ENABLED)) {
        return false;
    }

    /* Handle different event types */
    switch (event->type) {
        case EVENT_MOUSE_CLICK:
            if (w->on_click) {
                w->on_click(w, event);
                if (event->handled) return true;
            }
            break;

        case EVENT_MOUSE_MOVE:
        case EVENT_MOUSE_DOWN:
        case EVENT_MOUSE_UP:
        case EVENT_MOUSE_ENTER:
        case EVENT_MOUSE_LEAVE:
            if (w->on_mouse) {
                w->on_mouse(w, event);
                if (event->handled) return true;
            }
            break;

        case EVENT_KEY_DOWN:
        case EVENT_KEY_UP:
        case EVENT_KEY_CHAR:
            /* Only handle key events if focused */
            if (w->flags & WIDGET_FLAG_FOCUSED) {
                if (w->on_key) {
                    w->on_key(w, event);
                    if (event->handled) return true;
                }
            }
            break;

        case EVENT_FOCUS_GAINED:
        case EVENT_FOCUS_LOST:
            if (w->on_focus) {
                w->on_focus(w, event);
                if (event->handled) return true;
            }
            break;

        case EVENT_PAINT:
            if (w->on_paint) {
                w->on_paint(w, event);
                if (event->handled) return true;
            }
            break;

        default:
            break;
    }

    /* Propagate to children for mouse events */
    if (event->type >= EVENT_MOUSE_MOVE && event->type <= EVENT_MOUSE_LEAVE) {
        /* Check children in reverse order (top to bottom) */
        for (int32_t i = (int32_t)w->child_count - 1; i >= 0; i--) {
            widget_t *child = w->children[i];
            if (widget_contains_point(child, event->mouse.global_x, event->mouse.global_y)) {
                if (widget_handle_event(child, event)) {
                    return true;
                }
            }
        }
    }

    return event->handled;
}

/**
 * Set keyboard focus to a widget
 */
void widget_set_focus(widget_t *w) {
    /* Remove focus from old widget */
    if (g_focused_widget && g_focused_widget != w) {
        g_focused_widget->flags &= ~WIDGET_FLAG_FOCUSED;

        /* Send focus lost event */
        event_t event;
        memset(&event, 0, sizeof(event_t));
        event.type = EVENT_FOCUS_LOST;
        event.target = g_focused_widget;
        if (g_focused_widget->on_focus) {
            g_focused_widget->on_focus(g_focused_widget, &event);
        }
        widget_invalidate(g_focused_widget);

        kprintf("[WIDGET] Widget %u lost focus\n", g_focused_widget->id);
    }

    g_focused_widget = w;

    /* Set focus on new widget */
    if (w) {
        w->flags |= WIDGET_FLAG_FOCUSED;

        /* Send focus gained event */
        event_t event;
        memset(&event, 0, sizeof(event_t));
        event.type = EVENT_FOCUS_GAINED;
        event.target = w;
        if (w->on_focus) {
            w->on_focus(w, &event);
        }
        widget_invalidate(w);

        kprintf("[WIDGET] Widget %u gained focus\n", w->id);
    }
}

/**
 * Get the currently focused widget
 */
widget_t *widget_get_focus(void) {
    return g_focused_widget;
}

/**
 * Set widget visibility
 */
void widget_set_visible(widget_t *w, bool visible) {
    if (!w) return;

    if (visible) {
        w->flags |= WIDGET_FLAG_VISIBLE;
    } else {
        w->flags &= ~WIDGET_FLAG_VISIBLE;
        /* Remove focus if hiding */
        if (w->flags & WIDGET_FLAG_FOCUSED) {
            widget_set_focus(NULL);
        }
    }
    widget_invalidate(w);

    kprintf("[WIDGET] Widget %u visibility set to %s\n",
            w->id, visible ? "true" : "false");
}

/**
 * Check if widget is visible
 */
bool widget_is_visible(widget_t *w) {
    if (!w) return false;
    return (w->flags & WIDGET_FLAG_VISIBLE) != 0;
}

/**
 * Set widget enabled state
 */
void widget_set_enabled(widget_t *w, bool enabled) {
    if (!w) return;

    if (enabled) {
        w->flags |= WIDGET_FLAG_ENABLED;
    } else {
        w->flags &= ~WIDGET_FLAG_ENABLED;
        /* Remove focus if disabling */
        if (w->flags & WIDGET_FLAG_FOCUSED) {
            widget_set_focus(NULL);
        }
    }
    widget_invalidate(w);

    kprintf("[WIDGET] Widget %u enabled set to %s\n",
            w->id, enabled ? "true" : "false");
}

/**
 * Check if widget is enabled
 */
bool widget_is_enabled(widget_t *w) {
    if (!w) return false;
    return (w->flags & WIDGET_FLAG_ENABLED) != 0;
}

/**
 * Mark widget as needing redraw
 */
void widget_invalidate(widget_t *w) {
    if (!w) return;

    w->flags |= WIDGET_FLAG_DIRTY;

    /* Also invalidate parent */
    if (w->parent) {
        widget_invalidate(w->parent);
    }
}

/**
 * Get widget absolute screen position
 */
void widget_get_absolute_pos(widget_t *w, int32_t *abs_x, int32_t *abs_y) {
    if (!w || !abs_x || !abs_y) return;

    *abs_x = w->x;
    *abs_y = w->y;

    /* Walk up the parent chain */
    widget_t *parent = w->parent;
    while (parent) {
        *abs_x += parent->x;
        *abs_y += parent->y;
        parent = parent->parent;
    }
}

/**
 * Check if a point is inside a widget
 */
bool widget_contains_point(widget_t *w, int32_t x, int32_t y) {
    if (!w) return false;

    int32_t abs_x, abs_y;
    widget_get_absolute_pos(w, &abs_x, &abs_y);

    return (x >= abs_x && x < abs_x + w->width &&
            y >= abs_y && y < abs_y + w->height);
}

/**
 * Find the deepest widget at a given point
 */
widget_t *widget_find_at(widget_t *root, int32_t x, int32_t y) {
    if (!root) return NULL;

    /* Check if point is in this widget */
    if (!widget_contains_point(root, x, y)) {
        return NULL;
    }

    /* Check if visible */
    if (!(root->flags & WIDGET_FLAG_VISIBLE)) {
        return NULL;
    }

    /* Check children in reverse order (top to bottom) */
    for (int32_t i = (int32_t)root->child_count - 1; i >= 0; i--) {
        widget_t *found = widget_find_at(root->children[i], x, y);
        if (found) {
            return found;
        }
    }

    /* No child contains the point, return this widget */
    return root;
}

/**
 * Set widget bounds (position and size)
 */
void widget_set_bounds(widget_t *w, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!w) return;

    int32_t old_w = w->width;
    int32_t old_h = w->height;

    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;

    /* Send resize event if size changed */
    if (old_w != width || old_h != height) {
        event_t event;
        memset(&event, 0, sizeof(event_t));
        event.type = EVENT_RESIZE;
        event.target = w;
        event.resize.old_w = old_w;
        event.resize.old_h = old_h;
        event.resize.new_w = width;
        event.resize.new_h = height;
        widget_handle_event(w, &event);
    }

    widget_invalidate(w);

    kprintf("[WIDGET] Widget %u bounds set to (%d, %d) size %dx%d\n",
            w->id, x, y, width, height);
}

/**
 * Destroy a widget and free its resources
 */
void widget_destroy(widget_t *w) {
    if (!w) return;

    kprintf("[WIDGET] Destroying widget %u\n", w->id);

    /* Remove focus if this widget has it */
    if (g_focused_widget == w) {
        widget_set_focus(NULL);
    }

    /* Remove from parent */
    if (w->parent) {
        widget_remove_child(w->parent, w);
    }

    /* Destroy all children */
    while (w->child_count > 0) {
        widget_destroy(w->children[0]);
    }

    /* Clear the structure */
    memset(w, 0, sizeof(widget_t));
}
