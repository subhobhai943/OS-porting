; AAAos Kernel - GDT Assembly Helpers
; Load GDT and update segment registers

[BITS 64]

section .text

; void gdt_load(gdt_descriptor_t *descriptor, uint16_t code_seg, uint16_t data_seg)
global gdt_load
gdt_load:
    ; RDI = pointer to GDT descriptor
    ; RSI = code segment selector
    ; RDX = data segment selector

    ; Load GDTR
    lgdt [rdi]

    ; Update data segment registers
    mov ax, dx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far return to update CS
    ; Push new code segment and return address
    pop rax                     ; Get return address
    push rsi                    ; Push code segment
    push rax                    ; Push return address
    retfq                       ; Far return (updates CS)
