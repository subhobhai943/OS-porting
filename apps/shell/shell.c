/**
 * AAAos Kernel Shell - Implementation
 *
 * Provides a basic command-line interface for the kernel.
 */

#include "shell.h"
#include "../../kernel/include/vga.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/include/types.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/mm/pmm.h"
#include "../../drivers/input/keyboard.h"

/* ========== Forward Declarations ========== */
static void shell_add_to_history(const char *cmd);
static void shell_history_up(void);
static void shell_history_down(void);
static int shell_parse_args(char *cmdline, char *argv[], int max_args);
static size_t shell_strlen(const char *s);
static int shell_strcmp(const char *s1, const char *s2);
static char* shell_strcpy(char *dest, const char *src);
static void shell_memset(void *ptr, int value, size_t num);

/* ========== Global Shell State ========== */
static shell_state_t shell_state;

/* Maximum number of commands */
#define SHELL_MAX_COMMANDS  32

/* Command table */
static shell_command_t shell_commands[SHELL_MAX_COMMANDS];
static int shell_command_count = 0;

/* Tick counter for uptime (incremented by timer interrupt) */
static volatile uint64_t tick_count = 0;

/* ========== String Utility Functions ========== */

static size_t shell_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int shell_strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static int shell_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static char* shell_strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

static char* shell_strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*dest++ = *src++)) n--;
    while (n--) *dest++ = '\0';
    return ret;
}

static void shell_memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char*)ptr;
    while (num--) *p++ = (unsigned char)value;
}

static void shell_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
}

/* Skip leading whitespace */
static char* shell_skip_whitespace(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Check if character is whitespace */
static bool shell_is_whitespace(char c) {
    return c == ' ' || c == '\t';
}

/* ========== Built-in Commands ========== */

/* Built-in command descriptors */
static const shell_command_t builtin_commands[] = {
    {"help",     "Display available commands",           "[command]",    cmd_help},
    {"clear",    "Clear the screen",                     NULL,           cmd_clear},
    {"echo",     "Print arguments",                      "[args...]",    cmd_echo},
    {"uptime",   "Show system uptime",                   NULL,           cmd_uptime},
    {"mem",      "Show memory statistics",               NULL,           cmd_mem},
    {"ps",       "List processes",                       NULL,           cmd_ps},
    {"reboot",   "Reboot the system",                    NULL,           cmd_reboot},
    {"shutdown", "Halt the system",                      NULL,           cmd_shutdown},
    {"version",  "Show OS version",                      NULL,           cmd_version},
    {"date",     "Show current date/time",               NULL,           cmd_date},
    {"cpuinfo",  "Show CPU information",                 NULL,           cmd_cpuinfo},
    {NULL, NULL, NULL, NULL}  /* Sentinel */
};

int cmd_help(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    if (argc > 1) {
        /* Show help for specific command */
        const shell_command_t *cmd = shell_find_command(argv[1]);
        if (cmd) {
            vga_printf("%s - %s\n", cmd->name, cmd->help);
            if (cmd->usage) {
                vga_printf("Usage: %s %s\n", cmd->name, cmd->usage);
            }
        } else {
            vga_printf("Unknown command: %s\n", argv[1]);
            return 1;
        }
    } else {
        /* Show all commands */
        vga_puts("Available commands:\n");
        vga_puts("-------------------\n");

        for (int i = 0; i < shell_command_count; i++) {
            vga_printf("  %-12s %s\n",
                       shell_commands[i].name,
                       shell_commands[i].help);
        }

        vga_puts("\nType 'help <command>' for more information.\n");
    }

    return 0;
}

int cmd_clear(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    vga_clear();
    kprintf("[SHELL] Screen cleared\n");

    return 0;
}

int cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        vga_puts(argv[i]);
        if (i < argc - 1) {
            vga_putc(' ');
        }
    }
    vga_putc('\n');

    return 0;
}

int cmd_uptime(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    /* Read PIT ticks for approximate uptime */
    /* Note: In a real implementation, we'd have a timer driver */
    /* For now, we use the tick counter if available */

    uint64_t ticks = tick_count;
    uint64_t seconds = ticks / 100;  /* Assuming 100 Hz timer */
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;

    vga_printf("System uptime: ");
    if (hours > 0) {
        vga_printf("%llu hours, ", hours);
    }
    if (minutes > 0 || hours > 0) {
        vga_printf("%llu minutes, ", minutes % 60);
    }
    vga_printf("%llu seconds\n", seconds % 60);

    kprintf("[SHELL] uptime: %llu ticks (%llu seconds)\n", ticks, seconds);

    return 0;
}

int cmd_mem(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    size_t total_pages = pmm_get_total_pages();
    size_t free_pages = pmm_get_free_pages();
    size_t used_pages = pmm_get_used_pages();

    size_t total_kb = (total_pages * PAGE_SIZE) / KB;
    size_t free_kb = (free_pages * PAGE_SIZE) / KB;
    size_t used_kb = (used_pages * PAGE_SIZE) / KB;

    vga_puts("Memory Statistics:\n");
    vga_puts("------------------\n");
    vga_printf("Total Memory:  %llu KB (%llu pages)\n", total_kb, total_pages);
    vga_printf("Used Memory:   %llu KB (%llu pages)\n", used_kb, used_pages);
    vga_printf("Free Memory:   %llu KB (%llu pages)\n", free_kb, free_pages);

    if (total_pages > 0) {
        uint32_t percent_used = (used_pages * 100) / total_pages;
        vga_printf("Usage:         %u%%\n", percent_used);
    }

    kprintf("[SHELL] mem: total=%llu free=%llu used=%llu pages\n",
            total_pages, free_pages, used_pages);

    return 0;
}

int cmd_ps(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    /* In a full OS, this would list actual processes */
    /* For now, show placeholder information */

    vga_puts("Process List:\n");
    vga_puts("-------------\n");
    vga_puts("  PID  STATE   NAME\n");
    vga_puts("    0  Running kernel\n");
    vga_puts("    1  Running shell\n");
    vga_puts("\n(Process scheduler not yet implemented)\n");

    kprintf("[SHELL] ps: listed processes\n");

    return 0;
}

int cmd_reboot(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("Rebooting system...\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    kprintf("[SHELL] Initiating system reboot\n");

    /* Disable interrupts */
    interrupts_disable();

    /* Method 1: 8042 keyboard controller reset */
    /* Wait for keyboard controller to be ready */
    uint8_t status;
    do {
        status = inb(0x64);
    } while (status & 0x02);

    /* Send reset command */
    outb(0x64, 0xFE);

    /* If that didn't work, try triple fault */
    /* Load an invalid IDT and trigger interrupt */
    struct {
        uint16_t limit;
        uint64_t base;
    } PACKED null_idt = {0, 0};

    __asm__ __volatile__(
        "lidt %0\n"
        "int $0x03\n"
        :
        : "m"(null_idt)
    );

    /* Should never reach here */
    for (;;) {
        __asm__ __volatile__("hlt");
    }

    return 0;
}

int cmd_shutdown(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("System is shutting down...\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    kprintf("[SHELL] Initiating system shutdown\n");

    /* Disable interrupts */
    interrupts_disable();

    /* Method 1: ACPI shutdown (if available) */
    /* For QEMU, writing to port 0x604 can trigger shutdown */
    outw(0x604, 0x2000);

    /* Method 2: APM shutdown (older systems) */
    /* APM real mode interface - this won't work in long mode directly */

    /* Method 3: Just halt the CPU */
    vga_puts("\nSystem halted. You may now turn off your computer.\n");
    kprintf("[SHELL] System halted\n");

    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }

    return 0;
}

int cmd_version(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("\n");
    vga_puts("    ___    ___    ___              \n");
    vga_puts("   /   |  /   |  /   |  ____  ___ \n");
    vga_puts("  / /| | / /| | / /| | / __ \\/ __/\n");
    vga_puts(" / ___ |/ ___ |/ ___ |/ /_/ /\\__ \\\n");
    vga_puts("/_/  |_/_/  |_/_/  |_|\\____//___/ \n");
    vga_puts("\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /* Kernel version from main.c */
    vga_puts("AAAos Kernel v0.1.0 \"Genesis\"\n");
    vga_printf("Shell v%d.%d.%d\n",
               SHELL_VERSION_MAJOR, SHELL_VERSION_MINOR, SHELL_VERSION_PATCH);
    vga_puts("\n");

    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    vga_puts("A General-Purpose Operating System\n");
    vga_puts("Built with GCC for x86_64\n");

    kprintf("[SHELL] version info displayed\n");

    return 0;
}

int cmd_date(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    /* Read date/time from CMOS RTC */
    /* RTC registers are accessed via ports 0x70 (index) and 0x71 (data) */

    /* Wait for RTC update to complete */
    do {
        outb(0x70, 0x0A);
    } while (inb(0x71) & 0x80);

    /* Read time values */
    outb(0x70, 0x00);
    uint8_t seconds = inb(0x71);

    outb(0x70, 0x02);
    uint8_t minutes = inb(0x71);

    outb(0x70, 0x04);
    uint8_t hours = inb(0x71);

    /* Read date values */
    outb(0x70, 0x07);
    uint8_t day = inb(0x71);

    outb(0x70, 0x08);
    uint8_t month = inb(0x71);

    outb(0x70, 0x09);
    uint8_t year = inb(0x71);

    /* Read century if available (CMOS register 0x32 on some systems) */
    outb(0x70, 0x32);
    uint8_t century = inb(0x71);

    /* Check if RTC is in BCD mode */
    outb(0x70, 0x0B);
    uint8_t status_b = inb(0x71);

    bool bcd_mode = !(status_b & 0x04);

    /* Convert BCD to binary if needed */
    if (bcd_mode) {
        #define BCD_TO_BIN(val) ((val) = ((val) & 0x0F) + ((val) / 16) * 10)
        BCD_TO_BIN(seconds);
        BCD_TO_BIN(minutes);
        BCD_TO_BIN(hours);
        BCD_TO_BIN(day);
        BCD_TO_BIN(month);
        BCD_TO_BIN(year);
        BCD_TO_BIN(century);
        #undef BCD_TO_BIN
    }

    /* Calculate full year */
    uint32_t full_year;
    if (century > 0 && century < 100) {
        full_year = century * 100 + year;
    } else {
        /* Assume 2000s if century not available */
        full_year = 2000 + year;
    }

    /* Day of week names */
    static const char* days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };

    /* Month names */
    static const char* months[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    /* Calculate day of week (Zeller's formula simplified) */
    int dow = 0;  /* Placeholder - actual calculation is complex */

    if (month >= 1 && month <= 12) {
        vga_printf("%s %u, %u\n", months[month], day, full_year);
    } else {
        vga_printf("%02u/%02u/%u\n", month, day, full_year);
    }
    vga_printf("%02u:%02u:%02u\n", hours, minutes, seconds);

    kprintf("[SHELL] date: %u-%02u-%02u %02u:%02u:%02u\n",
            full_year, month, day, hours, minutes, seconds);

    UNUSED(dow);
    UNUSED(days);

    return 0;
}

int cmd_cpuinfo(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    /* Use CPUID instruction to get CPU information */
    uint32_t eax, ebx, ecx, edx;

    vga_puts("CPU Information:\n");
    vga_puts("----------------\n");

    /* CPUID function 0: Get vendor string */
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );

    char vendor[13];
    *(uint32_t*)(vendor + 0) = ebx;
    *(uint32_t*)(vendor + 4) = edx;
    *(uint32_t*)(vendor + 8) = ecx;
    vendor[12] = '\0';

    vga_printf("Vendor:   %s\n", vendor);

    uint32_t max_cpuid = eax;

    /* CPUID function 1: Get feature information */
    if (max_cpuid >= 1) {
        __asm__ __volatile__(
            "cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(1)
        );

        uint32_t stepping = eax & 0xF;
        uint32_t model = (eax >> 4) & 0xF;
        uint32_t family = (eax >> 8) & 0xF;
        uint32_t ext_model = (eax >> 16) & 0xF;
        uint32_t ext_family = (eax >> 20) & 0xFF;

        /* Calculate display model and family */
        uint32_t display_family = family;
        uint32_t display_model = model;
        if (family == 0xF) {
            display_family = family + ext_family;
        }
        if (family == 0x6 || family == 0xF) {
            display_model = model + (ext_model << 4);
        }

        vga_printf("Family:   %u\n", display_family);
        vga_printf("Model:    %u\n", display_model);
        vga_printf("Stepping: %u\n", stepping);

        /* Feature flags */
        vga_puts("Features: ");

        if (edx & (1 << 0))  vga_puts("FPU ");
        if (edx & (1 << 4))  vga_puts("TSC ");
        if (edx & (1 << 5))  vga_puts("MSR ");
        if (edx & (1 << 6))  vga_puts("PAE ");
        if (edx & (1 << 9))  vga_puts("APIC ");
        if (edx & (1 << 15)) vga_puts("CMOV ");
        if (edx & (1 << 23)) vga_puts("MMX ");
        if (edx & (1 << 24)) vga_puts("FXSR ");
        if (edx & (1 << 25)) vga_puts("SSE ");
        if (edx & (1 << 26)) vga_puts("SSE2 ");

        if (ecx & (1 << 0))  vga_puts("SSE3 ");
        if (ecx & (1 << 9))  vga_puts("SSSE3 ");
        if (ecx & (1 << 19)) vga_puts("SSE4.1 ");
        if (ecx & (1 << 20)) vga_puts("SSE4.2 ");
        if (ecx & (1 << 28)) vga_puts("AVX ");

        vga_puts("\n");
    }

    /* Extended CPUID: Get brand string */
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000000)
    );

    uint32_t max_ext_cpuid = eax;

    if (max_ext_cpuid >= 0x80000004) {
        char brand[49];
        shell_memset(brand, 0, sizeof(brand));

        /* Functions 0x80000002 - 0x80000004 return brand string */
        for (uint32_t i = 0; i < 3; i++) {
            __asm__ __volatile__(
                "cpuid"
                : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                : "a"(0x80000002 + i)
            );

            *(uint32_t*)(brand + i * 16 + 0) = eax;
            *(uint32_t*)(brand + i * 16 + 4) = ebx;
            *(uint32_t*)(brand + i * 16 + 8) = ecx;
            *(uint32_t*)(brand + i * 16 + 12) = edx;
        }

        /* Skip leading spaces */
        char *brand_str = brand;
        while (*brand_str == ' ') brand_str++;

        vga_printf("Brand:    %s\n", brand_str);
    }

    kprintf("[SHELL] cpuinfo: vendor=%s\n", vendor);

    return 0;
}

/* ========== Shell Core Functions ========== */

void shell_init(void) {
    kprintf("[SHELL] Initializing kernel shell\n");

    /* Initialize shell state */
    shell_memset(&shell_state, 0, sizeof(shell_state_t));
    shell_state.running = true;
    shell_state.echo_enabled = true;

    /* Register built-in commands */
    shell_command_count = 0;
    for (int i = 0; builtin_commands[i].name != NULL; i++) {
        if (shell_register_command(&builtin_commands[i]) < 0) {
            kprintf("[SHELL] Warning: Failed to register command '%s'\n",
                    builtin_commands[i].name);
        }
    }

    kprintf("[SHELL] Registered %d commands\n", shell_command_count);
}

int shell_register_command(const shell_command_t *cmd) {
    if (shell_command_count >= SHELL_MAX_COMMANDS) {
        return -1;
    }

    shell_memcpy(&shell_commands[shell_command_count], cmd, sizeof(shell_command_t));
    shell_command_count++;

    return 0;
}

const shell_command_t* shell_find_command(const char *name) {
    for (int i = 0; i < shell_command_count; i++) {
        if (shell_strcmp(shell_commands[i].name, name) == 0) {
            return &shell_commands[i];
        }
    }
    return NULL;
}

int shell_get_command_count(void) {
    return shell_command_count;
}

const shell_command_t* shell_get_command(int index) {
    if (index < 0 || index >= shell_command_count) {
        return NULL;
    }
    return &shell_commands[index];
}

void shell_print_prompt(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(SHELL_PROMPT);
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
}

static void shell_add_to_history(const char *cmd) {
    /* Don't add empty commands */
    if (cmd[0] == '\0') {
        return;
    }

    /* Don't add duplicate of last command */
    if (shell_state.history_count > 0 &&
        shell_strcmp(shell_state.history[shell_state.history_count - 1], cmd) == 0) {
        return;
    }

    /* Add command to history */
    if (shell_state.history_count < SHELL_HISTORY_SIZE) {
        shell_strcpy(shell_state.history[shell_state.history_count], cmd);
        shell_state.history_count++;
    } else {
        /* Shift history up and add at end */
        for (int i = 0; i < SHELL_HISTORY_SIZE - 1; i++) {
            shell_strcpy(shell_state.history[i], shell_state.history[i + 1]);
        }
        shell_strcpy(shell_state.history[SHELL_HISTORY_SIZE - 1], cmd);
    }

    /* Reset history index */
    shell_state.history_index = shell_state.history_count;
}

static void shell_clear_input_line(void) {
    /* Move cursor to start of input and clear */
    int prompt_len = shell_strlen(SHELL_PROMPT);
    int x = vga_get_cursor_x();
    int y = vga_get_cursor_y();

    /* Calculate start of input */
    int input_start_x = prompt_len;

    /* Clear input area */
    vga_set_cursor(input_start_x, y);
    for (size_t i = 0; i < shell_state.input_len; i++) {
        vga_putc(' ');
    }

    /* Reset cursor */
    vga_set_cursor(input_start_x, y);
}

static void shell_redraw_input(void) {
    shell_clear_input_line();

    /* Redraw current input */
    for (size_t i = 0; i < shell_state.input_len; i++) {
        vga_putc(shell_state.input_buffer[i]);
    }

    /* Move cursor to correct position */
    int prompt_len = shell_strlen(SHELL_PROMPT);
    int y = vga_get_cursor_y();
    vga_set_cursor(prompt_len + shell_state.input_pos, y);
}

static void shell_history_up(void) {
    if (shell_state.history_index > 0) {
        shell_state.history_index--;

        /* Copy history entry to input buffer */
        shell_strcpy(shell_state.input_buffer,
                     shell_state.history[shell_state.history_index]);
        shell_state.input_len = shell_strlen(shell_state.input_buffer);
        shell_state.input_pos = shell_state.input_len;

        shell_redraw_input();
    }
}

static void shell_history_down(void) {
    if (shell_state.history_index < shell_state.history_count) {
        shell_state.history_index++;

        if (shell_state.history_index < shell_state.history_count) {
            /* Copy history entry to input buffer */
            shell_strcpy(shell_state.input_buffer,
                         shell_state.history[shell_state.history_index]);
            shell_state.input_len = shell_strlen(shell_state.input_buffer);
            shell_state.input_pos = shell_state.input_len;
        } else {
            /* Clear input */
            shell_state.input_buffer[0] = '\0';
            shell_state.input_len = 0;
            shell_state.input_pos = 0;
        }

        shell_redraw_input();
    }
}

int shell_readline(char *buf, size_t max) {
    /* Initialize input state */
    shell_state.input_buffer[0] = '\0';
    shell_state.input_len = 0;
    shell_state.input_pos = 0;
    shell_state.history_index = shell_state.history_count;

    key_event_t event;

    while (1) {
        /* Wait for key event */
        if (!keyboard_get_event(&event)) {
            continue;
        }

        /* Only process key presses, not releases */
        if (!event.pressed) {
            continue;
        }

        /* Handle special keys */
        switch (event.keycode) {
            case KEY_ENTER:
                /* End of line */
                vga_putc('\n');
                shell_strncpy(buf, shell_state.input_buffer,
                              MIN(shell_state.input_len + 1, max));
                buf[max - 1] = '\0';
                return shell_state.input_len;

            case KEY_BACKSPACE:
                if (shell_state.input_pos > 0) {
                    /* Delete character before cursor */
                    shell_state.input_pos--;
                    shell_state.input_len--;

                    /* Shift characters left */
                    for (size_t i = shell_state.input_pos; i < shell_state.input_len; i++) {
                        shell_state.input_buffer[i] = shell_state.input_buffer[i + 1];
                    }
                    shell_state.input_buffer[shell_state.input_len] = '\0';

                    /* Redraw */
                    vga_putc('\b');
                    int saved_x = vga_get_cursor_x();
                    int saved_y = vga_get_cursor_y();
                    for (size_t i = shell_state.input_pos; i < shell_state.input_len; i++) {
                        vga_putc(shell_state.input_buffer[i]);
                    }
                    vga_putc(' ');  /* Clear last character */
                    vga_set_cursor(saved_x, saved_y);
                }
                break;

            case KEY_DELETE:
                if (shell_state.input_pos < shell_state.input_len) {
                    /* Delete character at cursor */
                    shell_state.input_len--;

                    /* Shift characters left */
                    for (size_t i = shell_state.input_pos; i < shell_state.input_len; i++) {
                        shell_state.input_buffer[i] = shell_state.input_buffer[i + 1];
                    }
                    shell_state.input_buffer[shell_state.input_len] = '\0';

                    /* Redraw */
                    int saved_x = vga_get_cursor_x();
                    int saved_y = vga_get_cursor_y();
                    for (size_t i = shell_state.input_pos; i < shell_state.input_len; i++) {
                        vga_putc(shell_state.input_buffer[i]);
                    }
                    vga_putc(' ');
                    vga_set_cursor(saved_x, saved_y);
                }
                break;

            case KEY_LEFT:
                if (shell_state.input_pos > 0) {
                    shell_state.input_pos--;
                    int x = vga_get_cursor_x();
                    int y = vga_get_cursor_y();
                    if (x > 0) {
                        vga_set_cursor(x - 1, y);
                    }
                }
                break;

            case KEY_RIGHT:
                if (shell_state.input_pos < shell_state.input_len) {
                    shell_state.input_pos++;
                    int x = vga_get_cursor_x();
                    int y = vga_get_cursor_y();
                    vga_set_cursor(x + 1, y);
                }
                break;

            case KEY_UP:
                shell_history_up();
                break;

            case KEY_DOWN:
                shell_history_down();
                break;

            case KEY_HOME:
                shell_state.input_pos = 0;
                {
                    int prompt_len = shell_strlen(SHELL_PROMPT);
                    int y = vga_get_cursor_y();
                    vga_set_cursor(prompt_len, y);
                }
                break;

            case KEY_END:
                shell_state.input_pos = shell_state.input_len;
                {
                    int prompt_len = shell_strlen(SHELL_PROMPT);
                    int y = vga_get_cursor_y();
                    vga_set_cursor(prompt_len + shell_state.input_len, y);
                }
                break;

            case KEY_ESCAPE:
                /* Clear current input */
                shell_clear_input_line();
                shell_state.input_buffer[0] = '\0';
                shell_state.input_len = 0;
                shell_state.input_pos = 0;
                shell_state.history_index = shell_state.history_count;
                break;

            default:
                /* Handle printable characters */
                if (event.ascii != 0 && event.ascii >= 32 && event.ascii < 127) {
                    if (shell_state.input_len < SHELL_MAX_INPUT - 1) {
                        /* Insert character at cursor position */
                        for (size_t i = shell_state.input_len; i > shell_state.input_pos; i--) {
                            shell_state.input_buffer[i] = shell_state.input_buffer[i - 1];
                        }
                        shell_state.input_buffer[shell_state.input_pos] = event.ascii;
                        shell_state.input_pos++;
                        shell_state.input_len++;
                        shell_state.input_buffer[shell_state.input_len] = '\0';

                        /* Redraw from cursor position */
                        int saved_x = vga_get_cursor_x();
                        int saved_y = vga_get_cursor_y();
                        for (size_t i = shell_state.input_pos - 1; i < shell_state.input_len; i++) {
                            vga_putc(shell_state.input_buffer[i]);
                        }
                        vga_set_cursor(saved_x + 1, saved_y);
                    }
                }
                break;
        }
    }

    return -1;
}

static int shell_parse_args(char *cmdline, char *argv[], int max_args) {
    int argc = 0;
    char *p = cmdline;

    while (*p && argc < max_args) {
        /* Skip leading whitespace */
        p = shell_skip_whitespace(p);
        if (*p == '\0') break;

        /* Check for quoted argument */
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;

            /* Find closing quote */
            while (*p && *p != quote) p++;
            if (*p) *p++ = '\0';
        } else {
            /* Regular argument */
            argv[argc++] = p;

            /* Find end of argument */
            while (*p && !shell_is_whitespace(*p)) p++;
            if (*p) *p++ = '\0';
        }
    }

    return argc;
}

int shell_execute(const char *cmdline) {
    /* Copy command line (we need to modify it for parsing) */
    char cmd_copy[SHELL_MAX_INPUT];
    shell_strncpy(cmd_copy, cmdline, SHELL_MAX_INPUT - 1);
    cmd_copy[SHELL_MAX_INPUT - 1] = '\0';

    /* Skip leading whitespace */
    char *cmd = shell_skip_whitespace(cmd_copy);

    /* Empty command */
    if (*cmd == '\0') {
        return 0;
    }

    /* Parse arguments */
    char *argv[SHELL_MAX_ARGS];
    int argc = shell_parse_args(cmd, argv, SHELL_MAX_ARGS);

    if (argc == 0) {
        return 0;
    }

    kprintf("[SHELL] Executing: %s\n", argv[0]);

    /* Look up command */
    const shell_command_t *cmd_entry = shell_find_command(argv[0]);
    if (cmd_entry == NULL) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("Unknown command: %s\n", argv[0]);
        vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
        vga_puts("Type 'help' for a list of commands.\n");
        return -1;
    }

    /* Execute command */
    int result = cmd_entry->handler(argc, argv);

    if (result != 0) {
        kprintf("[SHELL] Command '%s' returned %d\n", argv[0], result);
    }

    return result;
}

void shell_run(void) {
    char input[SHELL_MAX_INPUT];

    kprintf("[SHELL] Starting shell main loop\n");

    vga_puts("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("AAAos Kernel Shell v");
    vga_printf("%d.%d.%d\n", SHELL_VERSION_MAJOR, SHELL_VERSION_MINOR, SHELL_VERSION_PATCH);
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    vga_puts("Type 'help' for a list of commands.\n\n");

    while (shell_state.running) {
        /* Print prompt */
        shell_print_prompt();

        /* Read input */
        int len = shell_readline(input, SHELL_MAX_INPUT);

        if (len < 0) {
            kprintf("[SHELL] Read error\n");
            continue;
        }

        /* Add to history */
        shell_add_to_history(input);

        /* Execute command */
        shell_execute(input);
    }

    kprintf("[SHELL] Shell exiting\n");
}

/* ========== Timer Tick Handler ========== */

/**
 * Timer tick handler for uptime tracking
 * Should be called from timer interrupt handler
 */
void shell_timer_tick(void) {
    tick_count++;
}

/**
 * Get current tick count
 */
uint64_t shell_get_ticks(void) {
    return tick_count;
}
