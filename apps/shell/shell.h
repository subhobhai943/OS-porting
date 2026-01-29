/**
 * AAAos Kernel Shell
 *
 * A basic command-line shell for the AAAos kernel.
 * Provides built-in commands for system interaction and debugging.
 */

#ifndef _AAAOS_SHELL_H
#define _AAAOS_SHELL_H

#include "../../kernel/include/types.h"

/* Shell configuration */
#define SHELL_MAX_INPUT         256     /* Maximum input line length */
#define SHELL_MAX_ARGS          16      /* Maximum number of arguments */
#define SHELL_HISTORY_SIZE      16      /* Number of commands to remember */
#define SHELL_PROMPT            "aaos> " /* Shell prompt */

/* Shell version */
#define SHELL_VERSION_MAJOR     1
#define SHELL_VERSION_MINOR     0
#define SHELL_VERSION_PATCH     0

/**
 * Shell command handler function type
 * @param argc Number of arguments (including command name)
 * @param argv Array of argument strings
 * @return 0 on success, non-zero on error
 */
typedef int (*shell_cmd_handler_t)(int argc, char *argv[]);

/**
 * Shell command descriptor
 */
typedef struct {
    const char *name;           /* Command name */
    const char *help;           /* Help text */
    const char *usage;          /* Usage string (optional) */
    shell_cmd_handler_t handler;/* Command handler function */
} shell_command_t;

/**
 * Shell state structure
 */
typedef struct {
    char input_buffer[SHELL_MAX_INPUT];     /* Current input line */
    size_t input_pos;                        /* Cursor position in input */
    size_t input_len;                        /* Length of current input */

    char history[SHELL_HISTORY_SIZE][SHELL_MAX_INPUT]; /* Command history */
    int history_count;                       /* Number of commands in history */
    int history_index;                       /* Current history position */

    bool running;                            /* Shell is running */
    bool echo_enabled;                       /* Echo input to screen */
} shell_state_t;

/**
 * Initialize the kernel shell
 * Sets up internal state and registers built-in commands.
 */
void shell_init(void);

/**
 * Run the shell main loop
 * This is the read-eval-print loop that processes user input.
 * Does not return until shell is exited.
 */
void shell_run(void);

/**
 * Execute a command line
 * Parses the command and arguments, then invokes the appropriate handler.
 * @param cmdline The command line to execute
 * @return 0 on success, non-zero on error
 */
int shell_execute(const char *cmdline);

/**
 * Read a line of input from the keyboard
 * Handles line editing (backspace, etc.) and arrow keys for history.
 * @param buf Buffer to store the input
 * @param max Maximum number of characters to read
 * @return Number of characters read, or -1 on error
 */
int shell_readline(char *buf, size_t max);

/**
 * Print shell prompt
 */
void shell_print_prompt(void);

/**
 * Register a custom command
 * @param cmd Command descriptor
 * @return 0 on success, -1 if command table is full
 */
int shell_register_command(const shell_command_t *cmd);

/**
 * Look up a command by name
 * @param name Command name to find
 * @return Pointer to command descriptor, or NULL if not found
 */
const shell_command_t* shell_find_command(const char *name);

/**
 * Get the number of registered commands
 * @return Number of commands
 */
int shell_get_command_count(void);

/**
 * Get command at index
 * @param index Index of command
 * @return Pointer to command descriptor, or NULL if index out of range
 */
const shell_command_t* shell_get_command(int index);

/* ========== Built-in Command Handlers ========== */

/**
 * help - Display list of available commands
 */
int cmd_help(int argc, char *argv[]);

/**
 * clear - Clear the screen
 */
int cmd_clear(int argc, char *argv[]);

/**
 * echo - Print arguments to screen
 */
int cmd_echo(int argc, char *argv[]);

/**
 * uptime - Show system uptime
 */
int cmd_uptime(int argc, char *argv[]);

/**
 * mem - Show memory statistics
 */
int cmd_mem(int argc, char *argv[]);

/**
 * ps - List processes
 */
int cmd_ps(int argc, char *argv[]);

/**
 * reboot - Reboot the system
 */
int cmd_reboot(int argc, char *argv[]);

/**
 * shutdown - Halt the system
 */
int cmd_shutdown(int argc, char *argv[]);

/**
 * version - Show OS version
 */
int cmd_version(int argc, char *argv[]);

/**
 * date - Show current date/time
 */
int cmd_date(int argc, char *argv[]);

/**
 * cpuinfo - Show CPU information
 */
int cmd_cpuinfo(int argc, char *argv[]);

#endif /* _AAAOS_SHELL_H */
