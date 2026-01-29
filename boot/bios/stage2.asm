; AAAos Stage 2 Bootloader
; Responsibilities:
; 1. Enable A20 line
; 2. Get memory map from BIOS
; 3. Enter Protected Mode (32-bit)
; 4. Set up paging for Long Mode
; 5. Enter Long Mode (64-bit)
; 6. Load and jump to kernel

[BITS 16]
[ORG 0x7E00]

; Constants
KERNEL_LOAD_ADDR    equ 0x100000    ; 1MB - where kernel will be loaded
KERNEL_START_SECTOR equ 9           ; Kernel starts at sector 10 (LBA 9)
KERNEL_SECTORS      equ 256         ; Load up to 128KB of kernel
PAGE_PRESENT        equ (1 << 0)
PAGE_WRITE          equ (1 << 1)
PAGE_SIZE           equ (1 << 7)    ; 2MB pages

stage2_start:
    ; Print Stage 2 banner
    mov si, msg_stage2
    call print_string_16

    ; Enable A20 line
    call enable_a20

    ; Get memory map from BIOS
    call get_memory_map

    ; Load kernel to temporary location (below 1MB first, then copy)
    call load_kernel

    ; Print entering protected mode message
    mov si, msg_pmode
    call print_string_16

    ; Disable interrupts for mode switch
    cli

    ; Load GDT
    lgdt [gdt32_descriptor]

    ; Enter Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp 0x08:protected_mode_entry

;-----------------------------------------
; 16-bit helper functions
;-----------------------------------------

; Print string in real mode
print_string_16:
    pusha
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Enable A20 line using multiple methods
enable_a20:
    ; Try BIOS method first
    mov ax, 0x2401
    int 0x15
    jnc .done

    ; Try keyboard controller method
    call .wait_input
    mov al, 0xAD
    out 0x64, al
    call .wait_input
    mov al, 0xD0
    out 0x64, al
    call .wait_output
    in al, 0x60
    push ax
    call .wait_input
    mov al, 0xD1
    out 0x64, al
    call .wait_input
    pop ax
    or al, 2
    out 0x60, al
    call .wait_input
    mov al, 0xAE
    out 0x64, al
    call .wait_input

.done:
    mov si, msg_a20
    call print_string_16
    ret

.wait_input:
    in al, 0x64
    test al, 2
    jnz .wait_input
    ret

.wait_output:
    in al, 0x64
    test al, 1
    jz .wait_output
    ret

; Get memory map using INT 15h, E820h
get_memory_map:
    mov si, msg_memmap
    call print_string_16

    mov di, memory_map          ; Destination buffer
    xor ebx, ebx                ; Continuation value (0 for first call)
    xor bp, bp                  ; Entry counter

.loop:
    mov eax, 0xE820
    mov ecx, 24                 ; Buffer size
    mov edx, 0x534D4150         ; 'SMAP' signature
    int 0x15

    jc .done                    ; Carry = error or end
    cmp eax, 0x534D4150         ; Check signature returned
    jne .done

    inc bp                      ; Count entries
    add di, 24                  ; Next entry

    test ebx, ebx               ; Is continuation 0?
    jz .done
    jmp .loop

.done:
    mov [memory_map_entries], bp
    ret

; Load kernel from disk
load_kernel:
    mov si, msg_loading_kernel
    call print_string_16

    ; Use INT 13h extensions to load kernel
    ; We'll load to 0x10000 (64KB) temporarily, then copy in protected mode
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, kernel_dap
    int 0x13
    jc .error

    mov si, msg_kernel_loaded
    call print_string_16
    ret

.error:
    mov si, msg_kernel_error
    call print_string_16
    jmp $

;-----------------------------------------
; 32-bit Protected Mode
;-----------------------------------------
[BITS 32]

protected_mode_entry:
    ; Set up segment registers for 32-bit mode
    mov ax, 0x10                ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000            ; Stack at 576KB

    ; Copy kernel from temporary location to 1MB
    mov esi, 0x10000            ; Source
    mov edi, KERNEL_LOAD_ADDR   ; Destination (1MB)
    mov ecx, (KERNEL_SECTORS * 512) / 4  ; Size in dwords
    rep movsd

    ; Set up paging for Long Mode
    ; We'll use 2MB pages for simplicity

    ; Clear page tables area
    mov edi, 0x1000             ; PML4 at 0x1000
    xor eax, eax
    mov ecx, 0x5000 / 4         ; Clear 20KB
    rep stosd

    ; PML4[0] -> PDPT at 0x2000
    mov edi, 0x1000
    mov eax, 0x2000 | PAGE_PRESENT | PAGE_WRITE
    mov [edi], eax

    ; PML4[511] -> same PDPT (for higher half kernel mapping)
    mov eax, 0x2000 | PAGE_PRESENT | PAGE_WRITE
    mov [edi + 511 * 8], eax

    ; PDPT[0] -> PD at 0x3000
    mov edi, 0x2000
    mov eax, 0x3000 | PAGE_PRESENT | PAGE_WRITE
    mov [edi], eax

    ; PDPT[510] -> PD at 0x4000 (for higher half mapping at 0xFFFFFFFF80000000)
    mov eax, 0x4000 | PAGE_PRESENT | PAGE_WRITE
    mov [edi + 510 * 8], eax

    ; PD[0] -> First 2MB identity mapped
    mov edi, 0x3000
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE
    mov [edi], eax

    ; PD[0] for kernel at 1MB (2MB page)
    mov edi, 0x3000
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE
    mov [edi + 0 * 8], eax      ; 0-2MB

    ; Map first 2MB at higher half too
    mov edi, 0x4000
    mov eax, PAGE_PRESENT | PAGE_WRITE | PAGE_SIZE
    mov [edi], eax

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)            ; PAE bit
    mov cr4, eax

    ; Load PML4 address into CR3
    mov eax, 0x1000
    mov cr3, eax

    ; Enable Long Mode in EFER MSR
    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or eax, (1 << 8)            ; Long Mode Enable bit
    wrmsr

    ; Enable paging (this activates Long Mode)
    mov eax, cr0
    or eax, (1 << 31)           ; Paging bit
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [gdt64_descriptor]

    ; Far jump to 64-bit code
    jmp 0x08:long_mode_entry

;-----------------------------------------
; 64-bit Long Mode
;-----------------------------------------
[BITS 64]

long_mode_entry:
    ; Set up segment registers for 64-bit mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up stack
    mov rsp, 0x90000

    ; Clear screen (VGA text mode at 0xB8000)
    mov rdi, 0xB8000
    mov rcx, 80 * 25            ; 80x25 characters
    mov ax, 0x0720              ; Space with light gray on black
    rep stosw

    ; Print "AAAos" in VGA text mode
    mov rdi, 0xB8000
    mov rsi, msg_aaaos
    mov ah, 0x0F                ; White on black
.print_loop:
    lodsb
    test al, al
    jz .print_done
    stosw
    jmp .print_loop
.print_done:

    ; Pass boot information to kernel
    ; RDI = pointer to boot info structure
    mov rdi, boot_info

    ; Jump to kernel at 1MB
    mov rax, KERNEL_LOAD_ADDR
    jmp rax

    ; Should never reach here
    cli
    hlt
    jmp $

;-----------------------------------------
; Data Section
;-----------------------------------------

[BITS 16]

; Messages
msg_stage2:         db "AAAos Stage 2 Bootloader", 13, 10, 0
msg_a20:            db "A20 enabled.", 13, 10, 0
msg_memmap:         db "Getting memory map...", 13, 10, 0
msg_pmode:          db "Entering Protected Mode...", 13, 10, 0
msg_loading_kernel: db "Loading kernel...", 13, 10, 0
msg_kernel_loaded:  db "Kernel loaded.", 13, 10, 0
msg_kernel_error:   db "Kernel load error!", 13, 10, 0

[BITS 64]
msg_aaaos:          db "AAAos v0.1 - 64-bit Long Mode Active", 0

[BITS 16]

; Boot drive (passed from Stage 1)
boot_drive:         db 0x80         ; Default to first hard drive

; Kernel DAP for INT 13h
align 4
kernel_dap:
    db 0x10                         ; Size
    db 0                            ; Reserved
    dw KERNEL_SECTORS               ; Sectors to read
    dw 0x0000                       ; Offset
    dw 0x1000                       ; Segment (0x1000:0x0000 = 0x10000)
    dq KERNEL_START_SECTOR          ; Starting LBA

; Memory map storage
memory_map_entries: dw 0
align 8
memory_map:
    times 32 * 24 db 0              ; Space for 32 memory map entries

; Boot information structure passed to kernel
align 8
boot_info:
    .magic:         dd 0xAAAB007    ; Magic number
    .mem_map_addr:  dq memory_map
    .mem_map_count: dq 0
    .framebuffer:   dq 0xB8000      ; VGA text mode buffer
    .fb_width:      dd 80
    .fb_height:     dd 25
    .fb_bpp:        dd 16           ; Bits per character cell

;-----------------------------------------
; GDT for 32-bit Protected Mode
;-----------------------------------------
align 16
gdt32_start:
    ; Null descriptor
    dq 0

    ; Code segment (0x08)
    dw 0xFFFF       ; Limit (low)
    dw 0x0000       ; Base (low)
    db 0x00         ; Base (middle)
    db 10011010b    ; Access: Present, Ring 0, Code, Executable, Readable
    db 11001111b    ; Flags: 4KB granularity, 32-bit + Limit (high)
    db 0x00         ; Base (high)

    ; Data segment (0x10)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; Access: Present, Ring 0, Data, Writable
    db 11001111b
    db 0x00

gdt32_end:

gdt32_descriptor:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

;-----------------------------------------
; GDT for 64-bit Long Mode
;-----------------------------------------
align 16
gdt64_start:
    ; Null descriptor
    dq 0

    ; Code segment (0x08) - 64-bit
    dw 0x0000       ; Limit (ignored in 64-bit)
    dw 0x0000       ; Base (ignored)
    db 0x00         ; Base
    db 10011010b    ; Access: Present, Ring 0, Code, Executable, Readable
    db 00100000b    ; Flags: Long mode
    db 0x00         ; Base

    ; Data segment (0x10) - 64-bit
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10010010b    ; Access: Present, Ring 0, Data, Writable
    db 00000000b
    db 0x00

gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64_start - 1
    dq gdt64_start

; Pad to ensure we don't overflow
times 16384 - ($ - $$) db 0
