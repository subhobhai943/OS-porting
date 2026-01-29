/**
 * AAAos Kernel - Local APIC Driver Implementation
 *
 * This file implements the Local Advanced Programmable Interrupt Controller
 * (Local APIC) driver, including:
 * - APIC detection and initialization
 * - APIC timer calibration and interrupt handling
 * - Inter-Processor Interrupt (IPI) support for SMP
 */

#include "apic.h"
#include "io.h"
#include "../../include/serial.h"

/* ============================================================================
 * Private Data
 * ============================================================================ */

/* APIC information */
static apic_info_t apic_info = {
    .base_address = APIC_DEFAULT_BASE,
    .id = 0,
    .version = 0,
    .max_lvt = 0,
    .enabled = false,
    .is_bsp = false,
    .x2apic_supported = false,
    .x2apic_enabled = false,
    .timer_frequency = 0,
    .ticks = 0
};

/* Virtual address for APIC MMIO (identity mapped in early kernel) */
static volatile uint32_t *apic_base = NULL;

/* Timer calibration values */
static uint32_t apic_timer_initial_count = 0;
static uint32_t apic_timer_ticks_per_ms = 0;

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * Check CPUID for APIC support
 */
static bool check_apic_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;

    /* Check CPUID leaf 1 */
    cpuid(1, &eax, &ebx, &ecx, &edx);

    /* Bit 9 of EDX indicates APIC presence */
    if (!(edx & (1 << 9))) {
        return false;
    }

    /* Check for x2APIC support (bit 21 of ECX) */
    apic_info.x2apic_supported = (ecx & (1 << 21)) != 0;

    return true;
}

/**
 * Read the APIC base address from MSR
 */
static uint64_t get_apic_base_msr(void) {
    uint64_t msr = rdmsr(APIC_BASE_MSR);
    return msr & 0xFFFFF000;  /* Mask off lower 12 bits (flags) */
}

/**
 * Check if this is the bootstrap processor
 */
static bool is_bsp(void) {
    uint64_t msr = rdmsr(APIC_BASE_MSR);
    return (msr & APIC_BASE_MSR_BSP) != 0;
}

/**
 * Enable APIC via MSR
 */
static void enable_apic_msr(void) {
    uint64_t msr = rdmsr(APIC_BASE_MSR);
    msr |= APIC_BASE_MSR_ENABLE;
    wrmsr(APIC_BASE_MSR, msr);
}

/**
 * Short delay using PIT
 */
static void pit_delay(uint32_t ms) {
    /* Configure PIT channel 0 for rate generator */
    uint32_t divisor = (PIT_FREQUENCY * ms) / 1000;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    /* Send command byte: channel 0, lobyte/hibyte, rate generator */
    outb(PIT_CMD, 0x34);
    io_wait();

    /* Send divisor */
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);
    io_wait();
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF);
    io_wait();

    /* Wait for one period by polling */
    /* Read channel 0 count until it wraps */
    outb(PIT_CMD, 0x00);  /* Latch count for channel 0 */
    io_wait();
    uint8_t lo = inb(PIT_CHANNEL0_DATA);
    io_wait();
    uint8_t hi = inb(PIT_CHANNEL0_DATA);
    uint16_t initial = (hi << 8) | lo;

    uint16_t current = initial;
    uint16_t last = initial;

    while (1) {
        outb(PIT_CMD, 0x00);
        io_wait();
        lo = inb(PIT_CHANNEL0_DATA);
        io_wait();
        hi = inb(PIT_CHANNEL0_DATA);
        current = (hi << 8) | lo;

        /* Check if we've wrapped around (count decreased past our target) */
        if (current > last && last < (divisor / 2)) {
            break;
        }
        last = current;

        /* Safety timeout using small busy-wait */
        for (volatile int i = 0; i < 100; i++);
    }
}

/**
 * Simple microsecond delay using a busy loop
 * Note: This is approximate and depends on CPU speed
 */
static void busy_delay_us(uint32_t us) {
    /* Approximate busy-wait (very rough estimate) */
    for (volatile uint32_t i = 0; i < us * 100; i++) {
        __asm__ __volatile__("pause");
    }
}

/* ============================================================================
 * APIC Register Access
 * ============================================================================ */

uint32_t apic_read(uint32_t reg) {
    if (apic_base == NULL) {
        return 0;
    }
    return apic_base[reg / 4];
}

void apic_write(uint32_t reg, uint32_t value) {
    if (apic_base == NULL) {
        return;
    }
    apic_base[reg / 4] = value;
}

/* ============================================================================
 * PIC Disable
 * ============================================================================ */

void apic_disable_pic(void) {
    kprintf("[APIC] Disabling legacy 8259 PIC...\n");

    /* Mask all interrupts on both PICs */
    outb(0x21, 0xFF);   /* Mask all on master PIC */
    outb(0xA1, 0xFF);   /* Mask all on slave PIC */
    io_wait();

    kprintf("[APIC] Legacy PIC disabled\n");
}

/* ============================================================================
 * APIC Core Functions
 * ============================================================================ */

bool apic_is_available(void) {
    return check_apic_cpuid();
}

bool apic_init(void) {
    kprintf("[APIC] Initializing Local APIC...\n");

    /* Check if APIC is available */
    if (!check_apic_cpuid()) {
        kprintf("[APIC] ERROR: APIC not available on this CPU!\n");
        return false;
    }

    kprintf("[APIC] APIC detected via CPUID\n");

    if (apic_info.x2apic_supported) {
        kprintf("[APIC] x2APIC mode supported (not enabled)\n");
    }

    /* Get APIC base address */
    apic_info.base_address = get_apic_base_msr();
    kprintf("[APIC] APIC base address: 0x%016llx\n", apic_info.base_address);

    /* Check if this is the BSP */
    apic_info.is_bsp = is_bsp();
    kprintf("[APIC] Processor type: %s\n", apic_info.is_bsp ? "BSP" : "AP");

    /* Set up the APIC base pointer (identity mapped for now) */
    apic_base = (volatile uint32_t *)apic_info.base_address;

    /* Enable APIC via MSR */
    enable_apic_msr();
    kprintf("[APIC] APIC enabled via MSR\n");

    /* Read APIC ID and version */
    uint32_t id_reg = apic_read(APIC_REG_ID);
    apic_info.id = (id_reg >> 24) & 0xFF;

    uint32_t version_reg = apic_read(APIC_REG_VERSION);
    apic_info.version = version_reg & 0xFF;
    apic_info.max_lvt = ((version_reg >> 16) & 0xFF) + 1;

    kprintf("[APIC] APIC ID: %d, Version: 0x%02x, Max LVT: %d\n",
            apic_info.id, apic_info.version, apic_info.max_lvt);

    /* Disable legacy PIC */
    apic_disable_pic();

    /* Set Task Priority Register to 0 (accept all interrupts) */
    apic_write(APIC_REG_TPR, 0);

    /* Set up Destination Format Register for flat model */
    apic_write(APIC_REG_DFR, 0xFFFFFFFF);

    /* Set up Logical Destination Register */
    apic_write(APIC_REG_LDR, (apic_read(APIC_REG_LDR) & 0x00FFFFFF) |
               ((uint32_t)(1 << apic_info.id) << 24));

    /* Configure Spurious Interrupt Vector Register */
    /* Enable APIC and set spurious vector */
    uint32_t svr = apic_read(APIC_REG_SVR);
    svr |= APIC_SVR_ENABLE;          /* Software enable */
    svr |= APIC_SPURIOUS_VECTOR;     /* Set spurious vector */
    apic_write(APIC_REG_SVR, svr);

    /* Mask all LVT entries initially */
    apic_write(APIC_REG_LVT_TIMER, APIC_LVT_MASKED);
    apic_write(APIC_REG_LVT_THERMAL, APIC_LVT_MASKED);
    apic_write(APIC_REG_LVT_PERF, APIC_LVT_MASKED);
    apic_write(APIC_REG_LVT_LINT0, APIC_LVT_MASKED);
    apic_write(APIC_REG_LVT_LINT1, APIC_LVT_MASKED);
    apic_write(APIC_REG_LVT_ERROR, APIC_ERROR_VECTOR);  /* Unmask error */

    /* Clear any pending errors */
    apic_write(APIC_REG_ESR, 0);
    apic_write(APIC_REG_ESR, 0);  /* Write twice per spec */

    /* Send EOI to clear any pending interrupts */
    apic_eoi();

    apic_info.enabled = true;

    kprintf("[APIC] Local APIC initialized successfully\n");

    return true;
}

void apic_enable(void) {
    uint32_t svr = apic_read(APIC_REG_SVR);
    svr |= APIC_SVR_ENABLE;
    apic_write(APIC_REG_SVR, svr);
    apic_info.enabled = true;
}

void apic_disable(void) {
    uint32_t svr = apic_read(APIC_REG_SVR);
    svr &= ~APIC_SVR_ENABLE;
    apic_write(APIC_REG_SVR, svr);
    apic_info.enabled = false;
}

uint8_t apic_get_id(void) {
    uint32_t id_reg = apic_read(APIC_REG_ID);
    return (id_reg >> 24) & 0xFF;
}

uint8_t apic_get_version(void) {
    uint32_t version_reg = apic_read(APIC_REG_VERSION);
    return version_reg & 0xFF;
}

void apic_eoi(void) {
    apic_write(APIC_REG_EOI, 0);
}

const apic_info_t* apic_get_info(void) {
    return &apic_info;
}

/* ============================================================================
 * APIC Timer Functions
 * ============================================================================ */

/**
 * Calibrate the APIC timer using PIT
 * Returns the number of APIC timer ticks per millisecond
 */
static uint32_t apic_timer_calibrate(void) {
    kprintf("[APIC] Calibrating APIC timer...\n");

    /* Set up PIT channel 2 for one-shot mode */
    /* We'll use it to measure a known time interval */

    /* Set APIC timer divide value to 16 */
    apic_write(APIC_REG_TIMER_DCR, APIC_TIMER_DIV_16);

    /* Set initial count to maximum */
    apic_write(APIC_REG_TIMER_ICR, 0xFFFFFFFF);

    /* Wait for approximately 10ms using PIT */
    /* Configure PIT channel 2 for 10ms */
    uint32_t pit_divisor = PIT_FREQUENCY / 100;  /* 10ms = 1/100 second */

    /* Gate speaker (enable channel 2 counting) */
    uint8_t speaker = inb(0x61);
    speaker = (speaker & 0xFC) | 0x01;  /* Gate on, speaker off */
    outb(0x61, speaker);

    /* Configure PIT channel 2: one-shot mode */
    outb(PIT_CMD, 0xB0);  /* Channel 2, lobyte/hibyte, one-shot */
    io_wait();
    outb(PIT_CHANNEL2_DATA, pit_divisor & 0xFF);
    io_wait();
    outb(PIT_CHANNEL2_DATA, (pit_divisor >> 8) & 0xFF);
    io_wait();

    /* Start counting by toggling gate */
    speaker = inb(0x61);
    outb(0x61, speaker & 0xFE);  /* Gate off */
    outb(0x61, speaker | 0x01);  /* Gate on - starts counting */

    /* Wait for PIT to count down (poll bit 5 of port 0x61) */
    while (!(inb(0x61) & 0x20)) {
        __asm__ __volatile__("pause");
    }

    /* Read current APIC timer count */
    uint32_t final_count = apic_read(APIC_REG_TIMER_CCR);

    /* Calculate ticks elapsed */
    uint32_t ticks_10ms = 0xFFFFFFFF - final_count;

    /* Stop the timer */
    apic_write(APIC_REG_TIMER_ICR, 0);

    /* Calculate ticks per ms (divide by 10 since we measured 10ms) */
    uint32_t ticks_per_ms = ticks_10ms / 10;

    kprintf("[APIC] Timer calibration: %u ticks in 10ms\n", ticks_10ms);
    kprintf("[APIC] Timer ticks per ms: %u\n", ticks_per_ms);

    /* Calculate approximate timer frequency (accounting for divide by 16) */
    apic_info.timer_frequency = ticks_per_ms * 1000 * 16;
    kprintf("[APIC] Estimated bus frequency: %u MHz\n",
            apic_info.timer_frequency / 1000000);

    return ticks_per_ms;
}

bool apic_timer_init(uint32_t frequency) {
    kprintf("[APIC] Initializing APIC timer at %u Hz...\n", frequency);

    if (!apic_info.enabled) {
        kprintf("[APIC] ERROR: APIC not enabled!\n");
        return false;
    }

    if (frequency == 0) {
        kprintf("[APIC] ERROR: Invalid frequency!\n");
        return false;
    }

    /* Calibrate the timer */
    apic_timer_ticks_per_ms = apic_timer_calibrate();

    if (apic_timer_ticks_per_ms == 0) {
        kprintf("[APIC] ERROR: Timer calibration failed!\n");
        return false;
    }

    /* Calculate initial count for desired frequency */
    /* ticks_per_interrupt = ticks_per_second / frequency */
    /* ticks_per_second = ticks_per_ms * 1000 */
    uint32_t ticks_per_second = apic_timer_ticks_per_ms * 1000;
    apic_timer_initial_count = ticks_per_second / frequency;

    kprintf("[APIC] Timer initial count: %u (for %u Hz)\n",
            apic_timer_initial_count, frequency);

    /* Set divide configuration */
    apic_write(APIC_REG_TIMER_DCR, APIC_TIMER_DIV_16);

    /* Configure LVT Timer: periodic mode, unmasked, vector */
    uint32_t lvt_timer = APIC_TIMER_VECTOR;           /* Vector number */
    lvt_timer |= APIC_TIMER_MODE_PERIODIC;            /* Periodic mode */
    /* Don't set MASKED bit - we want interrupts */
    apic_write(APIC_REG_LVT_TIMER, lvt_timer);

    /* Set initial count - this starts the timer */
    apic_write(APIC_REG_TIMER_ICR, apic_timer_initial_count);

    apic_info.ticks = 0;

    kprintf("[APIC] Timer started in periodic mode\n");

    return true;
}

void apic_timer_stop(void) {
    /* Mask the timer LVT entry */
    uint32_t lvt_timer = apic_read(APIC_REG_LVT_TIMER);
    lvt_timer |= APIC_LVT_MASKED;
    apic_write(APIC_REG_LVT_TIMER, lvt_timer);

    /* Set initial count to 0 */
    apic_write(APIC_REG_TIMER_ICR, 0);

    kprintf("[APIC] Timer stopped\n");
}

uint32_t apic_timer_get_count(void) {
    return apic_read(APIC_REG_TIMER_CCR);
}

uint64_t apic_timer_get_ticks(void) {
    return apic_info.ticks;
}

/* ============================================================================
 * Interrupt Handlers
 * ============================================================================ */

void apic_timer_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Increment tick counter */
    apic_info.ticks++;

    /* Acknowledge the interrupt */
    apic_eoi();

    /* Here you would typically:
     * 1. Update system time
     * 2. Check if current process needs to be preempted
     * 3. Call scheduler if needed
     */
}

void apic_spurious_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Spurious interrupts should NOT send EOI */
    kprintf("[APIC] Spurious interrupt received\n");
}

void apic_error_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Read and clear error status */
    apic_write(APIC_REG_ESR, 0);
    uint32_t esr = apic_read(APIC_REG_ESR);

    kprintf("[APIC] ERROR: APIC error interrupt, ESR=0x%08x\n", esr);

    if (esr & (1 << 0)) kprintf("[APIC]   - Send Checksum Error\n");
    if (esr & (1 << 1)) kprintf("[APIC]   - Receive Checksum Error\n");
    if (esr & (1 << 2)) kprintf("[APIC]   - Send Accept Error\n");
    if (esr & (1 << 3)) kprintf("[APIC]   - Receive Accept Error\n");
    if (esr & (1 << 4)) kprintf("[APIC]   - Redirectable IPI\n");
    if (esr & (1 << 5)) kprintf("[APIC]   - Send Illegal Vector\n");
    if (esr & (1 << 6)) kprintf("[APIC]   - Received Illegal Vector\n");
    if (esr & (1 << 7)) kprintf("[APIC]   - Illegal Register Address\n");

    apic_eoi();
}

/* ============================================================================
 * Inter-Processor Interrupt (IPI) Functions
 * ============================================================================ */

void apic_ipi_wait(void) {
    /* Wait for delivery status bit to clear */
    while (apic_read(APIC_REG_ICR_LOW) & (1 << 12)) {
        __asm__ __volatile__("pause");
    }
}

bool apic_send_ipi(uint8_t dest_apic, uint8_t vector) {
    if (!apic_info.enabled) {
        return false;
    }

    /* Wait for any pending IPI to complete */
    apic_ipi_wait();

    /* Set destination APIC ID */
    apic_write(APIC_REG_ICR_HIGH, ((uint32_t)dest_apic) << 24);

    /* Send IPI: fixed delivery, physical destination, edge triggered, assert */
    uint32_t icr_low = vector;
    icr_low |= APIC_ICR_DELIVERY_FIXED;
    icr_low |= APIC_ICR_DEST_PHYSICAL;
    icr_low |= APIC_ICR_LEVEL_ASSERT;
    icr_low |= APIC_ICR_TRIGGER_EDGE;
    icr_low |= APIC_ICR_DEST_NOSHORTHAND;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    return true;
}

bool apic_send_ipi_all(uint8_t vector) {
    if (!apic_info.enabled) {
        return false;
    }

    /* Wait for any pending IPI to complete */
    apic_ipi_wait();

    /* Send IPI to all excluding self */
    uint32_t icr_low = vector;
    icr_low |= APIC_ICR_DELIVERY_FIXED;
    icr_low |= APIC_ICR_LEVEL_ASSERT;
    icr_low |= APIC_ICR_TRIGGER_EDGE;
    icr_low |= APIC_ICR_DEST_ALL_EXC_SELF;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    return true;
}

bool apic_send_ipi_self(uint8_t vector) {
    if (!apic_info.enabled) {
        return false;
    }

    /* Wait for any pending IPI to complete */
    apic_ipi_wait();

    /* Send IPI to self */
    uint32_t icr_low = vector;
    icr_low |= APIC_ICR_DELIVERY_FIXED;
    icr_low |= APIC_ICR_LEVEL_ASSERT;
    icr_low |= APIC_ICR_TRIGGER_EDGE;
    icr_low |= APIC_ICR_DEST_SELF;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    return true;
}

bool apic_send_init_ipi(uint8_t dest_apic) {
    if (!apic_info.enabled) {
        return false;
    }

    kprintf("[APIC] Sending INIT IPI to APIC %d\n", dest_apic);

    /* Wait for any pending IPI to complete */
    apic_ipi_wait();

    /* Set destination */
    apic_write(APIC_REG_ICR_HIGH, ((uint32_t)dest_apic) << 24);

    /* Send INIT IPI */
    uint32_t icr_low = 0;
    icr_low |= APIC_ICR_DELIVERY_INIT;
    icr_low |= APIC_ICR_DEST_PHYSICAL;
    icr_low |= APIC_ICR_LEVEL_ASSERT;
    icr_low |= APIC_ICR_TRIGGER_LEVEL;
    icr_low |= APIC_ICR_DEST_NOSHORTHAND;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    /* Wait 10ms */
    busy_delay_us(10000);

    /* Send INIT de-assert */
    icr_low = 0;
    icr_low |= APIC_ICR_DELIVERY_INIT;
    icr_low |= APIC_ICR_DEST_PHYSICAL;
    icr_low |= APIC_ICR_LEVEL_DEASSERT;
    icr_low |= APIC_ICR_TRIGGER_LEVEL;
    icr_low |= APIC_ICR_DEST_NOSHORTHAND;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    return true;
}

bool apic_send_startup_ipi(uint8_t dest_apic, uint8_t vector) {
    if (!apic_info.enabled) {
        return false;
    }

    kprintf("[APIC] Sending SIPI to APIC %d, vector 0x%02x\n", dest_apic, vector);

    /* Wait for any pending IPI to complete */
    apic_ipi_wait();

    /* Set destination */
    apic_write(APIC_REG_ICR_HIGH, ((uint32_t)dest_apic) << 24);

    /* Send SIPI */
    uint32_t icr_low = vector;
    icr_low |= APIC_ICR_DELIVERY_STARTUP;
    icr_low |= APIC_ICR_DEST_PHYSICAL;
    icr_low |= APIC_ICR_LEVEL_ASSERT;
    icr_low |= APIC_ICR_TRIGGER_EDGE;
    icr_low |= APIC_ICR_DEST_NOSHORTHAND;

    apic_write(APIC_REG_ICR_LOW, icr_low);

    /* Wait for delivery */
    apic_ipi_wait();

    /* Wait 200us */
    busy_delay_us(200);

    return true;
}
