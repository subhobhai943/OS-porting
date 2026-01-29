; AAAos Stage 1 Bootloader (MBR)
; This 512-byte boot sector loads Stage 2 from disk
; and jumps to it. Runs in 16-bit real mode.

[BITS 16]
[ORG 0x7C00]

; Constants
STAGE2_LOAD_SEG     equ 0x0000
STAGE2_LOAD_OFF     equ 0x7E00      ; Load Stage 2 right after MBR
STAGE2_SECTORS      equ 32          ; Load 32 sectors (16KB) for Stage 2
STAGE2_START_SECTOR equ 1           ; Stage 2 starts at sector 2 (LBA 1)

start:
    ; Set up segment registers
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; Stack grows down from boot sector
    sti

    ; Save boot drive number
    mov [boot_drive], dl

    ; Print loading message
    mov si, msg_loading
    call print_string

    ; Reset disk system
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Load Stage 2 using INT 13h extensions (LBA mode)
    mov ah, 0x42                ; Extended read
    mov dl, [boot_drive]
    mov si, dap                 ; Disk Address Packet
    int 0x13
    jc disk_error

    ; Print success message
    mov si, msg_loaded
    call print_string

    ; Jump to Stage 2
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

disk_error:
    mov si, msg_disk_error
    call print_string
    jmp halt

halt:
    cli
    hlt
    jmp halt

; Print null-terminated string from SI
print_string:
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

; Data
boot_drive:     db 0

; Disk Address Packet for INT 13h extensions
align 4
dap:
    db 0x10                     ; Size of DAP (16 bytes)
    db 0                        ; Reserved
    dw STAGE2_SECTORS           ; Number of sectors to read
    dw STAGE2_LOAD_OFF          ; Offset to load to
    dw STAGE2_LOAD_SEG          ; Segment to load to
    dq STAGE2_START_SECTOR      ; Starting LBA (sector 1 = second sector)

; Messages
msg_loading:    db "AAAos Stage 1...", 13, 10, 0
msg_loaded:     db "Stage 2 loaded.", 13, 10, 0
msg_disk_error: db "Disk error!", 13, 10, 0

; Pad to 510 bytes and add boot signature
times 510 - ($ - $$) db 0
dw 0xAA55
