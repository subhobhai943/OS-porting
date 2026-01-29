/**
 * AAAos Kernel - Round-Robin Scheduler
 *
 * Implements preemptive round-robin scheduling with time slices.
 * The scheduler maintains a ready queue of runnable processes and
 * selects the next process to run when triggered by timer interrupt.
 */

#ifndef _AAAOS_SCHED_SCHEDULER_H
#define _AAAOS_SCHED_SCHEDULER_H

#include "../include/types.h"
#include "../proc/process.h"

/* Scheduler configuration */
#define SCHEDULER_TIME_SLICE        10      /* Default time slice in ticks */
#define SCHEDULER_MAX_QUEUE_SIZE    256     /* Maximum processes in ready queue */

/* Timer frequency (PIT runs at ~1193182 Hz, we'll divide for ~100 Hz) */
#define SCHEDULER_TICK_FREQUENCY    100     /* Ticks per second (Hz) */
#define PIT_BASE_FREQUENCY          1193182 /* PIT base oscillator frequency */
#define PIT_DIVISOR                 (PIT_BASE_FREQUENCY / SCHEDULER_TICK_FREQUENCY)

/**
 * Ready queue node for linked-list implementation
 * Uses intrusive linking through the process structure
 */
typedef struct sched_node {
    process_t *process;             /* Process in this queue slot */
    struct sched_node *next;        /* Next node in queue */
} sched_node_t;

/**
 * Scheduler statistics
 */
typedef struct {
    uint64_t total_ticks;           /* Total timer ticks since boot */
    uint64_t context_switches;      /* Number of context switches */
    uint64_t idle_ticks;            /* Ticks spent in idle process */
    uint64_t processes_scheduled;   /* Total number of processes scheduled */
} scheduler_stats_t;

/**
 * Initialize the scheduler subsystem
 * - Sets up the ready queue
 * - Configures the PIT timer for preemption
 * - Registers the timer interrupt handler
 */
void scheduler_init(void);

/**
 * Add a process to the ready queue
 * The process must be in PROCESS_STATE_READY state.
 *
 * @param proc Pointer to the process to add
 * @return true on success, false if queue is full or proc is NULL
 */
bool scheduler_add(process_t *proc);

/**
 * Remove a process from the ready queue
 * Used when a process blocks, terminates, or needs to be taken off the queue.
 *
 * @param proc Pointer to the process to remove
 * @return true if process was found and removed, false otherwise
 */
bool scheduler_remove(process_t *proc);

/**
 * Select and switch to the next runnable process
 * Implements the round-robin scheduling algorithm:
 * 1. Save current process context (if any)
 * 2. Pick next process from ready queue
 * 3. Restore new process context and switch
 *
 * @return Pointer to the newly scheduled process
 */
process_t* scheduler_schedule(void);

/**
 * Voluntarily yield the CPU to another process
 * The current process is moved to the back of the ready queue
 * and the scheduler picks the next process to run.
 */
void scheduler_yield(void);

/**
 * Timer interrupt handler - called on every timer tick
 * - Decrements the current process's time slice
 * - Triggers rescheduling when time slice expires
 * - Updates scheduler statistics
 *
 * @param frame Interrupt frame (from timer ISR)
 */
void scheduler_tick(interrupt_frame_t *frame);

/**
 * Get the currently running process
 * @return Pointer to current process, or NULL if none
 */
process_t* scheduler_get_current(void);

/**
 * Set the time slice for a process
 * @param proc Process to modify
 * @param ticks Number of timer ticks for the time slice
 */
void scheduler_set_time_slice(process_t *proc, uint64_t ticks);

/**
 * Get scheduler statistics
 * @return Pointer to scheduler statistics structure
 */
const scheduler_stats_t* scheduler_get_stats(void);

/**
 * Check if the scheduler is running
 * @return true if scheduler has been initialized and is active
 */
bool scheduler_is_running(void);

/**
 * Dump scheduler state to serial console (for debugging)
 */
void scheduler_dump_state(void);

/**
 * Context switch function (implemented in assembly)
 * Saves the current CPU context and restores the new one.
 *
 * @param old_context Pointer to save current context (can be NULL for first switch)
 * @param new_context Pointer to context to restore
 */
extern void context_switch(cpu_context_t *old_context, cpu_context_t *new_context);

/**
 * Start the first process (no context to save)
 * Used when starting the scheduler for the first time.
 *
 * @param context Pointer to the context to restore
 */
extern void context_switch_first(cpu_context_t *context);

#endif /* _AAAOS_SCHED_SCHEDULER_H */
