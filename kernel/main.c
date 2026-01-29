/**
 * AAAos Kernel - Main Entry Point
 *
 * This is the C entry point called from the assembly boot code.
 * It initializes all kernel subsystems and prepares the system for use.
 */

#include "include/types.h"
#include "include/boot.h"
#include "include/serial.h"
#include "include/vga.h"
#include "arch/x86_64/include/gdt.h"
#include "arch/x86_64/include/idt.h"

/* Kernel version */
#define KERNEL_VERSION_MAJOR    0
#define KERNEL_VERSION_MINOR    1
#define KERNEL_VERSION_PATCH    0
#define KERNEL_CODENAME         "Genesis"

/* External symbols from linker */
extern char _text_start, _text_end;
extern char _rodata_start, _rodata_end;
extern char _data_start, _data_end;
extern char _bss_start, _bss_end;
extern char _kernel_end;

/**
 * Print kernel banner
 */
static void print_banner(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("\n");
    vga_puts("    ___    ___    ___              \n");
    vga_puts("   /   |  /   |  /   |  ____  ___ \n");
    vga_puts("  / /| | / /| | / /| | / __ \\/ __/\n");
    vga_puts(" / ___ |/ ___ |/ ___ |/ /_/ /\\__ \\\n");
    vga_puts("/_/  |_/_/  |_/_/  |_|\\____//___/ \n");
    vga_puts("\n");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_printf("  AAAos Kernel v%d.%d.%d \"%s\"\n",
               KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR,
               KERNEL_VERSION_PATCH, KERNEL_CODENAME);
    vga_puts("  A General-Purpose Operating System\n");
    vga_puts("\n");

    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
}

/**
 * Print memory map from bootloader
 */
static void print_memory_map(boot_info_t *boot_info) {
    if (boot_info->mem_map_addr == 0 || boot_info->mem_map_count == 0) {
        kprintf("[MEM] No memory map available from bootloader\n");
        return;
    }

    memory_map_entry_t *entries = (memory_map_entry_t*)boot_info->mem_map_addr;
    uint64_t total_usable = 0;

    kprintf("[MEM] Memory Map (%llu entries):\n", boot_info->mem_map_count);

    for (uint64_t i = 0; i < boot_info->mem_map_count; i++) {
        const char *type_str;
        switch (entries[i].type) {
            case MEMORY_TYPE_USABLE:
                type_str = "Usable";
                total_usable += entries[i].length;
                break;
            case MEMORY_TYPE_RESERVED:
                type_str = "Reserved";
                break;
            case MEMORY_TYPE_ACPI_RECL:
                type_str = "ACPI Reclaimable";
                break;
            case MEMORY_TYPE_ACPI_NVS:
                type_str = "ACPI NVS";
                break;
            case MEMORY_TYPE_BAD:
                type_str = "Bad Memory";
                break;
            default:
                type_str = "Unknown";
                break;
        }

        kprintf("  %016llx - %016llx : %s (%llu KB)\n",
                entries[i].base,
                entries[i].base + entries[i].length - 1,
                type_str,
                entries[i].length / 1024);
    }

    kprintf("[MEM] Total usable: %llu MB\n", total_usable / (1024 * 1024));
}

/**
 * Print kernel section info
 */
static void print_kernel_info(void) {
    kprintf("[KERNEL] Section Layout:\n");
    kprintf("  .text:   %p - %p (%llu KB)\n",
            &_text_start, &_text_end,
            ((uint64_t)&_text_end - (uint64_t)&_text_start) / 1024);
    kprintf("  .rodata: %p - %p (%llu KB)\n",
            &_rodata_start, &_rodata_end,
            ((uint64_t)&_rodata_end - (uint64_t)&_rodata_start) / 1024);
    kprintf("  .data:   %p - %p (%llu KB)\n",
            &_data_start, &_data_end,
            ((uint64_t)&_data_end - (uint64_t)&_data_start) / 1024);
    kprintf("  .bss:    %p - %p (%llu KB)\n",
            &_bss_start, &_bss_end,
            ((uint64_t)&_bss_end - (uint64_t)&_bss_start) / 1024);
    kprintf("  Kernel end: %p\n", &_kernel_end);
}

/**
 * Simple keyboard handler for testing
 */
static void keyboard_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Read scancode from keyboard controller */
    uint8_t scancode = 0;
    __asm__ __volatile__("inb $0x60, %0" : "=a"(scancode));

    /* Only handle key presses (not releases) */
    if (scancode < 0x80) {
        /* Simple scancode to ASCII (US QWERTY, partial) */
        static const char scancode_to_ascii[] = {
            0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
            '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
            0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
            0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
            '*', 0, ' '
        };

        if (scancode < sizeof(scancode_to_ascii)) {
            char c = scancode_to_ascii[scancode];
            if (c != 0) {
                vga_putc(c);
                kprintf("%c", c);
            }
        }
    }
}

/**
 * Kernel main entry point
 * @param boot_info Pointer to boot information structure (passed in RDI)
 */
void kernel_main(boot_info_t *boot_info) {
    /* Initialize serial port for debugging */
    serial_init_com1();
    kprintf("\n");
    kprintf("=====================================\n");
    kprintf("AAAos Kernel Starting...\n");
    kprintf("=====================================\n");

    /* Initialize VGA console */
    vga_init();
    print_banner();

    /* Validate boot info */
    if (!boot_info_valid(boot_info)) {
        kprintf("[BOOT] Warning: Invalid boot info magic (got 0x%x)\n",
                boot_info ? boot_info->magic : 0);
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        vga_puts("Warning: Boot info not valid\n");
        vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    } else {
        kprintf("[BOOT] Boot info valid, framebuffer at 0x%llx\n",
                boot_info->framebuffer);
    }

    /* Initialize GDT */
    vga_puts("Initializing GDT...");
    gdt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(" OK\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    /* Initialize IDT */
    vga_puts("Initializing IDT...");
    idt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(" OK\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    /* Register keyboard handler */
    idt_register_handler(IRQ_KEYBOARD, keyboard_handler);

    /* Print kernel info */
    print_kernel_info();

    /* Print memory map if available */
    if (boot_info_valid(boot_info)) {
        print_memory_map(boot_info);
    }

    /* Enable interrupts */
    vga_puts("Enabling interrupts...");
    interrupts_enable();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts(" OK\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);

    /* Print ready message */
    vga_puts("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_puts("AAAos kernel initialized successfully!\n");
    vga_set_color(VGA_COLOR_LIGHT_GRAY, VGA_COLOR_BLACK);
    vga_puts("Type to test keyboard input (echoed to screen):\n");
    vga_puts("> ");

    kprintf("\n[KERNEL] Initialization complete. Entering idle loop.\n");
    kprintf("[KERNEL] Keyboard input will be echoed.\n");

    /* Kernel idle loop */
    for (;;) {
        /* Halt CPU until next interrupt */
        __asm__ __volatile__("hlt");
    }
}
