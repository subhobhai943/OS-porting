; AAAos Kernel - System Call Assembly Entry Point
;
; This file contains the low-level assembly code for handling system calls
; using the SYSCALL/SYSRET mechanism on x86_64.
;
; When a user-space program executes the SYSCALL instruction:
; - RCX <- RIP (return address)
; - R11 <- RFLAGS
; - RIP <- MSR_LSTAR (syscall_entry)
; - CS  <- from MSR_STAR
; - SS  <- from MSR_STAR
; - RFLAGS <- RFLAGS & ~MSR_SFMASK
;
; Syscall calling convention (Linux x86_64 ABI):
; - RAX = syscall number
; - RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
; - Return value in RAX
;
; Note: R10 is used instead of RCX because SYSCALL clobbers RCX with RIP

[BITS 64]

; Segment selectors (must match GDT)
%define KERNEL_CS   0x08
%define KERNEL_DS   0x10
%define USER_CS     0x18
%define USER_DS     0x20

section .data

; Kernel stack for syscall handling (temporary until per-process stacks)
align 16
syscall_stack_bottom:
    times 8192 db 0         ; 8KB stack
syscall_stack_top:

section .text

; External C syscall handler
extern syscall_handler

; ============================================================================
; syscall_entry - Entry point for system calls
; ============================================================================
; This is the target of the SYSCALL instruction (set in MSR_LSTAR).
; At entry:
;   RCX = user RIP (return address)
;   R11 = user RFLAGS
;   RAX = syscall number
;   RDI, RSI, RDX, R10, R8, R9 = syscall arguments
;   RSP = user stack (still!)
;
; IMPORTANT: We are now in ring 0 but still using the user stack!
; We must switch to a kernel stack before doing anything else.
; ============================================================================

global syscall_entry
syscall_entry:
    ; =========================================================================
    ; CRITICAL: Switch to kernel stack
    ; =========================================================================
    ; The SYSCALL instruction does NOT switch stacks automatically.
    ; We must save the user RSP and switch to a kernel stack immediately.
    ;
    ; For now, we use a static kernel stack. A proper implementation would
    ; get the kernel stack from the TSS or per-CPU data structure.
    ; =========================================================================

    ; Save user RSP in a scratch register temporarily
    ; We'll use the swap technique with GS base in a full implementation
    ; For now, use a simple approach with a global variable

    ; Swap to kernel stack
    ; Save user RSP, load kernel RSP
    mov [rel user_rsp_save], rsp
    mov rsp, [rel kernel_rsp]

    ; =========================================================================
    ; Save user context on kernel stack
    ; =========================================================================
    ; Build a syscall_frame_t structure on the stack

    ; Push user RSP (was saved above)
    push qword [rel user_rsp_save]

    ; Save argument registers
    push rdi            ; arg1
    push rsi            ; arg2
    push rdx            ; arg3
    push rcx            ; Contains user RIP
    push rax            ; Syscall number (will be overwritten with result)
    push r8             ; arg5
    push r9             ; arg6
    push r10            ; arg4
    push r11            ; Contains user RFLAGS

    ; Save callee-saved registers (we must preserve these)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; =========================================================================
    ; Call C handler
    ; =========================================================================
    ; Pass pointer to the frame as first argument (RDI)
    mov rdi, rsp

    ; Ensure stack is 16-byte aligned for C calling convention
    ; The stack should already be aligned after our pushes
    ; (16 registers * 8 bytes = 128 bytes, which is 16-byte aligned)

    ; Call the C syscall dispatcher
    call syscall_handler

    ; Return value is in RAX, will be stored in frame by handler

    ; =========================================================================
    ; Restore context and return to user space
    ; =========================================================================

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Restore other registers
    pop r11             ; User RFLAGS for SYSRET
    pop r10
    pop r9
    pop r8
    pop rax             ; Return value (set by handler)
    pop rcx             ; User RIP for SYSRET
    pop rdx
    pop rsi
    pop rdi

    ; Restore user stack pointer
    pop rsp             ; This pops the saved user RSP

    ; =========================================================================
    ; Return to user space via SYSRET
    ; =========================================================================
    ; SYSRET expects:
    ; - RCX = RIP to return to
    ; - R11 = RFLAGS to restore
    ; - RAX = return value (already set)
    ;
    ; SYSRET will:
    ; - RIP <- RCX
    ; - RFLAGS <- R11 (with RF and VM cleared, some bits fixed)
    ; - CS <- from MSR_STAR[63:48] + 16, with RPL=3
    ; - SS <- from MSR_STAR[63:48] + 8, with RPL=3
    ;
    ; IMPORTANT: SYSRET has a security issue where if RCX contains a
    ; non-canonical address, it will #GP in ring 3 with a ring 0 stack!
    ; A production kernel should validate RCX before SYSRET.
    ; =========================================================================

    ; Use sysretq for 64-bit return
    ; o64 prefix ensures 64-bit operand size
    db 0x48             ; REX.W prefix for 64-bit SYSRET
    sysret


; ============================================================================
; Data section for stack management
; ============================================================================

section .data

; Temporary storage for user RSP during stack switch
align 8
user_rsp_save:
    dq 0

; Kernel stack pointer for syscall handling
align 8
kernel_rsp:
    dq syscall_stack_top


; ============================================================================
; Alternative entry point using interrupt (for compatibility/debugging)
; ============================================================================
; Some systems or debugging scenarios may want to use INT 0x80 style syscalls.
; This provides that interface.

section .text

global syscall_int_entry
syscall_int_entry:
    ; This would be registered as an interrupt handler (e.g., INT 0x80)
    ; For now, just redirect to the main handler logic

    ; Note: When entered via interrupt, we already have a kernel stack
    ; and the CPU has pushed SS, RSP, RFLAGS, CS, RIP

    ; Save registers to build interrupt-compatible frame
    push rax            ; Save syscall number

    ; Save all general purpose registers
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Adjust for different frame layout
    ; The interrupt frame is different from SYSCALL frame
    ; This is a simplified stub - full implementation would need
    ; to properly translate between frames

    ; For now, just call the handler with current stack
    mov rdi, rsp
    call syscall_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx

    ; Skip saved RAX (return value already in RAX from handler)
    add rsp, 8

    ; Return from interrupt
    iretq


; ============================================================================
; Helper: Get/Set kernel stack for current CPU/process
; ============================================================================
; These would be used by the scheduler to set up per-process kernel stacks

global syscall_set_kernel_stack
syscall_set_kernel_stack:
    ; RDI = new kernel stack pointer
    mov [rel kernel_rsp], rdi
    ret

global syscall_get_kernel_stack
syscall_get_kernel_stack:
    mov rax, [rel kernel_rsp]
    ret
