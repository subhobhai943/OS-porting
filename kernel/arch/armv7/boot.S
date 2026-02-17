.section .text
.global _start
.type _start, %function

_start:
    ldr sp, =_stack_top
    bl kernel_main_armv7

1:
    wfi
    b 1b

.size _start, . - _start
