/**
 * AAAos Kernel - Programmable Interval Timer (PIT) Driver Implementation
 *
 * This driver configures the Intel 8253/8254 PIT chip for system timing.
 * Channel 0 is used to generate periodic interrupts (IRQ0, vector 32).
 */

#include "pit.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"

/* Timer state */
static volatile uint64_t pit_ticks = 0;         /* Tick counter since boot */
static uint32_t pit_frequency = 0;              /* Configured frequency in Hz */
static uint32_t pit_divisor = 0;                /* PIT divisor value */
static uint32_t ms_per_tick = 0;                /* Milliseconds per tick (integer part) */
static uint32_t ms_remainder = 0;               /* Fractional ms accumulator */
static uint32_t ticks_per_ms = 0;               /* Ticks per millisecond */

/* PIC ports for unmasking IRQ0 */
#define PIC_MASTER_DATA     0x21

/**
 * Set the PIT channel 0 divisor
 *
 * The actual frequency will be: PIT_BASE_FREQUENCY / divisor
 *
 * @param divisor 16-bit divisor value (0 = 65536)
 */
static void pit_set_divisor(uint16_t divisor) {
    /* Disable interrupts during configuration */
    interrupts_disable();

    /* Command: Channel 0, lobyte/hibyte access, rate generator, binary mode */
    uint8_t command = PIT_CMD_CHANNEL0 | PIT_CMD_LOHIBYTE | PIT_CMD_MODE2 | PIT_CMD_BINARY;
    outb(PIT_COMMAND, command);

    /* Send divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    io_wait();

    /* Re-enable interrupts */
    interrupts_enable();
}

/**
 * Unmask IRQ0 in the PIC to enable timer interrupts
 */
static void pit_unmask_irq(void) {
    uint8_t mask = inb(PIC_MASTER_DATA);
    mask &= ~(1 << 0);  /* Clear bit 0 to unmask IRQ0 */
    outb(PIC_MASTER_DATA, mask);
}

/**
 * Initialize the Programmable Interval Timer
 */
void pit_init(uint32_t frequency) {
    kprintf("[PIT] Initializing Programmable Interval Timer...\n");

    /* Validate frequency */
    if (frequency == 0) {
        kprintf("[PIT] ERROR: Invalid frequency 0, using default %u Hz\n", PIT_DEFAULT_FREQUENCY);
        frequency = PIT_DEFAULT_FREQUENCY;
    }

    /* Calculate divisor */
    /* divisor = base_freq / desired_freq */
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;

    /* Clamp divisor to valid 16-bit range (1-65536, where 0 means 65536) */
    if (divisor > 65535) {
        divisor = 65535;
        kprintf("[PIT] WARNING: Frequency too low, clamping divisor to 65535\n");
    }
    if (divisor < 1) {
        divisor = 1;
        kprintf("[PIT] WARNING: Frequency too high, clamping divisor to 1\n");
    }

    /* Calculate actual frequency (may differ slightly due to integer division) */
    uint32_t actual_frequency = PIT_BASE_FREQUENCY / divisor;

    /* Store configuration */
    pit_frequency = actual_frequency;
    pit_divisor = divisor;

    /* Calculate timing helpers */
    if (actual_frequency >= 1000) {
        ticks_per_ms = actual_frequency / 1000;
        ms_per_tick = 0;
    } else {
        ticks_per_ms = 1;
        ms_per_tick = 1000 / actual_frequency;
    }

    kprintf("[PIT] Requested frequency: %u Hz\n", frequency);
    kprintf("[PIT] Divisor: %u (0x%04X)\n", divisor, divisor);
    kprintf("[PIT] Actual frequency: %u Hz\n", actual_frequency);
    kprintf("[PIT] Tick period: %u.%03u ms\n",
            1000 / actual_frequency,
            (1000000 / actual_frequency) % 1000);

    /* Register interrupt handler */
    idt_register_handler(IRQ_TIMER, pit_handler);
    kprintf("[PIT] Registered handler for IRQ0 (vector %u)\n", IRQ_TIMER);

    /* Configure the PIT hardware */
    pit_set_divisor((uint16_t)divisor);

    /* Unmask IRQ0 in the PIC */
    pit_unmask_irq();

    /* Reset tick counter */
    pit_ticks = 0;
    ms_remainder = 0;

    kprintf("[PIT] Timer initialized and running\n");
}

/**
 * Timer interrupt handler
 */
void pit_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Increment tick counter */
    pit_ticks++;

    /* Optional: Periodic status output for debugging (every 10 seconds at 1000 Hz) */
#ifdef PIT_DEBUG_TICKS
    if (pit_ticks % (pit_frequency * 10) == 0) {
        kprintf("[PIT] Uptime: %llu seconds\n", pit_ticks / pit_frequency);
    }
#endif
}

/**
 * Get current tick count
 */
uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

/**
 * Get system uptime in milliseconds
 */
uint64_t pit_get_uptime_ms(void) {
    if (pit_frequency == 0) {
        return 0;
    }

    /* Calculate milliseconds: ticks * 1000 / frequency */
    /* Use 64-bit arithmetic to avoid overflow */
    uint64_t ticks = pit_ticks;
    return (ticks * 1000) / pit_frequency;
}

/**
 * Sleep for specified milliseconds (busy wait)
 */
void pit_sleep_ms(uint32_t ms) {
    if (pit_frequency == 0) {
        kprintf("[PIT] WARNING: pit_sleep_ms called before initialization\n");
        return;
    }

    /* Calculate number of ticks to wait */
    /* ticks = ms * frequency / 1000 */
    uint64_t ticks_to_wait = ((uint64_t)ms * pit_frequency) / 1000;

    /* Ensure at least 1 tick for very short sleeps */
    if (ticks_to_wait == 0 && ms > 0) {
        ticks_to_wait = 1;
    }

    pit_sleep_ticks(ticks_to_wait);
}

/**
 * Sleep for specified number of ticks (busy wait)
 */
void pit_sleep_ticks(uint64_t ticks) {
    if (pit_frequency == 0) {
        kprintf("[PIT] WARNING: pit_sleep_ticks called before initialization\n");
        return;
    }

    uint64_t start_ticks = pit_ticks;
    uint64_t target_ticks = start_ticks + ticks;

    /* Handle potential overflow (unlikely but safe) */
    if (target_ticks < start_ticks) {
        /* Wait for counter to wrap around */
        while (pit_ticks >= start_ticks) {
            __asm__ __volatile__("hlt");  /* Halt until next interrupt */
        }
    }

    /* Wait for target tick count */
    while (pit_ticks < target_ticks) {
        __asm__ __volatile__("hlt");  /* Halt until next interrupt */
    }
}

/**
 * Get the configured PIT frequency
 */
uint32_t pit_get_frequency(void) {
    return pit_frequency;
}

/**
 * Disable the PIT
 */
void pit_disable(void) {
    kprintf("[PIT] Disabling timer...\n");

    /* Set maximum divisor (longest period = ~55ms) */
    pit_set_divisor(0);  /* 0 is interpreted as 65536 */

    /* Mask IRQ0 in the PIC */
    uint8_t mask = inb(PIC_MASTER_DATA);
    mask |= (1 << 0);  /* Set bit 0 to mask IRQ0 */
    outb(PIC_MASTER_DATA, mask);

    kprintf("[PIT] Timer disabled\n");
}
