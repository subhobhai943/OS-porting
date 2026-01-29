/**
 * AAAos Kernel - System Call Interface
 *
 * This module provides the system call interface for user-space programs
 * to request kernel services. Uses the SYSCALL/SYSRET mechanism on x86_64.
 *
 * Calling Convention:
 *   RAX = syscall number
 *   RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5, R9 = arg6
 *   Return value in RAX
 */

#ifndef _AAAOS_SYSCALL_H
#define _AAAOS_SYSCALL_H

#include "../include/types.h"

/* ============================================================================
 * System Call Numbers
 * ============================================================================ */

#define SYS_EXIT        0       /* Exit process */
#define SYS_READ        1       /* Read from file descriptor */
#define SYS_WRITE       2       /* Write to file descriptor */
#define SYS_OPEN        3       /* Open file */
#define SYS_CLOSE       4       /* Close file descriptor */
#define SYS_FORK        5       /* Create child process */
#define SYS_EXEC        6       /* Execute program */
#define SYS_WAIT        7       /* Wait for child process */
#define SYS_GETPID      8       /* Get process ID */
#define SYS_SLEEP       9       /* Sleep for milliseconds */
#define SYS_MMAP        10      /* Map memory */
#define SYS_MUNMAP      11      /* Unmap memory */

#define SYSCALL_MAX     11      /* Maximum syscall number */

/* ============================================================================
 * MSR Definitions for SYSCALL/SYSRET
 * ============================================================================ */

#define MSR_EFER        0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR        0xC0000081  /* Segment selectors for SYSCALL/SYSRET */
#define MSR_LSTAR       0xC0000082  /* Long mode SYSCALL entry point (RIP) */
#define MSR_CSTAR       0xC0000083  /* Compatibility mode SYSCALL entry (unused) */
#define MSR_SFMASK      0xC0000084  /* RFLAGS mask for SYSCALL */

/* EFER bits */
#define EFER_SCE        (1 << 0)    /* SYSCALL Enable */

/* RFLAGS bits to mask on SYSCALL */
#define RFLAGS_IF       (1 << 9)    /* Interrupt Flag */
#define RFLAGS_TF       (1 << 8)    /* Trap Flag */
#define RFLAGS_DF       (1 << 10)   /* Direction Flag */
#define RFLAGS_AC       (1 << 18)   /* Alignment Check */
#define RFLAGS_NT       (1 << 14)   /* Nested Task */

/* Mask to clear IF, TF, DF, AC, NT on syscall entry */
#define SYSCALL_RFLAGS_MASK (RFLAGS_IF | RFLAGS_TF | RFLAGS_DF | RFLAGS_AC | RFLAGS_NT)

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define ENOSYS          38      /* Function not implemented */
#define EINVAL          22      /* Invalid argument */
#define EBADF           9       /* Bad file descriptor */
#define ENOMEM          12      /* Out of memory */
#define ECHILD          10      /* No child processes */
#define EPERM           1       /* Operation not permitted */
#define ENOENT          2       /* No such file or directory */
#define EIO             5       /* I/O error */

/* ============================================================================
 * Syscall Register Frame
 * ============================================================================ */

/**
 * Register state saved on syscall entry
 * This structure matches the layout pushed by syscall_entry in assembly
 */
typedef struct PACKED {
    /* Callee-saved registers (preserved across syscall) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

    /* Caller-saved / syscall arguments */
    uint64_t r11;           /* Contains RFLAGS before SYSCALL */
    uint64_t r10;           /* Syscall arg4 */
    uint64_t r9;            /* Syscall arg6 */
    uint64_t r8;            /* Syscall arg5 */
    uint64_t rax;           /* Syscall number / return value */
    uint64_t rcx;           /* Contains RIP before SYSCALL */
    uint64_t rdx;           /* Syscall arg3 */
    uint64_t rsi;           /* Syscall arg2 */
    uint64_t rdi;           /* Syscall arg1 */

    /* User stack pointer (saved from SYSCALL) */
    uint64_t user_rsp;
} syscall_frame_t;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * Initialize the syscall mechanism
 * Sets up MSRs for SYSCALL/SYSRET instruction support
 */
void syscall_init(void);

/**
 * Main syscall dispatcher (called from assembly)
 * @param frame Pointer to saved register state
 * @return Return value for the syscall (placed in RAX)
 */
int64_t syscall_handler(syscall_frame_t *frame);

/* ============================================================================
 * Individual System Call Handlers
 * ============================================================================ */

/**
 * SYS_EXIT - Terminate the current process
 * @param status Exit status code
 */
void sys_exit(int status);

/**
 * SYS_READ - Read from a file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or negative error code
 */
int64_t sys_read(int fd, void *buf, size_t count);

/**
 * SYS_WRITE - Write to a file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
int64_t sys_write(int fd, const void *buf, size_t count);

/**
 * SYS_OPEN - Open a file
 * @param pathname Path to the file
 * @param flags Open flags
 * @param mode File mode (for creation)
 * @return File descriptor, or negative error code
 */
int64_t sys_open(const char *pathname, int flags, int mode);

/**
 * SYS_CLOSE - Close a file descriptor
 * @param fd File descriptor to close
 * @return 0 on success, negative error code on failure
 */
int64_t sys_close(int fd);

/**
 * SYS_FORK - Create a child process
 * @return Child PID to parent, 0 to child, negative error code on failure
 */
int64_t sys_fork(void);

/**
 * SYS_EXEC - Execute a new program
 * @param pathname Path to executable
 * @param argv Argument vector
 * @param envp Environment vector
 * @return Does not return on success, negative error code on failure
 */
int64_t sys_exec(const char *pathname, char *const argv[], char *const envp[]);

/**
 * SYS_WAIT - Wait for a child process
 * @param pid PID to wait for (-1 for any child)
 * @param status Pointer to store exit status
 * @param options Wait options
 * @return PID of terminated child, or negative error code
 */
int64_t sys_wait(int pid, int *status, int options);

/**
 * SYS_GETPID - Get current process ID
 * @return Current process ID
 */
int64_t sys_getpid(void);

/**
 * SYS_SLEEP - Sleep for a specified duration
 * @param milliseconds Duration to sleep in milliseconds
 * @return 0 on success, negative error code on failure
 */
int64_t sys_sleep(uint64_t milliseconds);

/**
 * SYS_MMAP - Map memory into address space
 * @param addr Preferred address (NULL for any)
 * @param length Length of mapping
 * @param prot Memory protection flags
 * @param flags Mapping flags
 * @param fd File descriptor (for file mappings)
 * @param offset Offset into file
 * @return Address of mapping, or negative error code
 */
int64_t sys_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset);

/**
 * SYS_MUNMAP - Unmap memory from address space
 * @param addr Address of mapping
 * @param length Length of mapping
 * @return 0 on success, negative error code on failure
 */
int64_t sys_munmap(void *addr, size_t length);

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Read from Model-Specific Register
 * @param msr MSR number
 * @return Value of the MSR
 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );
    return ((uint64_t)high << 32) | low;
}

/**
 * Write to Model-Specific Register
 * @param msr MSR number
 * @param value Value to write
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ __volatile__(
        "wrmsr"
        : /* no outputs */
        : "c"(msr), "a"(low), "d"(high)
    );
}

#endif /* _AAAOS_SYSCALL_H */
