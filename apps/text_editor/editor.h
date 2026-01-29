/**
 * AAAos Text Editor
 *
 * A basic text editor for AAAos that supports opening, editing, and saving
 * text files. Works in both VGA text mode and graphical framebuffer mode.
 */

#ifndef _AAAOS_APP_TEXT_EDITOR_H
#define _AAAOS_APP_TEXT_EDITOR_H

#include "../../kernel/include/types.h"

/* Editor configuration */
#define EDITOR_MAX_FILENAME     256         /* Maximum filename length */
#define EDITOR_MAX_LINES        65536       /* Maximum number of lines */
#define EDITOR_MAX_LINE_LENGTH  4096        /* Maximum characters per line */
#define EDITOR_TAB_WIDTH        4           /* Tab display width */
#define EDITOR_INITIAL_CAPACITY 1024        /* Initial line capacity */

/* Editor status bar height (in lines/rows) */
#define EDITOR_STATUS_HEIGHT    1

/* Editor display modes */
typedef enum {
    EDITOR_MODE_VGA,            /* VGA text mode (80x25) */
    EDITOR_MODE_FRAMEBUFFER     /* Graphical framebuffer mode */
} editor_mode_t;

/* Editor states */
typedef enum {
    EDITOR_STATE_NORMAL,        /* Normal editing mode */
    EDITOR_STATE_SEARCH,        /* Search/find mode */
    EDITOR_STATE_GOTO,          /* Go to line mode */
    EDITOR_STATE_SAVE_AS,       /* Save-as prompt mode */
    EDITOR_STATE_CONFIRM_QUIT   /* Confirm quit with unsaved changes */
} editor_state_t;

/**
 * Single line of text in the editor buffer
 */
typedef struct line {
    char    *data;              /* Line text data (null-terminated) */
    size_t  length;             /* Current length (excluding null) */
    size_t  capacity;           /* Allocated capacity */
} line_t;

/**
 * Editor state structure
 * Contains all state needed for the text editor
 */
typedef struct editor {
    /* File information */
    char        filename[EDITOR_MAX_FILENAME];  /* Current filename */
    bool        has_filename;                   /* true if file has been named */
    bool        modified;                       /* true if buffer modified since save */
    bool        readonly;                       /* true if file is read-only */

    /* Text buffer */
    line_t      **lines;                        /* Array of line pointers */
    size_t      num_lines;                      /* Number of lines in buffer */
    size_t      lines_capacity;                 /* Allocated capacity for lines array */

    /* Cursor position */
    int         cursor_x;                       /* Cursor column (0-based) */
    int         cursor_y;                       /* Cursor row/line (0-based) */

    /* View/scroll position */
    int         scroll_x;                       /* Horizontal scroll offset */
    int         scroll_y;                       /* Vertical scroll offset (top visible line) */

    /* Display dimensions */
    int         screen_width;                   /* Screen width in characters */
    int         screen_height;                  /* Screen height in characters */
    int         text_area_height;               /* Height available for text (minus status) */

    /* Display mode */
    editor_mode_t display_mode;                 /* VGA or framebuffer mode */

    /* Editor state */
    editor_state_t state;                       /* Current editor state */
    bool        running;                        /* true while editor is running */

    /* Input buffer for prompts (search, goto, etc.) */
    char        input_buffer[256];              /* Input buffer for prompts */
    size_t      input_length;                   /* Current input length */
    char        status_message[256];            /* Status message to display */

    /* Search state */
    char        search_query[256];              /* Last search query */
    int         search_match_line;              /* Line of last match */
    int         search_match_col;               /* Column of last match */

    /* Display colors (for VGA mode) */
    uint8_t     text_color;                     /* Normal text color */
    uint8_t     status_color;                   /* Status bar color */
    uint8_t     line_num_color;                 /* Line number color */

    /* Display colors (for framebuffer mode) */
    uint32_t    fb_text_fg;                     /* Text foreground color */
    uint32_t    fb_text_bg;                     /* Text background color */
    uint32_t    fb_status_fg;                   /* Status bar foreground */
    uint32_t    fb_status_bg;                   /* Status bar background */

    /* Line numbers */
    bool        show_line_numbers;              /* Display line numbers */
    int         line_num_width;                 /* Width of line number gutter */

} editor_t;

/*============================================================================
 * Editor Lifecycle Functions
 *============================================================================*/

/**
 * Initialize a new editor instance
 * @return Pointer to new editor, or NULL on failure
 */
editor_t* editor_init(void);

/**
 * Destroy editor and free all resources
 * @param editor Editor instance to destroy
 */
void editor_destroy(editor_t *editor);

/**
 * Run the main editor loop
 * @param editor Editor instance
 */
void editor_run(editor_t *editor);

/*============================================================================
 * File Operations
 *============================================================================*/

/**
 * Open a file for editing
 * @param editor Editor instance
 * @param filename Path to file to open
 * @return 0 on success, negative error code on failure
 */
int editor_open(editor_t *editor, const char *filename);

/**
 * Save the current file
 * @param editor Editor instance
 * @return 0 on success, negative error code on failure
 */
int editor_save(editor_t *editor);

/**
 * Save the current file with a new name
 * @param editor Editor instance
 * @param filename New filename
 * @return 0 on success, negative error code on failure
 */
int editor_save_as(editor_t *editor, const char *filename);

/**
 * Create a new empty buffer
 * @param editor Editor instance
 */
void editor_new(editor_t *editor);

/*============================================================================
 * Text Editing Functions
 *============================================================================*/

/**
 * Insert a character at the current cursor position
 * @param editor Editor instance
 * @param c Character to insert
 */
void editor_insert_char(editor_t *editor, char c);

/**
 * Delete the character before the cursor (backspace)
 * @param editor Editor instance
 */
void editor_delete_char(editor_t *editor);

/**
 * Delete the character at the cursor position (delete key)
 * @param editor Editor instance
 */
void editor_delete_char_forward(editor_t *editor);

/**
 * Insert a newline at the current cursor position
 * @param editor Editor instance
 */
void editor_newline(editor_t *editor);

/**
 * Insert a tab at the current cursor position
 * @param editor Editor instance
 */
void editor_insert_tab(editor_t *editor);

/**
 * Delete the current line
 * @param editor Editor instance
 */
void editor_delete_line(editor_t *editor);

/*============================================================================
 * Cursor Movement Functions
 *============================================================================*/

/**
 * Move cursor by relative offset
 * @param editor Editor instance
 * @param dx Horizontal movement (negative = left, positive = right)
 * @param dy Vertical movement (negative = up, positive = down)
 */
void editor_move_cursor(editor_t *editor, int dx, int dy);

/**
 * Move cursor to start of current line
 * @param editor Editor instance
 */
void editor_home(editor_t *editor);

/**
 * Move cursor to end of current line
 * @param editor Editor instance
 */
void editor_end(editor_t *editor);

/**
 * Move cursor up one page
 * @param editor Editor instance
 */
void editor_page_up(editor_t *editor);

/**
 * Move cursor down one page
 * @param editor Editor instance
 */
void editor_page_down(editor_t *editor);

/**
 * Go to a specific line number
 * @param editor Editor instance
 * @param line Line number (1-based for user, converted to 0-based internally)
 */
void editor_goto_line(editor_t *editor, int line);

/**
 * Move cursor to start of document
 * @param editor Editor instance
 */
void editor_goto_start(editor_t *editor);

/**
 * Move cursor to end of document
 * @param editor Editor instance
 */
void editor_goto_end(editor_t *editor);

/*============================================================================
 * Display Functions
 *============================================================================*/

/**
 * Redraw the entire editor display
 * @param editor Editor instance
 */
void editor_draw(editor_t *editor);

/**
 * Draw the status bar
 * @param editor Editor instance
 */
void editor_draw_status(editor_t *editor);

/**
 * Update the cursor position on screen
 * @param editor Editor instance
 */
void editor_update_cursor(editor_t *editor);

/**
 * Set a status message to display
 * @param editor Editor instance
 * @param message Message to display
 */
void editor_set_status(editor_t *editor, const char *message);

/**
 * Scroll the view to ensure cursor is visible
 * @param editor Editor instance
 */
void editor_scroll_to_cursor(editor_t *editor);

/*============================================================================
 * Input Handling Functions
 *============================================================================*/

/**
 * Handle a keyboard event
 * @param editor Editor instance
 * @param key Keycode from keyboard driver
 * @param modifiers Modifier key state
 * @param ascii ASCII character (0 if non-printable)
 */
void editor_handle_key(editor_t *editor, int key, uint16_t modifiers, char ascii);

/**
 * Process keyboard input (called from main loop)
 * @param editor Editor instance
 */
void editor_process_input(editor_t *editor);

/*============================================================================
 * Search Functions
 *============================================================================*/

/**
 * Start search mode
 * @param editor Editor instance
 */
void editor_start_search(editor_t *editor);

/**
 * Find next occurrence of search query
 * @param editor Editor instance
 * @return true if found, false otherwise
 */
bool editor_find_next(editor_t *editor);

/**
 * Find previous occurrence of search query
 * @param editor Editor instance
 * @return true if found, false otherwise
 */
bool editor_find_prev(editor_t *editor);

/*============================================================================
 * Line Buffer Functions (Internal)
 *============================================================================*/

/**
 * Create a new line with given initial content
 * @param content Initial content (can be NULL for empty line)
 * @param length Length of content
 * @return Pointer to new line, or NULL on failure
 */
line_t* line_create(const char *content, size_t length);

/**
 * Destroy a line and free its memory
 * @param line Line to destroy
 */
void line_destroy(line_t *line);

/**
 * Ensure line has enough capacity
 * @param line Line to grow
 * @param needed_capacity Minimum capacity needed
 * @return true on success, false on failure
 */
bool line_ensure_capacity(line_t *line, size_t needed_capacity);

/**
 * Insert character into line at position
 * @param line Line to modify
 * @param pos Position to insert at
 * @param c Character to insert
 * @return true on success, false on failure
 */
bool line_insert_char(line_t *line, size_t pos, char c);

/**
 * Delete character from line at position
 * @param line Line to modify
 * @param pos Position to delete at
 */
void line_delete_char(line_t *line, size_t pos);

/**
 * Insert a new line into the editor at given position
 * @param editor Editor instance
 * @param pos Position to insert at (0 = before first line)
 * @param line Line to insert
 * @return true on success, false on failure
 */
bool editor_insert_line(editor_t *editor, size_t pos, line_t *line);

/**
 * Remove a line from the editor at given position
 * @param editor Editor instance
 * @param pos Position of line to remove
 */
void editor_remove_line(editor_t *editor, size_t pos);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Get the display width of a character (handles tabs)
 * @param c Character
 * @param col Current column position
 * @return Display width
 */
int editor_char_width(char c, int col);

/**
 * Convert screen column to buffer offset (handles tabs)
 * @param line Line to examine
 * @param screen_col Screen column
 * @return Buffer offset
 */
int editor_screen_to_offset(line_t *line, int screen_col);

/**
 * Convert buffer offset to screen column (handles tabs)
 * @param line Line to examine
 * @param offset Buffer offset
 * @return Screen column
 */
int editor_offset_to_screen(line_t *line, int offset);

#endif /* _AAAOS_APP_TEXT_EDITOR_H */
