/**
 * AAAos Kernel - Round-Robin Scheduler Implementation
 *
 * Implements preemptive round-robin scheduling with configurable time slices.
 * The scheduler is triggered by timer interrupts for preemption.
 */

#include "scheduler.h"
#include "../include/serial.h"
#include "../arch/x86_64/include/idt.h"
#include "../arch/x86_64/io.h"

/* PIT (Programmable Interval Timer) ports */
#define PIT_CHANNEL0_DATA   0x40
#define PIT_CHANNEL1_DATA   0x41
#define PIT_CHANNEL2_DATA   0x42
#define PIT_COMMAND         0x43

/* PIT command byte fields */
#define PIT_CMD_CHANNEL0    0x00
#define PIT_CMD_LOBYTE_HIBYTE 0x30
#define PIT_CMD_SQUARE_WAVE 0x06    /* Mode 3 */

/*
 * Ready Queue Implementation
 * Using a circular buffer with head and tail indices for O(1) operations
 */

/* Ready queue storage (circular buffer) */
static process_t *ready_queue[SCHEDULER_MAX_QUEUE_SIZE];
static uint32_t queue_head = 0;         /* Index of first element */
static uint32_t queue_tail = 0;         /* Index of next free slot */
static uint32_t queue_count = 0;        /* Number of elements in queue */

/* Current running process */
static process_t *current_process = NULL;

/* Scheduler state */
static bool scheduler_running = false;
static volatile int scheduler_lock = 0;

/* Scheduler statistics */
static scheduler_stats_t stats = {0};

/* Need reschedule flag (set in interrupt context, checked later) */
static volatile bool need_reschedule = false;

/**
 * Acquire scheduler lock
 */
static inline void sched_lock(void) {
    while (__sync_lock_test_and_set(&scheduler_lock, 1)) {
        __asm__ __volatile__("pause");
    }
}

/**
 * Release scheduler lock
 */
static inline void sched_unlock(void) {
    __sync_lock_release(&scheduler_lock);
}

/**
 * Check if ready queue is empty
 */
static inline bool queue_empty(void) {
    return queue_count == 0;
}

/**
 * Check if ready queue is full
 */
static inline bool queue_full(void) {
    return queue_count >= SCHEDULER_MAX_QUEUE_SIZE;
}

/**
 * Add process to back of ready queue
 */
static bool queue_enqueue(process_t *proc) {
    if (queue_full() || !proc) {
        return false;
    }

    ready_queue[queue_tail] = proc;
    queue_tail = (queue_tail + 1) % SCHEDULER_MAX_QUEUE_SIZE;
    queue_count++;

    return true;
}

/**
 * Remove process from front of ready queue
 */
static process_t* queue_dequeue(void) {
    if (queue_empty()) {
        return NULL;
    }

    process_t *proc = ready_queue[queue_head];
    ready_queue[queue_head] = NULL;
    queue_head = (queue_head + 1) % SCHEDULER_MAX_QUEUE_SIZE;
    queue_count--;

    return proc;
}

/**
 * Remove specific process from ready queue
 * This is O(n) but should be infrequent
 */
static bool queue_remove(process_t *proc) {
    if (queue_empty() || !proc) {
        return false;
    }

    /* Search for the process in the queue */
    uint32_t idx = queue_head;
    bool found = false;

    for (uint32_t i = 0; i < queue_count; i++) {
        if (ready_queue[idx] == proc) {
            found = true;

            /* Shift remaining elements */
            uint32_t current = idx;
            uint32_t next = (idx + 1) % SCHEDULER_MAX_QUEUE_SIZE;

            for (uint32_t j = i; j < queue_count - 1; j++) {
                ready_queue[current] = ready_queue[next];
                current = next;
                next = (next + 1) % SCHEDULER_MAX_QUEUE_SIZE;
            }

            /* Update tail and count */
            queue_tail = (queue_tail + SCHEDULER_MAX_QUEUE_SIZE - 1) % SCHEDULER_MAX_QUEUE_SIZE;
            ready_queue[queue_tail] = NULL;
            queue_count--;
            break;
        }
        idx = (idx + 1) % SCHEDULER_MAX_QUEUE_SIZE;
    }

    return found;
}

/**
 * Peek at front of queue without removing
 */
static process_t* queue_peek(void) {
    if (queue_empty()) {
        return NULL;
    }
    return ready_queue[queue_head];
}

/**
 * Initialize the PIT (Programmable Interval Timer)
 */
static void pit_init(void) {
    kprintf("[SCHED] Initializing PIT timer at %d Hz...\n", SCHEDULER_TICK_FREQUENCY);

    /* Calculate divisor for desired frequency */
    uint16_t divisor = PIT_DIVISOR;

    /* Send command byte: Channel 0, lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_LOBYTE_HIBYTE | PIT_CMD_SQUARE_WAVE);
    io_wait();

    /* Send divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();

    kprintf("[SCHED] PIT initialized with divisor %u\n", divisor);
}

/**
 * Unmask the timer IRQ in the PIC
 */
static void timer_irq_enable(void) {
    /* Read current mask from master PIC */
    uint8_t mask = inb(0x21);

    /* Clear bit 0 to unmask IRQ0 (timer) */
    mask &= ~0x01;

    /* Write back the mask */
    outb(0x21, mask);

    kprintf("[SCHED] Timer IRQ0 enabled\n");
}

/**
 * Timer interrupt handler
 */
void scheduler_tick(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Update statistics */
    stats.total_ticks++;

    /* Check if scheduler is running */
    if (!scheduler_running || !current_process) {
        return;
    }

    /* Update current process tick count */
    current_process->total_ticks++;

    /* Track idle time */
    if (current_process->pid == PID_IDLE) {
        stats.idle_ticks++;
    }

    /* Decrement time slice */
    if (current_process->time_slice > 0) {
        current_process->time_slice--;
    }

    /* Check if time slice expired */
    if (current_process->time_slice == 0) {
        /* Only reschedule if there are other processes ready */
        if (queue_count > 0) {
            kprintf("[SCHED] Time slice expired for '%s' (PID %u), rescheduling\n",
                    current_process->name, current_process->pid);
            need_reschedule = true;
        } else {
            /* No other processes, give current process another time slice */
            current_process->time_slice = SCHEDULER_TIME_SLICE;
        }
    }

    /* Perform context switch if needed
     * Note: In a real system, we'd do this more carefully to avoid
     * issues with interrupt nesting. For simplicity, we do it here.
     */
    if (need_reschedule) {
        need_reschedule = false;
        scheduler_schedule();
    }
}

/**
 * Initialize the scheduler
 */
void scheduler_init(void) {
    kprintf("[SCHED] Initializing Round-Robin Scheduler...\n");

    /* Clear the ready queue */
    for (uint32_t i = 0; i < SCHEDULER_MAX_QUEUE_SIZE; i++) {
        ready_queue[i] = NULL;
    }
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;

    /* Clear statistics */
    stats.total_ticks = 0;
    stats.context_switches = 0;
    stats.idle_ticks = 0;
    stats.processes_scheduled = 0;

    /* Initialize PIT timer */
    pit_init();

    /* Register timer interrupt handler */
    idt_register_handler(IRQ_TIMER, scheduler_tick);
    kprintf("[SCHED] Timer interrupt handler registered (IRQ %d)\n", IRQ_TIMER - IRQ_BASE);

    /* Enable timer IRQ */
    timer_irq_enable();

    /* Get the idle process (should already exist from process_init) */
    process_t *idle = process_get_by_pid(PID_IDLE);
    if (idle) {
        current_process = idle;
        current_process->time_slice = SCHEDULER_TIME_SLICE;
        process_set_state(idle, PROCESS_STATE_RUNNING);
        kprintf("[SCHED] Idle process set as initial process\n");
    } else {
        kprintf("[SCHED] WARNING: No idle process found!\n");
    }

    scheduler_running = true;

    kprintf("[SCHED] Scheduler initialized successfully\n");
    kprintf("[SCHED] Time slice: %d ticks (%d ms)\n",
            SCHEDULER_TIME_SLICE,
            (SCHEDULER_TIME_SLICE * 1000) / SCHEDULER_TICK_FREQUENCY);
}

/**
 * Add a process to the ready queue
 */
bool scheduler_add(process_t *proc) {
    if (!proc) {
        kprintf("[SCHED] Error: scheduler_add called with NULL process\n");
        return false;
    }

    if (proc->state != PROCESS_STATE_READY) {
        kprintf("[SCHED] Error: Process '%s' (PID %u) not in READY state (state=%s)\n",
                proc->name, proc->pid, process_state_string(proc->state));
        return false;
    }

    sched_lock();

    if (queue_full()) {
        sched_unlock();
        kprintf("[SCHED] Error: Ready queue full, cannot add '%s' (PID %u)\n",
                proc->name, proc->pid);
        return false;
    }

    /* Set initial time slice */
    if (proc->time_slice == 0) {
        proc->time_slice = SCHEDULER_TIME_SLICE;
    }

    bool result = queue_enqueue(proc);

    sched_unlock();

    if (result) {
        kprintf("[SCHED] Added '%s' (PID %u) to ready queue (queue size: %u)\n",
                proc->name, proc->pid, queue_count);
        stats.processes_scheduled++;
    }

    return result;
}

/**
 * Remove a process from the ready queue
 */
bool scheduler_remove(process_t *proc) {
    if (!proc) {
        kprintf("[SCHED] Error: scheduler_remove called with NULL process\n");
        return false;
    }

    sched_lock();

    bool result = queue_remove(proc);

    sched_unlock();

    if (result) {
        kprintf("[SCHED] Removed '%s' (PID %u) from ready queue (queue size: %u)\n",
                proc->name, proc->pid, queue_count);
    }

    return result;
}

/**
 * Select and switch to the next runnable process
 */
process_t* scheduler_schedule(void) {
    sched_lock();

    process_t *old_process = current_process;
    process_t *new_process = NULL;

    /* If current process is still runnable, put it back in queue */
    if (old_process && old_process->state == PROCESS_STATE_RUNNING) {
        old_process->state = PROCESS_STATE_READY;
        queue_enqueue(old_process);
    }

    /* Get next process from ready queue */
    new_process = queue_dequeue();

    /* If no process available, use idle process */
    if (!new_process) {
        new_process = process_get_by_pid(PID_IDLE);
        if (!new_process) {
            sched_unlock();
            kprintf("[SCHED] FATAL: No runnable process and no idle process!\n");
            /* Halt system */
            __asm__ __volatile__("cli; hlt");
            return NULL;
        }
    }

    /* If same process, just continue running */
    if (new_process == old_process) {
        new_process->state = PROCESS_STATE_RUNNING;
        new_process->time_slice = SCHEDULER_TIME_SLICE;
        sched_unlock();
        return new_process;
    }

    /* Set up new process */
    new_process->state = PROCESS_STATE_RUNNING;
    new_process->time_slice = SCHEDULER_TIME_SLICE;
    current_process = new_process;

    stats.context_switches++;

    kprintf("[SCHED] Context switch: '%s' (PID %u) -> '%s' (PID %u) [switch #%llu]\n",
            old_process ? old_process->name : "(none)",
            old_process ? old_process->pid : 0,
            new_process->name,
            new_process->pid,
            stats.context_switches);

    sched_unlock();

    /* Perform actual context switch */
    if (old_process) {
        context_switch(&old_process->context, &new_process->context);
    } else {
        /* First switch - no old context to save */
        context_switch_first(&new_process->context);
    }

    return new_process;
}

/**
 * Voluntarily yield the CPU
 */
void scheduler_yield(void) {
    if (!scheduler_running) {
        return;
    }

    kprintf("[SCHED] Process '%s' (PID %u) yielding CPU\n",
            current_process ? current_process->name : "(none)",
            current_process ? current_process->pid : 0);

    /* Disable interrupts during yield */
    interrupts_disable();

    /* Reset time slice and reschedule */
    if (current_process) {
        current_process->time_slice = 0;
    }

    scheduler_schedule();

    /* Re-enable interrupts */
    interrupts_enable();
}

/**
 * Get the currently running process
 */
process_t* scheduler_get_current(void) {
    return current_process;
}

/**
 * Set time slice for a process
 */
void scheduler_set_time_slice(process_t *proc, uint64_t ticks) {
    if (proc) {
        proc->time_slice = ticks;
        kprintf("[SCHED] Set time slice for '%s' (PID %u) to %llu ticks\n",
                proc->name, proc->pid, ticks);
    }
}

/**
 * Get scheduler statistics
 */
const scheduler_stats_t* scheduler_get_stats(void) {
    return &stats;
}

/**
 * Check if scheduler is running
 */
bool scheduler_is_running(void) {
    return scheduler_running;
}

/**
 * Dump scheduler state for debugging
 */
void scheduler_dump_state(void) {
    kprintf("[SCHED] ========== Scheduler State ==========\n");
    kprintf("[SCHED] Running: %s\n", scheduler_running ? "YES" : "NO");
    kprintf("[SCHED] Current process: %s (PID %u)\n",
            current_process ? current_process->name : "(none)",
            current_process ? current_process->pid : 0);
    kprintf("[SCHED] Time slice remaining: %llu ticks\n",
            current_process ? current_process->time_slice : 0);
    kprintf("[SCHED] Ready queue size: %u / %u\n", queue_count, SCHEDULER_MAX_QUEUE_SIZE);

    /* Dump ready queue contents */
    if (queue_count > 0) {
        kprintf("[SCHED] Ready queue contents:\n");
        uint32_t idx = queue_head;
        for (uint32_t i = 0; i < queue_count; i++) {
            process_t *p = ready_queue[idx];
            kprintf("[SCHED]   [%u] '%s' (PID %u, prio=%u)\n",
                    i, p ? p->name : "(null)",
                    p ? p->pid : 0,
                    p ? p->priority : 0);
            idx = (idx + 1) % SCHEDULER_MAX_QUEUE_SIZE;
        }
    }

    /* Dump statistics */
    kprintf("[SCHED] --- Statistics ---\n");
    kprintf("[SCHED] Total ticks:        %llu\n", stats.total_ticks);
    kprintf("[SCHED] Context switches:   %llu\n", stats.context_switches);
    /* Calculate percentage using integer math (avoid FPU in kernel) */
    uint64_t idle_percent = 0;
    if (stats.total_ticks > 0) {
        idle_percent = (stats.idle_ticks * 100) / stats.total_ticks;
    }
    kprintf("[SCHED] Idle ticks:         %llu (%llu%%)\n",
            stats.idle_ticks, idle_percent);
    kprintf("[SCHED] Processes scheduled: %llu\n", stats.processes_scheduled);
    kprintf("[SCHED] =====================================\n");
}
