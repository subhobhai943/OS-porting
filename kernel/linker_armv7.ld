ENTRY(_start)

SECTIONS
{
    . = 0x8000;

    .text : ALIGN(4) {
        _text_start = .;
        *(.text.boot)
        *(.text*)
        _text_end = .;
    }

    .rodata : ALIGN(4) {
        _rodata_start = .;
        *(.rodata*)
        _rodata_end = .;
    }

    .data : ALIGN(4) {
        _data_start = .;
        *(.data*)
        _data_end = .;
    }

    .bss : ALIGN(8) {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    . = ALIGN(8);
    . += 0x4000;
    _stack_top = .;

    _kernel_end = .;
}
