; AAAos Kernel - Context Switch Assembly
;
; Implements the low-level context switch mechanism for the scheduler.
; These functions save and restore CPU registers to switch between processes.
;
; Calling conventions (System V AMD64 ABI):
;   - First argument: RDI
;   - Second argument: RSI
;   - Callee-saved registers: RBX, RBP, R12-R15
;   - Caller-saved registers: RAX, RCX, RDX, RSI, RDI, R8-R11

[BITS 64]

section .text

; Export symbols
global context_switch
global context_switch_first

;------------------------------------------------------------------------------
; context_switch - Switch from one process context to another
;
; void context_switch(cpu_context_t *old_context, cpu_context_t *new_context);
;
; Arguments:
;   RDI = pointer to old context structure (to save current state)
;   RSI = pointer to new context structure (to restore)
;
; The cpu_context_t structure layout (must match process.h):
;   Offset  Register
;   0x00    R15
;   0x08    R14
;   0x10    R13
;   0x18    R12
;   0x20    R11
;   0x28    R10
;   0x30    R9
;   0x38    R8
;   0x40    RBP
;   0x48    RDI
;   0x50    RSI
;   0x58    RDX
;   0x60    RCX
;   0x68    RBX
;   0x70    RAX
;   0x78    RIP
;   0x80    CS
;   0x88    RFLAGS
;   0x90    RSP
;   0x98    SS
;------------------------------------------------------------------------------
context_switch:
    ; Check if old_context is NULL (skip saving if so)
    test rdi, rdi
    jz .restore_context

    ; =========== SAVE OLD CONTEXT ===========
    ; Save general purpose registers to old_context structure

    mov [rdi + 0x00], r15
    mov [rdi + 0x08], r14
    mov [rdi + 0x10], r13
    mov [rdi + 0x18], r12
    mov [rdi + 0x20], r11
    mov [rdi + 0x28], r10
    mov [rdi + 0x30], r9
    mov [rdi + 0x38], r8
    mov [rdi + 0x40], rbp

    ; Save RDI (we're using it, so get original value from caller)
    ; The caller's RDI (old_context ptr) needs to be saved
    ; We save the current RDI value since that's what the process had
    mov [rdi + 0x48], rdi

    ; Save RSI (new_context ptr - but save original RSI value)
    mov [rdi + 0x50], rsi

    mov [rdi + 0x58], rdx
    mov [rdi + 0x60], rcx
    mov [rdi + 0x68], rbx
    mov [rdi + 0x70], rax

    ; Save RIP - use return address on stack
    ; The return address is what RIP should be when this process resumes
    mov rax, [rsp]              ; Get return address
    mov [rdi + 0x78], rax       ; Save as RIP

    ; Save CS - use current code segment
    mov ax, cs
    movzx rax, ax
    mov [rdi + 0x80], rax

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + 0x88], rax

    ; Save RSP - account for return address on stack
    ; When we return, RSP will be current + 8 (after popping return addr)
    lea rax, [rsp + 8]
    mov [rdi + 0x90], rax

    ; Save SS - use current stack segment
    mov ax, ss
    movzx rax, ax
    mov [rdi + 0x98], rax

.restore_context:
    ; =========== RESTORE NEW CONTEXT ===========
    ; RSI contains pointer to new context

    ; Restore general purpose registers from new_context structure
    mov r15, [rsi + 0x00]
    mov r14, [rsi + 0x08]
    mov r13, [rsi + 0x10]
    mov r12, [rsi + 0x18]
    mov r11, [rsi + 0x20]
    mov r10, [rsi + 0x28]
    mov r9,  [rsi + 0x30]
    mov r8,  [rsi + 0x38]
    mov rbp, [rsi + 0x40]

    ; We need to be careful with RDI and RSI since we're using RSI
    ; Temporarily use other registers
    mov rbx, [rsi + 0x58]       ; Load RDX into RBX temporarily
    mov rcx, [rsi + 0x60]       ; Load RCX
    mov rax, [rsi + 0x70]       ; Load RAX

    ; Get the new stack pointer
    mov rsp, [rsi + 0x90]

    ; Push the new RIP onto the new stack for 'ret' to use
    mov rdx, [rsi + 0x78]       ; Get new RIP
    push rdx                     ; Push it for ret

    ; Now restore the remaining registers
    mov rdx, rbx                ; Restore RDX from temp
    mov rbx, [rsi + 0x68]       ; Restore RBX

    ; Restore RFLAGS
    push qword [rsi + 0x88]
    popfq

    ; Finally restore RDI and RSI (RSI last since we're using it)
    mov rdi, [rsi + 0x48]
    mov rsi, [rsi + 0x50]

    ; Return to new context's RIP (via ret using pushed value)
    ret


;------------------------------------------------------------------------------
; context_switch_first - Start the first process (no old context to save)
;
; void context_switch_first(cpu_context_t *context);
;
; Arguments:
;   RDI = pointer to context structure to restore
;
; This function is used to start the very first process when there is
; no previous context to save. It performs an iretq to properly set up
; all segment registers and flags for the new process.
;------------------------------------------------------------------------------
context_switch_first:
    ; RSI not used, copy RDI to RSI for consistency with restore code
    mov rsi, rdi

    ; =========== RESTORE CONTEXT (same as above) ===========

    ; Restore general purpose registers
    mov r15, [rsi + 0x00]
    mov r14, [rsi + 0x08]
    mov r13, [rsi + 0x10]
    mov r12, [rsi + 0x18]
    mov r11, [rsi + 0x20]
    mov r10, [rsi + 0x28]
    mov r9,  [rsi + 0x30]
    mov r8,  [rsi + 0x38]
    mov rbp, [rsi + 0x40]
    mov rdx, [rsi + 0x58]
    mov rcx, [rsi + 0x60]
    mov rbx, [rsi + 0x68]
    mov rax, [rsi + 0x70]

    ; For the first process, we use iretq to properly initialize
    ; the CPU state including SS, RSP, RFLAGS, CS, and RIP

    ; Build iretq frame on current stack
    ; Push in reverse order: SS, RSP, RFLAGS, CS, RIP

    push qword [rsi + 0x98]     ; SS
    push qword [rsi + 0x90]     ; RSP
    push qword [rsi + 0x88]     ; RFLAGS
    push qword [rsi + 0x80]     ; CS
    push qword [rsi + 0x78]     ; RIP

    ; Restore RDI and RSI last
    mov rdi, [rsi + 0x48]
    mov rsi, [rsi + 0x50]

    ; Return via iretq to set up proper execution environment
    iretq
