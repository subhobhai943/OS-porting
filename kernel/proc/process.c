/**
 * AAAos Kernel - Process Management Implementation
 *
 * Implements process creation, termination, and management.
 * Uses a static process table for simplicity.
 */

#include "process.h"
#include "../include/serial.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/include/gdt.h"

/* Process table - statically allocated */
static process_t process_table[PROCESS_MAX_COUNT];

/* Current running process */
static process_t *current_process = NULL;

/* Next available PID */
static uint32_t next_pid = PID_IDLE;

/* Process manager lock */
static volatile int process_lock = 0;

/* Forward declarations */
static void idle_process_entry(void);
static process_t* alloc_pcb(void);
static void free_pcb(process_t *proc);

/**
 * Acquire process manager lock
 */
static inline void process_acquire_lock(void) {
    while (__sync_lock_test_and_set(&process_lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

/**
 * Release process manager lock
 */
static inline void process_release_lock(void) {
    __sync_lock_release(&process_lock);
}

/**
 * Simple string copy (kernel doesn't have libc)
 */
static void kstrcpy(char *dest, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/**
 * Get CR3 (current page table)
 */
static inline uint64_t read_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * Allocate a PCB from the process table
 */
static process_t* alloc_pcb(void) {
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state == PROCESS_STATE_INVALID) {
            /* Zero out the PCB */
            uint8_t *p = (uint8_t*)&process_table[i];
            for (size_t j = 0; j < sizeof(process_t); j++) {
                p[j] = 0;
            }
            return &process_table[i];
        }
    }
    return NULL;
}

/**
 * Free a PCB back to the process table
 */
static void free_pcb(process_t *proc) {
    if (proc) {
        proc->state = PROCESS_STATE_INVALID;
        proc->pid = PID_INVALID;
    }
}

/**
 * Idle process entry point
 * Simply halts the CPU, waiting for interrupts
 */
static void idle_process_entry(void) {
    kprintf("[PROC] Idle process running (PID %u)\n", current_process ? current_process->pid : 0);

    for (;;) {
        /* Enable interrupts and halt until next interrupt */
        __asm__ __volatile__(
            "sti\n"
            "hlt\n"
        );
    }
}

/**
 * Process state to string
 */
const char* process_state_string(process_state_t state) {
    switch (state) {
        case PROCESS_STATE_INVALID:     return "INVALID";
        case PROCESS_STATE_READY:       return "READY";
        case PROCESS_STATE_RUNNING:     return "RUNNING";
        case PROCESS_STATE_BLOCKED:     return "BLOCKED";
        case PROCESS_STATE_TERMINATED:  return "TERMINATED";
        default:                        return "UNKNOWN";
    }
}

/**
 * Initialize process management
 */
void process_init(void) {
    kprintf("[PROC] Initializing Process Manager...\n");

    /* Initialize the process table */
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        process_table[i].state = PROCESS_STATE_INVALID;
        process_table[i].pid = PID_INVALID;
    }

    kprintf("[PROC] Process table initialized (%u slots)\n", PROCESS_MAX_COUNT);

    /* Create the idle process (PID 1) */
    process_t *idle = process_create("idle", idle_process_entry);
    if (!idle) {
        kprintf("[PROC] FATAL: Failed to create idle process!\n");
        return;
    }

    /* Idle process has lowest priority */
    idle->priority = PRIORITY_IDLE;
    idle->flags |= PROCESS_FLAG_KERNEL;

    /* Make idle the current process */
    current_process = idle;
    process_set_state(idle, PROCESS_STATE_RUNNING);

    kprintf("[PROC] Idle process created (PID %u)\n", idle->pid);
    kprintf("[PROC] Process Manager initialized successfully\n");
}

/**
 * Create a new kernel process
 */
process_t* process_create(const char *name, process_entry_t entry) {
    if (!name || !entry) {
        kprintf("[PROC] Error: process_create called with NULL arguments\n");
        return NULL;
    }

    process_acquire_lock();

    /* Allocate a PCB */
    process_t *proc = alloc_pcb();
    if (!proc) {
        process_release_lock();
        kprintf("[PROC] Error: No free PCB slots (max %u processes)\n", PROCESS_MAX_COUNT);
        return NULL;
    }

    /* Assign PID */
    proc->pid = next_pid++;

    /* Copy name */
    kstrcpy(proc->name, name, PROCESS_NAME_MAX);

    /* Set initial state */
    proc->state = PROCESS_STATE_READY;
    proc->priority = PRIORITY_DEFAULT;
    proc->exit_status = 0;
    proc->time_slice = 0;
    proc->total_ticks = 0;

    /* Allocate kernel stack */
    size_t stack_pages = PROCESS_KERNEL_STACK_SIZE / PAGE_SIZE;
    physaddr_t stack_phys = pmm_alloc_pages(stack_pages);

    if (stack_phys == 0) {
        free_pcb(proc);
        process_release_lock();
        kprintf("[PROC] Error: Failed to allocate kernel stack for '%s'\n", name);
        return NULL;
    }

    /* For now, we use identity mapping (phys == virt) */
    proc->kernel_stack_base = (virtaddr_t)stack_phys;
    proc->kernel_stack = proc->kernel_stack_base + PROCESS_KERNEL_STACK_SIZE;

    kprintf("[PROC] Allocated kernel stack at 0x%llx (%llu KB)\n",
            (uint64_t)proc->kernel_stack_base, (uint64_t)(PROCESS_KERNEL_STACK_SIZE / KB));

    /* Use current page table (kernel threads share address space) */
    proc->page_table = read_cr3();

    /* Initialize CPU context */
    /* Stack grows downward, leave some space for the initial frame */
    uint64_t stack_top = proc->kernel_stack - sizeof(uint64_t);

    /* Set up initial context */
    proc->context.rip = (uint64_t)entry;        /* Entry point */
    proc->context.cs = 0x08;                     /* Kernel code segment */
    proc->context.rflags = 0x202;               /* IF=1, reserved=1 (interrupts enabled) */
    proc->context.rsp = stack_top;              /* Stack pointer */
    proc->context.ss = 0x10;                     /* Kernel data segment */

    /* Clear general purpose registers */
    proc->context.rax = 0;
    proc->context.rbx = 0;
    proc->context.rcx = 0;
    proc->context.rdx = 0;
    proc->context.rsi = 0;
    proc->context.rdi = 0;
    proc->context.rbp = 0;
    proc->context.r8 = 0;
    proc->context.r9 = 0;
    proc->context.r10 = 0;
    proc->context.r11 = 0;
    proc->context.r12 = 0;
    proc->context.r13 = 0;
    proc->context.r14 = 0;
    proc->context.r15 = 0;

    /* Set process flags */
    proc->flags = PROCESS_FLAG_KERNEL;

    /* Set parent (if there's a current process) */
    proc->parent = current_process;
    proc->child_count = 0;

    /* Add to parent's children list */
    if (current_process && current_process->child_count < PROCESS_MAX_CHILDREN) {
        current_process->children[current_process->child_count++] = proc;
    }

    process_release_lock();

    kprintf("[PROC] Created process '%s' (PID %u, entry=0x%llx)\n",
            proc->name, proc->pid, (uint64_t)entry);

    return proc;
}

/**
 * Terminate the current process
 */
NORETURN void process_exit(int status) {
    process_acquire_lock();

    if (!current_process) {
        kprintf("[PROC] Error: process_exit called with no current process!\n");
        process_release_lock();
        /* Halt since we have nowhere to go */
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }

    kprintf("[PROC] Process '%s' (PID %u) exiting with status %d\n",
            current_process->name, current_process->pid, status);

    /* Save exit status */
    current_process->exit_status = status;

    /* Mark as terminated */
    current_process->state = PROCESS_STATE_TERMINATED;

    /* Reparent children to idle process (PID 1) */
    if (current_process->child_count > 0) {
        process_t *idle = process_get_by_pid(PID_IDLE);
        if (idle) {
            for (uint32_t i = 0; i < current_process->child_count; i++) {
                process_t *child = current_process->children[i];
                if (child && idle->child_count < PROCESS_MAX_CHILDREN) {
                    child->parent = idle;
                    idle->children[idle->child_count++] = child;
                }
                current_process->children[i] = NULL;
            }
        }
        current_process->child_count = 0;
    }

    /* Remove from parent's children list */
    if (current_process->parent) {
        process_t *parent = current_process->parent;
        for (uint32_t i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == current_process) {
                /* Shift remaining children down */
                for (uint32_t j = i; j < parent->child_count - 1; j++) {
                    parent->children[j] = parent->children[j + 1];
                }
                parent->children[parent->child_count - 1] = NULL;
                parent->child_count--;
                break;
            }
        }
    }

    /* Free kernel stack */
    if (current_process->kernel_stack_base) {
        size_t stack_pages = PROCESS_KERNEL_STACK_SIZE / PAGE_SIZE;
        pmm_free_pages((physaddr_t)current_process->kernel_stack_base, stack_pages);
        current_process->kernel_stack_base = 0;
        current_process->kernel_stack = 0;
    }

    /* Free the PCB slot */
    free_pcb(current_process);

    process_t *old_process = current_process;
    current_process = NULL;

    process_release_lock();

    kprintf("[PROC] Process '%s' (PID %u) terminated\n", old_process->name, old_process->pid);

    /* TODO: Trigger scheduler to pick next process */
    /* For now, just halt */
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

/**
 * Get the current running process
 */
process_t* process_get_current(void) {
    return current_process;
}

/**
 * Find a process by PID
 */
process_t* process_get_by_pid(uint32_t pid) {
    if (pid == PID_INVALID) {
        return NULL;
    }

    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state != PROCESS_STATE_INVALID &&
            process_table[i].pid == pid) {
            return &process_table[i];
        }
    }

    return NULL;
}

/**
 * Change a process's state
 */
void process_set_state(process_t *proc, process_state_t state) {
    if (!proc) {
        kprintf("[PROC] Error: process_set_state called with NULL process\n");
        return;
    }

    process_state_t old_state = proc->state;
    proc->state = state;

    kprintf("[PROC] Process '%s' (PID %u): %s -> %s\n",
            proc->name, proc->pid,
            process_state_string(old_state),
            process_state_string(state));

    /* Update current_process if this process is now running */
    if (state == PROCESS_STATE_RUNNING) {
        /* Mark old current as READY if it was running */
        if (current_process && current_process != proc &&
            current_process->state == PROCESS_STATE_RUNNING) {
            current_process->state = PROCESS_STATE_READY;
        }
        current_process = proc;
    }
}

/**
 * Get total number of processes
 */
uint32_t process_get_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state != PROCESS_STATE_INVALID) {
            count++;
        }
    }
    return count;
}

/**
 * Count processes in a specific state
 */
uint32_t process_count_by_state(process_state_t state) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state == state) {
            count++;
        }
    }
    return count;
}

/**
 * Dump process info to serial console
 */
void process_dump_info(process_t *proc) {
    if (!proc) {
        kprintf("[PROC] dump_info: NULL process\n");
        return;
    }

    kprintf("[PROC] === Process Info ===\n");
    kprintf("[PROC]   PID:       %u\n", proc->pid);
    kprintf("[PROC]   Name:      %s\n", proc->name);
    kprintf("[PROC]   State:     %s\n", process_state_string(proc->state));
    kprintf("[PROC]   Priority:  %u\n", proc->priority);
    kprintf("[PROC]   Flags:     0x%x", proc->flags);
    if (proc->flags & PROCESS_FLAG_KERNEL) kprintf(" [KERNEL]");
    if (proc->flags & PROCESS_FLAG_USER) kprintf(" [USER]");
    kprintf("\n");
    kprintf("[PROC]   Ticks:     %llu\n", proc->total_ticks);
    kprintf("[PROC]   K-Stack:   0x%llx (base: 0x%llx)\n",
            proc->kernel_stack, proc->kernel_stack_base);
    kprintf("[PROC]   Page Table: 0x%llx\n", proc->page_table);
    kprintf("[PROC]   RIP:       0x%llx\n", proc->context.rip);
    kprintf("[PROC]   RSP:       0x%llx\n", proc->context.rsp);
    kprintf("[PROC]   RFLAGS:    0x%llx\n", proc->context.rflags);
    kprintf("[PROC]   Parent:    %s (PID %u)\n",
            proc->parent ? proc->parent->name : "(none)",
            proc->parent ? proc->parent->pid : 0);
    kprintf("[PROC]   Children:  %u\n", proc->child_count);
    kprintf("[PROC] ==================\n");
}

/**
 * Dump all processes
 */
void process_dump_all(void) {
    kprintf("[PROC] ========== Process Table ==========\n");
    kprintf("[PROC] Total slots: %u, Active: %u\n",
            PROCESS_MAX_COUNT, process_get_count());
    kprintf("[PROC]   Ready:      %u\n", process_count_by_state(PROCESS_STATE_READY));
    kprintf("[PROC]   Running:    %u\n", process_count_by_state(PROCESS_STATE_RUNNING));
    kprintf("[PROC]   Blocked:    %u\n", process_count_by_state(PROCESS_STATE_BLOCKED));
    kprintf("[PROC]   Terminated: %u\n", process_count_by_state(PROCESS_STATE_TERMINATED));
    kprintf("[PROC] ---------------------------------\n");
    kprintf("[PROC] PID  STATE      PRIO  NAME\n");
    kprintf("[PROC] ---------------------------------\n");

    for (uint32_t i = 0; i < PROCESS_MAX_COUNT; i++) {
        if (process_table[i].state != PROCESS_STATE_INVALID) {
            process_t *p = &process_table[i];
            const char *marker = (p == current_process) ? "*" : " ";
            kprintf("[PROC] %s%-4u %-10s %-5u %s\n",
                    marker,
                    p->pid,
                    process_state_string(p->state),
                    p->priority,
                    p->name);
        }
    }

    kprintf("[PROC] =====================================\n");
    kprintf("[PROC] (* = current process)\n");
}
