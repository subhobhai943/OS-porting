/**
 * AAAos Text Editor Implementation
 *
 * A basic text editor for AAAos that supports opening, editing, and saving
 * text files. Works in both VGA text mode and graphical framebuffer mode.
 */

#include "editor.h"
#include "../../kernel/include/types.h"
#include "../../kernel/include/vga.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../fs/vfs/vfs.h"
#include "../../lib/libc/string.h"
#include "../../drivers/input/keyboard.h"

/*============================================================================
 * Line Buffer Functions
 *============================================================================*/

/**
 * Create a new line with given initial content
 */
line_t* line_create(const char *content, size_t length) {
    line_t *line = (line_t*)kmalloc(sizeof(line_t));
    if (!line) {
        kprintf("editor: failed to allocate line structure\n");
        return NULL;
    }

    /* Determine initial capacity */
    size_t capacity = EDITOR_INITIAL_CAPACITY;
    if (length >= capacity) {
        capacity = length + 1;
    }

    line->data = (char*)kmalloc(capacity);
    if (!line->data) {
        kprintf("editor: failed to allocate line data\n");
        kfree(line);
        return NULL;
    }

    /* Copy content if provided */
    if (content && length > 0) {
        memcpy(line->data, content, length);
    }
    line->data[length] = '\0';
    line->length = length;
    line->capacity = capacity;

    return line;
}

/**
 * Destroy a line and free its memory
 */
void line_destroy(line_t *line) {
    if (line) {
        if (line->data) {
            kfree(line->data);
        }
        kfree(line);
    }
}

/**
 * Ensure line has enough capacity
 */
bool line_ensure_capacity(line_t *line, size_t needed_capacity) {
    if (!line) {
        return false;
    }

    if (line->capacity >= needed_capacity) {
        return true;
    }

    /* Double capacity until it's enough */
    size_t new_capacity = line->capacity;
    while (new_capacity < needed_capacity) {
        new_capacity *= 2;
    }

    /* Enforce maximum line length */
    if (new_capacity > EDITOR_MAX_LINE_LENGTH) {
        new_capacity = EDITOR_MAX_LINE_LENGTH;
        if (needed_capacity > EDITOR_MAX_LINE_LENGTH) {
            kprintf("editor: line too long\n");
            return false;
        }
    }

    char *new_data = (char*)krealloc(line->data, new_capacity);
    if (!new_data) {
        kprintf("editor: failed to expand line capacity\n");
        return false;
    }

    line->data = new_data;
    line->capacity = new_capacity;
    return true;
}

/**
 * Insert character into line at position
 */
bool line_insert_char(line_t *line, size_t pos, char c) {
    if (!line || pos > line->length) {
        return false;
    }

    /* Ensure capacity for one more character plus null terminator */
    if (!line_ensure_capacity(line, line->length + 2)) {
        return false;
    }

    /* Shift characters to the right */
    memmove(line->data + pos + 1, line->data + pos, line->length - pos + 1);
    line->data[pos] = c;
    line->length++;

    return true;
}

/**
 * Delete character from line at position
 */
void line_delete_char(line_t *line, size_t pos) {
    if (!line || pos >= line->length) {
        return;
    }

    /* Shift characters to the left */
    memmove(line->data + pos, line->data + pos + 1, line->length - pos);
    line->length--;
}

/*============================================================================
 * Editor Line Management
 *============================================================================*/

/**
 * Insert a new line into the editor at given position
 */
bool editor_insert_line(editor_t *editor, size_t pos, line_t *line) {
    if (!editor || !line || pos > editor->num_lines) {
        return false;
    }

    /* Check maximum lines */
    if (editor->num_lines >= EDITOR_MAX_LINES) {
        kprintf("editor: maximum lines reached\n");
        return false;
    }

    /* Expand lines array if needed */
    if (editor->num_lines >= editor->lines_capacity) {
        size_t new_capacity = editor->lines_capacity * 2;
        line_t **new_lines = (line_t**)krealloc(editor->lines,
                                                 new_capacity * sizeof(line_t*));
        if (!new_lines) {
            kprintf("editor: failed to expand lines array\n");
            return false;
        }
        editor->lines = new_lines;
        editor->lines_capacity = new_capacity;
    }

    /* Shift lines down to make room */
    if (pos < editor->num_lines) {
        memmove(editor->lines + pos + 1, editor->lines + pos,
                (editor->num_lines - pos) * sizeof(line_t*));
    }

    editor->lines[pos] = line;
    editor->num_lines++;

    return true;
}

/**
 * Remove a line from the editor at given position
 */
void editor_remove_line(editor_t *editor, size_t pos) {
    if (!editor || pos >= editor->num_lines) {
        return;
    }

    /* Free the line */
    line_destroy(editor->lines[pos]);

    /* Shift lines up */
    if (pos < editor->num_lines - 1) {
        memmove(editor->lines + pos, editor->lines + pos + 1,
                (editor->num_lines - pos - 1) * sizeof(line_t*));
    }

    editor->num_lines--;
}

/*============================================================================
 * Editor Lifecycle Functions
 *============================================================================*/

/**
 * Initialize a new editor instance
 */
editor_t* editor_init(void) {
    editor_t *editor = (editor_t*)kmalloc(sizeof(editor_t));
    if (!editor) {
        kprintf("editor: failed to allocate editor structure\n");
        return NULL;
    }

    /* Zero out the structure */
    memset(editor, 0, sizeof(editor_t));

    /* Initialize lines array */
    editor->lines_capacity = 256;
    editor->lines = (line_t**)kmalloc(editor->lines_capacity * sizeof(line_t*));
    if (!editor->lines) {
        kprintf("editor: failed to allocate lines array\n");
        kfree(editor);
        return NULL;
    }

    /* Create initial empty line */
    line_t *first_line = line_create(NULL, 0);
    if (!first_line) {
        kfree(editor->lines);
        kfree(editor);
        return NULL;
    }
    editor->lines[0] = first_line;
    editor->num_lines = 1;

    /* Initialize display settings for VGA mode */
    editor->display_mode = EDITOR_MODE_VGA;
    editor->screen_width = VGA_WIDTH;
    editor->screen_height = VGA_HEIGHT;
    editor->text_area_height = VGA_HEIGHT - EDITOR_STATUS_HEIGHT;

    /* Initialize colors */
    editor->text_color = vga_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    editor->status_color = vga_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GRAY);
    editor->line_num_color = vga_color(VGA_COLOR_DARK_GRAY, VGA_COLOR_BLACK);

    /* Framebuffer colors */
    editor->fb_text_fg = 0xFFFFFF;
    editor->fb_text_bg = 0x000000;
    editor->fb_status_fg = 0x000000;
    editor->fb_status_bg = 0xC0C0C0;

    /* Line numbers */
    editor->show_line_numbers = true;
    editor->line_num_width = 5;

    /* Initial state */
    editor->state = EDITOR_STATE_NORMAL;
    editor->running = true;
    editor->has_filename = false;
    editor->modified = false;
    editor->readonly = false;

    kprintf("editor: initialized successfully\n");
    return editor;
}

/**
 * Destroy editor and free all resources
 */
void editor_destroy(editor_t *editor) {
    if (!editor) {
        return;
    }

    /* Free all lines */
    if (editor->lines) {
        for (size_t i = 0; i < editor->num_lines; i++) {
            line_destroy(editor->lines[i]);
        }
        kfree(editor->lines);
    }

    kfree(editor);
    kprintf("editor: destroyed\n");
}

/**
 * Create a new empty buffer
 */
void editor_new(editor_t *editor) {
    if (!editor) {
        return;
    }

    /* Free existing lines */
    for (size_t i = 0; i < editor->num_lines; i++) {
        line_destroy(editor->lines[i]);
    }

    /* Create single empty line */
    editor->lines[0] = line_create(NULL, 0);
    editor->num_lines = 1;

    /* Reset state */
    editor->filename[0] = '\0';
    editor->has_filename = false;
    editor->modified = false;
    editor->cursor_x = 0;
    editor->cursor_y = 0;
    editor->scroll_x = 0;
    editor->scroll_y = 0;

    editor_set_status(editor, "New file");
}

/*============================================================================
 * File Operations
 *============================================================================*/

/**
 * Open a file for editing
 */
int editor_open(editor_t *editor, const char *filename) {
    if (!editor || !filename) {
        return VFS_ERR_INVAL;
    }

    kprintf("editor: opening file '%s'\n", filename);

    /* Open the file */
    vfs_file_t *file = vfs_open(filename, VFS_O_RDONLY);
    if (!file) {
        kprintf("editor: failed to open file\n");
        editor_set_status(editor, "Failed to open file");
        return VFS_ERR_NOENT;
    }

    /* Get file size */
    vfs_stat_t stat;
    if (vfs_fstat(file, &stat) != VFS_OK) {
        vfs_close(file);
        return VFS_ERR_IO;
    }

    /* Allocate buffer for file content */
    size_t file_size = (size_t)stat.st_size;
    char *buffer = NULL;

    if (file_size > 0) {
        buffer = (char*)kmalloc(file_size + 1);
        if (!buffer) {
            vfs_close(file);
            return VFS_ERR_NOMEM;
        }

        /* Read file content */
        ssize_t bytes_read = vfs_read(file, buffer, file_size);
        if (bytes_read < 0) {
            kfree(buffer);
            vfs_close(file);
            return (int)bytes_read;
        }
        buffer[bytes_read] = '\0';
    }

    vfs_close(file);

    /* Clear existing content */
    for (size_t i = 0; i < editor->num_lines; i++) {
        line_destroy(editor->lines[i]);
    }
    editor->num_lines = 0;

    /* Parse content into lines */
    if (buffer && file_size > 0) {
        char *start = buffer;
        char *ptr = buffer;

        while (*ptr) {
            if (*ptr == '\n') {
                size_t len = (size_t)(ptr - start);
                /* Remove trailing CR if present (Windows line endings) */
                if (len > 0 && start[len - 1] == '\r') {
                    len--;
                }

                line_t *line = line_create(start, len);
                if (line) {
                    editor_insert_line(editor, editor->num_lines, line);
                }
                start = ptr + 1;
            }
            ptr++;
        }

        /* Handle last line (may not end with newline) */
        if (start < ptr) {
            size_t len = (size_t)(ptr - start);
            if (len > 0 && start[len - 1] == '\r') {
                len--;
            }
            line_t *line = line_create(start, len);
            if (line) {
                editor_insert_line(editor, editor->num_lines, line);
            }
        }

        kfree(buffer);
    }

    /* Ensure at least one line exists */
    if (editor->num_lines == 0) {
        line_t *line = line_create(NULL, 0);
        if (line) {
            editor_insert_line(editor, 0, line);
        }
    }

    /* Update editor state */
    strncpy(editor->filename, filename, EDITOR_MAX_FILENAME - 1);
    editor->filename[EDITOR_MAX_FILENAME - 1] = '\0';
    editor->has_filename = true;
    editor->modified = false;
    editor->cursor_x = 0;
    editor->cursor_y = 0;
    editor->scroll_x = 0;
    editor->scroll_y = 0;

    editor_set_status(editor, "File opened");
    kprintf("editor: loaded %zu lines\n", editor->num_lines);

    return VFS_OK;
}

/**
 * Save the current file
 */
int editor_save(editor_t *editor) {
    if (!editor) {
        return VFS_ERR_INVAL;
    }

    if (!editor->has_filename) {
        editor_set_status(editor, "No filename - use Save As");
        return VFS_ERR_INVAL;
    }

    return editor_save_as(editor, editor->filename);
}

/**
 * Save the current file with a new name
 */
int editor_save_as(editor_t *editor, const char *filename) {
    if (!editor || !filename) {
        return VFS_ERR_INVAL;
    }

    kprintf("editor: saving to '%s'\n", filename);

    /* Open file for writing */
    vfs_file_t *file = vfs_open(filename, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (!file) {
        kprintf("editor: failed to create file\n");
        editor_set_status(editor, "Failed to save file");
        return VFS_ERR_IO;
    }

    /* Write each line */
    for (size_t i = 0; i < editor->num_lines; i++) {
        line_t *line = editor->lines[i];

        /* Write line content */
        if (line->length > 0) {
            ssize_t written = vfs_write(file, line->data, line->length);
            if (written < 0) {
                vfs_close(file);
                return (int)written;
            }
        }

        /* Write newline (except for last line if it's empty) */
        if (i < editor->num_lines - 1 || line->length > 0) {
            char newline = '\n';
            vfs_write(file, &newline, 1);
        }
    }

    vfs_close(file);

    /* Update state */
    strncpy(editor->filename, filename, EDITOR_MAX_FILENAME - 1);
    editor->filename[EDITOR_MAX_FILENAME - 1] = '\0';
    editor->has_filename = true;
    editor->modified = false;

    editor_set_status(editor, "File saved");
    kprintf("editor: saved %zu lines\n", editor->num_lines);

    return VFS_OK;
}

/*============================================================================
 * Text Editing Functions
 *============================================================================*/

/**
 * Insert a character at the current cursor position
 */
void editor_insert_char(editor_t *editor, char c) {
    if (!editor || editor->cursor_y >= (int)editor->num_lines) {
        return;
    }

    line_t *line = editor->lines[editor->cursor_y];

    /* Clamp cursor to line length */
    if (editor->cursor_x > (int)line->length) {
        editor->cursor_x = (int)line->length;
    }

    if (line_insert_char(line, (size_t)editor->cursor_x, c)) {
        editor->cursor_x++;
        editor->modified = true;
    }
}

/**
 * Delete the character before the cursor (backspace)
 */
void editor_delete_char(editor_t *editor) {
    if (!editor) {
        return;
    }

    if (editor->cursor_x > 0) {
        /* Delete character in current line */
        line_t *line = editor->lines[editor->cursor_y];
        editor->cursor_x--;
        line_delete_char(line, (size_t)editor->cursor_x);
        editor->modified = true;
    } else if (editor->cursor_y > 0) {
        /* Join with previous line */
        line_t *curr_line = editor->lines[editor->cursor_y];
        line_t *prev_line = editor->lines[editor->cursor_y - 1];

        /* Move cursor to end of previous line */
        editor->cursor_x = (int)prev_line->length;

        /* Append current line to previous line */
        if (curr_line->length > 0) {
            if (line_ensure_capacity(prev_line, prev_line->length + curr_line->length + 1)) {
                memcpy(prev_line->data + prev_line->length, curr_line->data, curr_line->length);
                prev_line->length += curr_line->length;
                prev_line->data[prev_line->length] = '\0';
            }
        }

        /* Remove current line */
        editor_remove_line(editor, (size_t)editor->cursor_y);
        editor->cursor_y--;
        editor->modified = true;
    }
}

/**
 * Delete the character at the cursor position (delete key)
 */
void editor_delete_char_forward(editor_t *editor) {
    if (!editor || editor->cursor_y >= (int)editor->num_lines) {
        return;
    }

    line_t *line = editor->lines[editor->cursor_y];

    if (editor->cursor_x < (int)line->length) {
        /* Delete character at cursor */
        line_delete_char(line, (size_t)editor->cursor_x);
        editor->modified = true;
    } else if (editor->cursor_y < (int)editor->num_lines - 1) {
        /* Join with next line */
        line_t *next_line = editor->lines[editor->cursor_y + 1];

        /* Append next line to current line */
        if (next_line->length > 0) {
            if (line_ensure_capacity(line, line->length + next_line->length + 1)) {
                memcpy(line->data + line->length, next_line->data, next_line->length);
                line->length += next_line->length;
                line->data[line->length] = '\0';
            }
        }

        /* Remove next line */
        editor_remove_line(editor, (size_t)editor->cursor_y + 1);
        editor->modified = true;
    }
}

/**
 * Insert a newline at the current cursor position
 */
void editor_newline(editor_t *editor) {
    if (!editor || editor->cursor_y >= (int)editor->num_lines) {
        return;
    }

    line_t *curr_line = editor->lines[editor->cursor_y];

    /* Clamp cursor to line length */
    if (editor->cursor_x > (int)curr_line->length) {
        editor->cursor_x = (int)curr_line->length;
    }

    /* Create new line with content after cursor */
    size_t split_pos = (size_t)editor->cursor_x;
    size_t remaining_len = curr_line->length - split_pos;

    line_t *new_line = line_create(curr_line->data + split_pos, remaining_len);
    if (!new_line) {
        return;
    }

    /* Truncate current line */
    curr_line->length = split_pos;
    curr_line->data[split_pos] = '\0';

    /* Insert new line */
    if (!editor_insert_line(editor, (size_t)editor->cursor_y + 1, new_line)) {
        line_destroy(new_line);
        return;
    }

    /* Move cursor to start of new line */
    editor->cursor_y++;
    editor->cursor_x = 0;
    editor->modified = true;
}

/**
 * Insert a tab at the current cursor position
 */
void editor_insert_tab(editor_t *editor) {
    if (!editor) {
        return;
    }

    /* Insert spaces for tab */
    for (int i = 0; i < EDITOR_TAB_WIDTH; i++) {
        editor_insert_char(editor, ' ');
    }
}

/**
 * Delete the current line
 */
void editor_delete_line(editor_t *editor) {
    if (!editor || editor->num_lines <= 1) {
        /* Keep at least one line */
        if (editor->num_lines == 1) {
            line_t *line = editor->lines[0];
            line->length = 0;
            line->data[0] = '\0';
            editor->cursor_x = 0;
            editor->modified = true;
        }
        return;
    }

    editor_remove_line(editor, (size_t)editor->cursor_y);

    /* Adjust cursor if needed */
    if (editor->cursor_y >= (int)editor->num_lines) {
        editor->cursor_y = (int)editor->num_lines - 1;
    }

    /* Clamp cursor x to new line length */
    line_t *line = editor->lines[editor->cursor_y];
    if (editor->cursor_x > (int)line->length) {
        editor->cursor_x = (int)line->length;
    }

    editor->modified = true;
}

/*============================================================================
 * Cursor Movement Functions
 *============================================================================*/

/**
 * Move cursor by relative offset
 */
void editor_move_cursor(editor_t *editor, int dx, int dy) {
    if (!editor) {
        return;
    }

    /* Move vertically */
    editor->cursor_y += dy;

    /* Clamp to valid line range */
    if (editor->cursor_y < 0) {
        editor->cursor_y = 0;
    }
    if (editor->cursor_y >= (int)editor->num_lines) {
        editor->cursor_y = (int)editor->num_lines - 1;
    }

    /* Move horizontally */
    line_t *line = editor->lines[editor->cursor_y];
    editor->cursor_x += dx;

    /* Handle wrapping */
    if (editor->cursor_x < 0) {
        if (editor->cursor_y > 0) {
            editor->cursor_y--;
            line = editor->lines[editor->cursor_y];
            editor->cursor_x = (int)line->length;
        } else {
            editor->cursor_x = 0;
        }
    } else if (editor->cursor_x > (int)line->length) {
        if (editor->cursor_y < (int)editor->num_lines - 1) {
            editor->cursor_y++;
            editor->cursor_x = 0;
        } else {
            editor->cursor_x = (int)line->length;
        }
    }

    /* Ensure cursor is visible */
    editor_scroll_to_cursor(editor);
}

/**
 * Move cursor to start of current line
 */
void editor_home(editor_t *editor) {
    if (editor) {
        editor->cursor_x = 0;
        editor_scroll_to_cursor(editor);
    }
}

/**
 * Move cursor to end of current line
 */
void editor_end(editor_t *editor) {
    if (editor && editor->cursor_y < (int)editor->num_lines) {
        line_t *line = editor->lines[editor->cursor_y];
        editor->cursor_x = (int)line->length;
        editor_scroll_to_cursor(editor);
    }
}

/**
 * Move cursor up one page
 */
void editor_page_up(editor_t *editor) {
    if (!editor) {
        return;
    }

    editor->cursor_y -= editor->text_area_height;
    if (editor->cursor_y < 0) {
        editor->cursor_y = 0;
    }

    /* Clamp cursor x to line length */
    line_t *line = editor->lines[editor->cursor_y];
    if (editor->cursor_x > (int)line->length) {
        editor->cursor_x = (int)line->length;
    }

    editor_scroll_to_cursor(editor);
}

/**
 * Move cursor down one page
 */
void editor_page_down(editor_t *editor) {
    if (!editor) {
        return;
    }

    editor->cursor_y += editor->text_area_height;
    if (editor->cursor_y >= (int)editor->num_lines) {
        editor->cursor_y = (int)editor->num_lines - 1;
    }

    /* Clamp cursor x to line length */
    line_t *line = editor->lines[editor->cursor_y];
    if (editor->cursor_x > (int)line->length) {
        editor->cursor_x = (int)line->length;
    }

    editor_scroll_to_cursor(editor);
}

/**
 * Go to a specific line number
 */
void editor_goto_line(editor_t *editor, int line_num) {
    if (!editor) {
        return;
    }

    /* Convert from 1-based to 0-based */
    int target = line_num - 1;

    if (target < 0) {
        target = 0;
    }
    if (target >= (int)editor->num_lines) {
        target = (int)editor->num_lines - 1;
    }

    editor->cursor_y = target;
    editor->cursor_x = 0;
    editor_scroll_to_cursor(editor);
}

/**
 * Move cursor to start of document
 */
void editor_goto_start(editor_t *editor) {
    if (editor) {
        editor->cursor_x = 0;
        editor->cursor_y = 0;
        editor_scroll_to_cursor(editor);
    }
}

/**
 * Move cursor to end of document
 */
void editor_goto_end(editor_t *editor) {
    if (!editor || editor->num_lines == 0) {
        return;
    }

    editor->cursor_y = (int)editor->num_lines - 1;
    line_t *line = editor->lines[editor->cursor_y];
    editor->cursor_x = (int)line->length;
    editor_scroll_to_cursor(editor);
}

/**
 * Scroll the view to ensure cursor is visible
 */
void editor_scroll_to_cursor(editor_t *editor) {
    if (!editor) {
        return;
    }

    /* Vertical scrolling */
    if (editor->cursor_y < editor->scroll_y) {
        editor->scroll_y = editor->cursor_y;
    }
    if (editor->cursor_y >= editor->scroll_y + editor->text_area_height) {
        editor->scroll_y = editor->cursor_y - editor->text_area_height + 1;
    }

    /* Calculate text area width (accounting for line numbers) */
    int text_width = editor->screen_width;
    if (editor->show_line_numbers) {
        text_width -= editor->line_num_width;
    }

    /* Horizontal scrolling */
    if (editor->cursor_x < editor->scroll_x) {
        editor->scroll_x = editor->cursor_x;
    }
    if (editor->cursor_x >= editor->scroll_x + text_width) {
        editor->scroll_x = editor->cursor_x - text_width + 1;
    }
}

/*============================================================================
 * Display Functions
 *============================================================================*/

/**
 * Get the display width of a character (handles tabs)
 */
int editor_char_width(char c, int col) {
    if (c == '\t') {
        return EDITOR_TAB_WIDTH - (col % EDITOR_TAB_WIDTH);
    }
    return 1;
}

/**
 * Convert screen column to buffer offset (handles tabs)
 */
int editor_screen_to_offset(line_t *line, int screen_col) {
    if (!line) {
        return 0;
    }

    int col = 0;
    for (size_t i = 0; i < line->length; i++) {
        int width = editor_char_width(line->data[i], col);
        if (col + width > screen_col) {
            return (int)i;
        }
        col += width;
    }
    return (int)line->length;
}

/**
 * Convert buffer offset to screen column (handles tabs)
 */
int editor_offset_to_screen(line_t *line, int offset) {
    if (!line) {
        return 0;
    }

    int col = 0;
    for (int i = 0; i < offset && i < (int)line->length; i++) {
        col += editor_char_width(line->data[i], col);
    }
    return col;
}

/**
 * Redraw the entire editor display
 */
void editor_draw(editor_t *editor) {
    if (!editor) {
        return;
    }

    /* Get VGA memory pointer */
    uint16_t *vga_buffer = (uint16_t*)VGA_MEMORY;

    int text_start_x = 0;
    if (editor->show_line_numbers) {
        text_start_x = editor->line_num_width;
    }

    /* Draw each visible line */
    for (int y = 0; y < editor->text_area_height; y++) {
        int line_idx = editor->scroll_y + y;
        int screen_y = y;

        /* Clear line */
        for (int x = 0; x < editor->screen_width; x++) {
            vga_buffer[screen_y * VGA_WIDTH + x] = vga_entry(' ', editor->text_color);
        }

        if (line_idx >= (int)editor->num_lines) {
            /* Draw tilde for empty lines beyond file */
            if (editor->show_line_numbers) {
                vga_buffer[screen_y * VGA_WIDTH] = vga_entry('~', editor->line_num_color);
            }
            continue;
        }

        /* Draw line number */
        if (editor->show_line_numbers) {
            char num_buf[16];
            int num_len = 0;
            int num = line_idx + 1;

            /* Convert number to string (reversed) */
            char temp[16];
            int temp_len = 0;
            if (num == 0) {
                temp[temp_len++] = '0';
            } else {
                while (num > 0) {
                    temp[temp_len++] = '0' + (num % 10);
                    num /= 10;
                }
            }

            /* Right-align the number */
            int padding = editor->line_num_width - 1 - temp_len;
            for (int i = 0; i < padding; i++) {
                num_buf[num_len++] = ' ';
            }
            while (temp_len > 0) {
                num_buf[num_len++] = temp[--temp_len];
            }
            num_buf[num_len++] = ' ';

            /* Draw line number */
            for (int i = 0; i < editor->line_num_width && i < num_len; i++) {
                vga_buffer[screen_y * VGA_WIDTH + i] =
                    vga_entry(num_buf[i], editor->line_num_color);
            }
        }

        /* Draw line content */
        line_t *line = editor->lines[line_idx];
        int screen_x = text_start_x;
        int buffer_col = 0;

        for (size_t i = 0; i < line->length && screen_x < editor->screen_width; i++) {
            char c = line->data[i];

            /* Handle tabs */
            if (c == '\t') {
                int tab_width = EDITOR_TAB_WIDTH - (buffer_col % EDITOR_TAB_WIDTH);
                for (int t = 0; t < tab_width && screen_x < editor->screen_width; t++) {
                    if (buffer_col >= editor->scroll_x) {
                        vga_buffer[screen_y * VGA_WIDTH + screen_x] =
                            vga_entry(' ', editor->text_color);
                        screen_x++;
                    }
                    buffer_col++;
                }
            } else {
                if (buffer_col >= editor->scroll_x) {
                    vga_buffer[screen_y * VGA_WIDTH + screen_x] =
                        vga_entry(c, editor->text_color);
                    screen_x++;
                }
                buffer_col++;
            }
        }
    }

    /* Draw status bar */
    editor_draw_status(editor);

    /* Update cursor */
    editor_update_cursor(editor);
}

/**
 * Draw the status bar
 */
void editor_draw_status(editor_t *editor) {
    if (!editor) {
        return;
    }

    uint16_t *vga_buffer = (uint16_t*)VGA_MEMORY;
    int status_y = editor->text_area_height;

    /* Clear status bar */
    for (int x = 0; x < editor->screen_width; x++) {
        vga_buffer[status_y * VGA_WIDTH + x] = vga_entry(' ', editor->status_color);
    }

    /* Build status string */
    char status[256];
    int pos = 0;

    /* Filename */
    if (editor->has_filename) {
        const char *fn = editor->filename;
        while (*fn && pos < 30) {
            status[pos++] = *fn++;
        }
    } else {
        const char *newfile = "[New File]";
        while (*newfile && pos < 30) {
            status[pos++] = *newfile++;
        }
    }

    /* Modified indicator */
    if (editor->modified) {
        status[pos++] = ' ';
        status[pos++] = '[';
        status[pos++] = '+';
        status[pos++] = ']';
    }

    /* Spacing */
    while (pos < 40) {
        status[pos++] = ' ';
    }

    /* Status message or help */
    if (editor->status_message[0]) {
        const char *msg = editor->status_message;
        while (*msg && pos < 60) {
            status[pos++] = *msg++;
        }
    }

    /* Right-align position info */
    char pos_info[32];
    int pi_len = 0;

    /* Build "Line X, Col Y" string */
    const char *line_str = "Ln ";
    while (*line_str) pos_info[pi_len++] = *line_str++;

    /* Line number */
    int ln = editor->cursor_y + 1;
    char num_buf[16];
    int num_len = 0;
    if (ln == 0) {
        num_buf[num_len++] = '0';
    } else {
        while (ln > 0) {
            num_buf[num_len++] = '0' + (ln % 10);
            ln /= 10;
        }
    }
    while (num_len > 0) {
        pos_info[pi_len++] = num_buf[--num_len];
    }

    pos_info[pi_len++] = ',';
    pos_info[pi_len++] = ' ';

    const char *col_str = "Col ";
    while (*col_str) pos_info[pi_len++] = *col_str++;

    /* Column number */
    int col = editor->cursor_x + 1;
    num_len = 0;
    if (col == 0) {
        num_buf[num_len++] = '0';
    } else {
        while (col > 0) {
            num_buf[num_len++] = '0' + (col % 10);
            col /= 10;
        }
    }
    while (num_len > 0) {
        pos_info[pi_len++] = num_buf[--num_len];
    }
    pos_info[pi_len] = '\0';

    /* Draw position info at right side */
    int right_pos = editor->screen_width - pi_len - 1;
    for (int i = 0; i < pi_len; i++) {
        vga_buffer[status_y * VGA_WIDTH + right_pos + i] =
            vga_entry(pos_info[i], editor->status_color);
    }

    /* Draw status content */
    for (int i = 0; i < pos && i < right_pos - 1; i++) {
        vga_buffer[status_y * VGA_WIDTH + i] = vga_entry(status[i], editor->status_color);
    }
}

/**
 * Update the cursor position on screen
 */
void editor_update_cursor(editor_t *editor) {
    if (!editor) {
        return;
    }

    int screen_x = editor->cursor_x - editor->scroll_x;
    int screen_y = editor->cursor_y - editor->scroll_y;

    if (editor->show_line_numbers) {
        screen_x += editor->line_num_width;
    }

    /* Only show cursor if visible */
    if (screen_x >= 0 && screen_x < editor->screen_width &&
        screen_y >= 0 && screen_y < editor->text_area_height) {
        vga_set_cursor(screen_x, screen_y);
        vga_enable_cursor(true);
    } else {
        vga_enable_cursor(false);
    }
}

/**
 * Set a status message to display
 */
void editor_set_status(editor_t *editor, const char *message) {
    if (!editor) {
        return;
    }

    if (message) {
        strncpy(editor->status_message, message, sizeof(editor->status_message) - 1);
        editor->status_message[sizeof(editor->status_message) - 1] = '\0';
    } else {
        editor->status_message[0] = '\0';
    }
}

/*============================================================================
 * Search Functions
 *============================================================================*/

/**
 * Start search mode
 */
void editor_start_search(editor_t *editor) {
    if (editor) {
        editor->state = EDITOR_STATE_SEARCH;
        editor->input_length = 0;
        editor->input_buffer[0] = '\0';
        editor_set_status(editor, "Search: ");
    }
}

/**
 * Find next occurrence of search query
 */
bool editor_find_next(editor_t *editor) {
    if (!editor || editor->search_query[0] == '\0') {
        return false;
    }

    size_t query_len = strlen(editor->search_query);

    /* Start from cursor position */
    int start_line = editor->cursor_y;
    int start_col = editor->cursor_x + 1;

    for (int i = 0; i < (int)editor->num_lines; i++) {
        int line_idx = (start_line + i) % (int)editor->num_lines;
        line_t *line = editor->lines[line_idx];

        int search_start = (i == 0) ? start_col : 0;

        /* Search in this line */
        for (int j = search_start; j <= (int)line->length - (int)query_len; j++) {
            bool match = true;
            for (size_t k = 0; k < query_len; k++) {
                if (line->data[j + k] != editor->search_query[k]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                editor->cursor_y = line_idx;
                editor->cursor_x = j;
                editor->search_match_line = line_idx;
                editor->search_match_col = j;
                editor_scroll_to_cursor(editor);
                editor_set_status(editor, "Found");
                return true;
            }
        }
    }

    editor_set_status(editor, "Not found");
    return false;
}

/**
 * Find previous occurrence of search query
 */
bool editor_find_prev(editor_t *editor) {
    if (!editor || editor->search_query[0] == '\0') {
        return false;
    }

    size_t query_len = strlen(editor->search_query);

    /* Start from cursor position */
    int start_line = editor->cursor_y;
    int start_col = editor->cursor_x - 1;

    for (int i = 0; i < (int)editor->num_lines; i++) {
        int line_idx = (start_line - i + (int)editor->num_lines) % (int)editor->num_lines;
        line_t *line = editor->lines[line_idx];

        int search_end = (i == 0) ? start_col : (int)line->length - (int)query_len;

        /* Search in this line (backwards) */
        for (int j = search_end; j >= 0; j--) {
            bool match = true;
            for (size_t k = 0; k < query_len; k++) {
                if (line->data[j + k] != editor->search_query[k]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                editor->cursor_y = line_idx;
                editor->cursor_x = j;
                editor->search_match_line = line_idx;
                editor->search_match_col = j;
                editor_scroll_to_cursor(editor);
                editor_set_status(editor, "Found");
                return true;
            }
        }
    }

    editor_set_status(editor, "Not found");
    return false;
}

/*============================================================================
 * Input Handling Functions
 *============================================================================*/

/**
 * Parse a number from the input buffer
 */
static int parse_input_number(editor_t *editor) {
    int result = 0;
    for (size_t i = 0; i < editor->input_length; i++) {
        char c = editor->input_buffer[i];
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
        }
    }
    return result;
}

/**
 * Handle a keyboard event
 */
void editor_handle_key(editor_t *editor, int key, uint16_t modifiers, char ascii) {
    if (!editor) {
        return;
    }

    bool ctrl = (modifiers & MOD_CTRL) != 0;
    bool shift = (modifiers & MOD_SHIFT) != 0;
    UNUSED(shift);

    /* Handle state-specific input */
    switch (editor->state) {
        case EDITOR_STATE_GOTO:
            /* Go to line mode */
            if (key == KEY_ENTER) {
                int line_num = parse_input_number(editor);
                if (line_num > 0) {
                    editor_goto_line(editor, line_num);
                }
                editor->state = EDITOR_STATE_NORMAL;
                editor_set_status(editor, NULL);
            } else if (key == KEY_ESCAPE) {
                editor->state = EDITOR_STATE_NORMAL;
                editor_set_status(editor, NULL);
            } else if (key == KEY_BACKSPACE && editor->input_length > 0) {
                editor->input_length--;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Go to line: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            } else if (ascii >= '0' && ascii <= '9' && editor->input_length < sizeof(editor->input_buffer) - 1) {
                editor->input_buffer[editor->input_length++] = ascii;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Go to line: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            }
            return;

        case EDITOR_STATE_SEARCH:
            /* Search mode */
            if (key == KEY_ENTER) {
                strncpy(editor->search_query, editor->input_buffer, sizeof(editor->search_query) - 1);
                editor->search_query[sizeof(editor->search_query) - 1] = '\0';
                editor->state = EDITOR_STATE_NORMAL;
                editor_find_next(editor);
            } else if (key == KEY_ESCAPE) {
                editor->state = EDITOR_STATE_NORMAL;
                editor_set_status(editor, NULL);
            } else if (key == KEY_BACKSPACE && editor->input_length > 0) {
                editor->input_length--;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Search: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            } else if (ascii >= 32 && ascii < 127 && editor->input_length < sizeof(editor->input_buffer) - 1) {
                editor->input_buffer[editor->input_length++] = ascii;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Search: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            }
            return;

        case EDITOR_STATE_SAVE_AS:
            /* Save-as mode */
            if (key == KEY_ENTER) {
                if (editor->input_length > 0) {
                    editor_save_as(editor, editor->input_buffer);
                }
                editor->state = EDITOR_STATE_NORMAL;
            } else if (key == KEY_ESCAPE) {
                editor->state = EDITOR_STATE_NORMAL;
                editor_set_status(editor, NULL);
            } else if (key == KEY_BACKSPACE && editor->input_length > 0) {
                editor->input_length--;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Save as: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            } else if (ascii >= 32 && ascii < 127 && editor->input_length < sizeof(editor->input_buffer) - 1) {
                editor->input_buffer[editor->input_length++] = ascii;
                editor->input_buffer[editor->input_length] = '\0';
                char msg[64] = "Save as: ";
                strcat(msg, editor->input_buffer);
                editor_set_status(editor, msg);
            }
            return;

        case EDITOR_STATE_CONFIRM_QUIT:
            /* Confirm quit with unsaved changes */
            if (ascii == 'y' || ascii == 'Y') {
                editor->running = false;
            } else if (ascii == 'n' || ascii == 'N' || key == KEY_ESCAPE) {
                editor->state = EDITOR_STATE_NORMAL;
                editor_set_status(editor, NULL);
            }
            return;

        default:
            break;
    }

    /* Normal mode key handling */

    /* Control key shortcuts */
    if (ctrl) {
        switch (key) {
            case KEY_Q:
                /* Ctrl+Q: Quit */
                if (editor->modified) {
                    editor->state = EDITOR_STATE_CONFIRM_QUIT;
                    editor_set_status(editor, "Unsaved changes! Quit? (y/n)");
                } else {
                    editor->running = false;
                }
                return;

            case KEY_S:
                /* Ctrl+S: Save */
                if (editor->has_filename) {
                    editor_save(editor);
                } else {
                    editor->state = EDITOR_STATE_SAVE_AS;
                    editor->input_length = 0;
                    editor->input_buffer[0] = '\0';
                    editor_set_status(editor, "Save as: ");
                }
                return;

            case KEY_G:
                /* Ctrl+G: Go to line */
                editor->state = EDITOR_STATE_GOTO;
                editor->input_length = 0;
                editor->input_buffer[0] = '\0';
                editor_set_status(editor, "Go to line: ");
                return;

            case KEY_F:
                /* Ctrl+F: Find */
                editor_start_search(editor);
                return;

            case KEY_N:
                /* Ctrl+N: New file */
                if (editor->modified) {
                    editor_set_status(editor, "Save first! (Ctrl+S)");
                } else {
                    editor_new(editor);
                }
                return;

            case KEY_HOME:
                /* Ctrl+Home: Go to start */
                editor_goto_start(editor);
                return;

            case KEY_END:
                /* Ctrl+End: Go to end */
                editor_goto_end(editor);
                return;

            default:
                break;
        }
    }

    /* Regular keys */
    switch (key) {
        case KEY_UP:
            editor_move_cursor(editor, 0, -1);
            break;

        case KEY_DOWN:
            editor_move_cursor(editor, 0, 1);
            break;

        case KEY_LEFT:
            editor_move_cursor(editor, -1, 0);
            break;

        case KEY_RIGHT:
            editor_move_cursor(editor, 1, 0);
            break;

        case KEY_HOME:
            editor_home(editor);
            break;

        case KEY_END:
            editor_end(editor);
            break;

        case KEY_PAGEUP:
            editor_page_up(editor);
            break;

        case KEY_PAGEDOWN:
            editor_page_down(editor);
            break;

        case KEY_BACKSPACE:
            editor_delete_char(editor);
            break;

        case KEY_DELETE:
            editor_delete_char_forward(editor);
            break;

        case KEY_ENTER:
            editor_newline(editor);
            break;

        case KEY_TAB:
            editor_insert_tab(editor);
            break;

        case KEY_ESCAPE:
            editor_set_status(editor, "^S:Save ^Q:Quit ^G:Goto ^F:Find");
            break;

        case KEY_F3:
            /* F3: Find next */
            editor_find_next(editor);
            break;

        default:
            /* Printable characters */
            if (ascii >= 32 && ascii < 127) {
                editor_insert_char(editor, ascii);
            }
            break;
    }
}

/**
 * Process keyboard input (called from main loop)
 */
void editor_process_input(editor_t *editor) {
    if (!editor) {
        return;
    }

    key_event_t event;
    if (keyboard_poll_event(&event)) {
        /* Only handle key presses, not releases */
        if (event.pressed) {
            editor_handle_key(editor, event.keycode, event.modifiers, event.ascii);
        }
    }
}

/*============================================================================
 * Main Loop
 *============================================================================*/

/**
 * Run the main editor loop
 */
void editor_run(editor_t *editor) {
    if (!editor) {
        return;
    }

    kprintf("editor: starting main loop\n");

    /* Clear screen and set up display */
    vga_clear();
    vga_enable_cursor(true);

    /* Initial status message */
    editor_set_status(editor, "^S:Save ^Q:Quit ^G:Goto ^F:Find");

    /* Main loop */
    while (editor->running) {
        /* Draw the editor */
        editor_draw(editor);

        /* Process input */
        editor_process_input(editor);
    }

    /* Clean up display */
    vga_clear();
    vga_enable_cursor(true);
    vga_set_cursor(0, 0);

    kprintf("editor: exiting main loop\n");
}
