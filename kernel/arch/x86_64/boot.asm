; AAAos Kernel Entry Point
; This is the first code executed when the bootloader jumps to the kernel
; At this point we're already in 64-bit Long Mode

[BITS 64]

section .text.boot
global _start
extern kernel_main
extern _bss_start
extern _bss_end

_start:
    ; Save boot info pointer (passed in RDI)
    push rdi

    ; Clear BSS section
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, _bss_start
    shr rcx, 3                  ; Divide by 8 (qwords)
    xor rax, rax
    rep stosq

    ; Set up a proper stack
    mov rsp, stack_top

    ; Restore boot info pointer
    pop rdi

    ; Call C kernel main
    call kernel_main

    ; If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt

; Kernel stack (16KB)
section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
