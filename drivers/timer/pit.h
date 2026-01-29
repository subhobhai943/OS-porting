/**
 * AAAos Kernel - Programmable Interval Timer (PIT) Driver
 *
 * The Intel 8253/8254 PIT provides system timing services including:
 * - Periodic timer interrupts for scheduling
 * - System uptime tracking
 * - Sleep/delay functions
 *
 * The PIT has a base frequency of 1193182 Hz and three channels:
 * - Channel 0: Connected to IRQ0 for system timer
 * - Channel 1: Historically used for DRAM refresh (unused)
 * - Channel 2: Connected to PC speaker
 */

#ifndef _AAAOS_DRIVERS_PIT_H
#define _AAAOS_DRIVERS_PIT_H

#include "../../kernel/include/types.h"
#include "../../kernel/arch/x86_64/include/idt.h"

/* PIT I/O Ports */
#define PIT_CHANNEL0_DATA   0x40    /* Channel 0 data port (read/write) */
#define PIT_CHANNEL1_DATA   0x41    /* Channel 1 data port (read/write) */
#define PIT_CHANNEL2_DATA   0x42    /* Channel 2 data port (read/write) */
#define PIT_COMMAND         0x43    /* Mode/Command register (write only) */

/* PIT Base Frequency */
#define PIT_BASE_FREQUENCY  1193182 /* Hz */

/* PIT Command Register Bits */
/* Channel select (bits 6-7) */
#define PIT_CMD_CHANNEL0    (0 << 6)    /* Select channel 0 */
#define PIT_CMD_CHANNEL1    (1 << 6)    /* Select channel 1 */
#define PIT_CMD_CHANNEL2    (2 << 6)    /* Select channel 2 */
#define PIT_CMD_READBACK    (3 << 6)    /* Read-back command (8254 only) */

/* Access mode (bits 4-5) */
#define PIT_CMD_LATCH       (0 << 4)    /* Latch count value command */
#define PIT_CMD_LOBYTE      (1 << 4)    /* Access low byte only */
#define PIT_CMD_HIBYTE      (2 << 4)    /* Access high byte only */
#define PIT_CMD_LOHIBYTE    (3 << 4)    /* Access low byte then high byte */

/* Operating mode (bits 1-3) */
#define PIT_CMD_MODE0       (0 << 1)    /* Interrupt on terminal count */
#define PIT_CMD_MODE1       (1 << 1)    /* Hardware re-triggerable one-shot */
#define PIT_CMD_MODE2       (2 << 1)    /* Rate generator */
#define PIT_CMD_MODE3       (3 << 1)    /* Square wave generator */
#define PIT_CMD_MODE4       (4 << 1)    /* Software triggered strobe */
#define PIT_CMD_MODE5       (5 << 1)    /* Hardware triggered strobe */

/* BCD/Binary mode (bit 0) */
#define PIT_CMD_BINARY      (0 << 0)    /* 16-bit binary counter */
#define PIT_CMD_BCD         (1 << 0)    /* 4-digit BCD counter */

/* Default configuration */
#define PIT_DEFAULT_FREQUENCY   1000    /* 1000 Hz = 1ms tick */

/**
 * Initialize the Programmable Interval Timer
 *
 * Configures PIT channel 0 to generate periodic interrupts at the
 * specified frequency. Registers the timer interrupt handler with the IDT.
 *
 * @param frequency Desired tick frequency in Hz (typically 100-1000 Hz)
 */
void pit_init(uint32_t frequency);

/**
 * Timer interrupt handler
 *
 * Called by the IDT when IRQ0 fires. Updates tick count and uptime.
 * Should not be called directly.
 *
 * @param frame Interrupt frame containing CPU state
 */
void pit_handler(interrupt_frame_t *frame);

/**
 * Get current tick count
 *
 * Returns the number of timer ticks since system boot.
 * Thread-safe (uses volatile counter).
 *
 * @return Number of ticks since boot
 */
uint64_t pit_get_ticks(void);

/**
 * Get system uptime in milliseconds
 *
 * Returns the time elapsed since system boot in milliseconds.
 * Calculated from tick count and configured frequency.
 *
 * @return Milliseconds since boot
 */
uint64_t pit_get_uptime_ms(void);

/**
 * Sleep for specified milliseconds
 *
 * Busy-waits for the specified duration. Interrupts must be enabled.
 * This is a blocking call that polls the tick counter.
 *
 * @param ms Number of milliseconds to sleep
 */
void pit_sleep_ms(uint32_t ms);

/**
 * Sleep for specified number of ticks
 *
 * Busy-waits for the specified number of timer ticks.
 * Interrupts must be enabled.
 *
 * @param ticks Number of ticks to sleep
 */
void pit_sleep_ticks(uint64_t ticks);

/**
 * Get the configured PIT frequency
 *
 * @return Current tick frequency in Hz
 */
uint32_t pit_get_frequency(void);

/**
 * Disable the PIT
 *
 * Stops the timer by setting a very long period.
 * Useful when switching to APIC timer.
 */
void pit_disable(void);

#endif /* _AAAOS_DRIVERS_PIT_H */
