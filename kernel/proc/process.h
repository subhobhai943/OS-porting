/**
 * AAAos Kernel - Process Management
 *
 * Defines the Process Control Block (PCB) and process management functions.
 * Initially supports kernel threads; user-space processes will be added later.
 */

#ifndef _AAAOS_PROC_PROCESS_H
#define _AAAOS_PROC_PROCESS_H

#include "../include/types.h"

/* Process configuration constants */
#define PROCESS_NAME_MAX        64      /* Maximum process name length */
#define PROCESS_MAX_COUNT       256     /* Maximum number of processes */
#define PROCESS_KERNEL_STACK_SIZE   (16 * KB)   /* 16KB kernel stack per process */
#define PROCESS_MAX_CHILDREN    32      /* Maximum children per process */

/* Special PIDs */
#define PID_INVALID             0       /* Invalid/no process */
#define PID_IDLE                1       /* Kernel idle process */

/* Process priorities */
#define PRIORITY_IDLE           0       /* Lowest priority (idle only) */
#define PRIORITY_LOW            1
#define PRIORITY_NORMAL         5
#define PRIORITY_HIGH           10
#define PRIORITY_REALTIME       15      /* Highest priority */
#define PRIORITY_DEFAULT        PRIORITY_NORMAL

/**
 * Process states
 */
typedef enum {
    PROCESS_STATE_INVALID = 0,  /* PCB slot is not in use */
    PROCESS_STATE_READY,        /* Ready to run, waiting for CPU */
    PROCESS_STATE_RUNNING,      /* Currently executing on CPU */
    PROCESS_STATE_BLOCKED,      /* Waiting for I/O or event */
    PROCESS_STATE_TERMINATED    /* Finished execution, awaiting cleanup */
} process_state_t;

/**
 * CPU context (saved registers for context switching)
 * Layout matches what we save/restore in assembly
 */
typedef struct PACKED {
    /* General purpose registers */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Instruction pointer and flags */
    uint64_t rip;           /* Instruction pointer */
    uint64_t cs;            /* Code segment */
    uint64_t rflags;        /* CPU flags */
    uint64_t rsp;           /* Stack pointer */
    uint64_t ss;            /* Stack segment */
} cpu_context_t;

/**
 * Process Control Block (PCB)
 * Contains all information about a process
 */
typedef struct process {
    /* Identity */
    uint32_t pid;                           /* Process ID */
    char name[PROCESS_NAME_MAX];            /* Process name */

    /* State */
    process_state_t state;                  /* Current state */
    int exit_status;                        /* Exit status (valid when TERMINATED) */

    /* Scheduling */
    uint8_t priority;                       /* Scheduling priority */
    uint64_t time_slice;                    /* Remaining time slice (ticks) */
    uint64_t total_ticks;                   /* Total CPU ticks used */

    /* CPU context */
    cpu_context_t context;                  /* Saved CPU registers */

    /* Memory */
    uint64_t page_table;                    /* CR3 value (PML4 physical address) */
    virtaddr_t kernel_stack;                /* Top of kernel stack */
    virtaddr_t kernel_stack_base;           /* Base of kernel stack (for freeing) */

    /* Process tree */
    struct process *parent;                 /* Parent process */
    struct process *children[PROCESS_MAX_CHILDREN]; /* Child processes */
    uint32_t child_count;                   /* Number of children */

    /* Flags */
    uint32_t flags;                         /* Process flags */
    #define PROCESS_FLAG_KERNEL     BIT(0)  /* Kernel process (ring 0) */
    #define PROCESS_FLAG_USER       BIT(1)  /* User process (ring 3) */

} process_t;

/**
 * Process entry point function type
 */
typedef void (*process_entry_t)(void);

/**
 * Initialize the process management subsystem
 * Creates the idle process as PID 1
 */
void process_init(void);

/**
 * Create a new kernel process
 * @param name Process name (max PROCESS_NAME_MAX-1 chars)
 * @param entry Entry point function
 * @return Pointer to new process, or NULL on failure
 */
process_t* process_create(const char *name, process_entry_t entry);

/**
 * Terminate the current process
 * @param status Exit status code
 * @note This function does not return
 */
NORETURN void process_exit(int status);

/**
 * Get the currently running process
 * @return Pointer to current process, or NULL if none
 */
process_t* process_get_current(void);

/**
 * Find a process by its PID
 * @param pid Process ID to find
 * @return Pointer to process, or NULL if not found
 */
process_t* process_get_by_pid(uint32_t pid);

/**
 * Change a process's state
 * @param proc Process to modify
 * @param state New state
 */
void process_set_state(process_t *proc, process_state_t state);

/**
 * Get the total number of processes (including terminated)
 * @return Number of processes
 */
uint32_t process_get_count(void);

/**
 * Get number of processes in a specific state
 * @param state State to count
 * @return Number of processes in that state
 */
uint32_t process_count_by_state(process_state_t state);

/**
 * Get string representation of process state
 * @param state Process state
 * @return Static string describing the state
 */
const char* process_state_string(process_state_t state);

/**
 * Dump process info to serial console (for debugging)
 * @param proc Process to dump info for
 */
void process_dump_info(process_t *proc);

/**
 * Dump all processes to serial console (for debugging)
 */
void process_dump_all(void);

#endif /* _AAAOS_PROC_PROCESS_H */
