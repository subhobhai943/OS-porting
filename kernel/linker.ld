/* AAAos Kernel Linker Script */
/* Kernel is loaded at 1MB (0x100000) in physical memory */
/* Virtual address starts at higher half: 0xFFFFFFFF80000000 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_PHYS_BASE = 0x100000;
KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
    /* Start at 1MB physical */
    . = KERNEL_PHYS_BASE;

    /* Text section - executable code */
    .text ALIGN(4K) : AT(ADDR(.text))
    {
        _text_start = .;
        *(.multiboot)
        *(.text.boot)
        *(.text .text.*)
        _text_end = .;
    }

    /* Read-only data */
    .rodata ALIGN(4K) : AT(ADDR(.rodata))
    {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }

    /* Initialized data */
    .data ALIGN(4K) : AT(ADDR(.data))
    {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    /* Uninitialized data (BSS) */
    .bss ALIGN(4K) : AT(ADDR(.bss))
    {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    /* Kernel end marker */
    . = ALIGN(4K);
    _kernel_end = .;

    /* Calculate kernel size */
    _kernel_size = _kernel_end - KERNEL_PHYS_BASE;

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
