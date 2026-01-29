/**
 * AAAos Kernel - Local APIC Driver
 *
 * The Local Advanced Programmable Interrupt Controller (Local APIC) provides
 * per-processor interrupt handling, a local timer, and inter-processor
 * interrupt (IPI) capabilities for SMP systems.
 */

#ifndef _AAAOS_ARCH_APIC_H
#define _AAAOS_ARCH_APIC_H

#include "../../include/types.h"
#include "include/idt.h"

/* ============================================================================
 * APIC Constants
 * ============================================================================ */

/* APIC Base Address MSR */
#define APIC_BASE_MSR           0x1B
#define APIC_BASE_MSR_ENABLE    (1 << 11)   /* Global APIC enable */
#define APIC_BASE_MSR_BSP       (1 << 8)    /* Bootstrap processor flag */
#define APIC_BASE_MSR_X2APIC    (1 << 10)   /* x2APIC mode enable */

/* Default APIC Base Address (physical) */
#define APIC_DEFAULT_BASE       0xFEE00000

/* ============================================================================
 * APIC Register Offsets (MMIO, relative to APIC base address)
 * ============================================================================ */

#define APIC_REG_ID             0x020   /* Local APIC ID */
#define APIC_REG_VERSION        0x030   /* Local APIC Version */
#define APIC_REG_TPR            0x080   /* Task Priority Register */
#define APIC_REG_APR            0x090   /* Arbitration Priority Register */
#define APIC_REG_PPR            0x0A0   /* Processor Priority Register */
#define APIC_REG_EOI            0x0B0   /* End-Of-Interrupt Register */
#define APIC_REG_RRD            0x0C0   /* Remote Read Register */
#define APIC_REG_LDR            0x0D0   /* Logical Destination Register */
#define APIC_REG_DFR            0x0E0   /* Destination Format Register */
#define APIC_REG_SVR            0x0F0   /* Spurious Interrupt Vector Register */
#define APIC_REG_ISR0           0x100   /* In-Service Register (bits 0-31) */
#define APIC_REG_ISR1           0x110   /* In-Service Register (bits 32-63) */
#define APIC_REG_ISR2           0x120   /* In-Service Register (bits 64-95) */
#define APIC_REG_ISR3           0x130   /* In-Service Register (bits 96-127) */
#define APIC_REG_ISR4           0x140   /* In-Service Register (bits 128-159) */
#define APIC_REG_ISR5           0x150   /* In-Service Register (bits 160-191) */
#define APIC_REG_ISR6           0x160   /* In-Service Register (bits 192-223) */
#define APIC_REG_ISR7           0x170   /* In-Service Register (bits 224-255) */
#define APIC_REG_TMR0           0x180   /* Trigger Mode Register (bits 0-31) */
#define APIC_REG_IRR0           0x200   /* Interrupt Request Register (bits 0-31) */
#define APIC_REG_ESR            0x280   /* Error Status Register */
#define APIC_REG_LVT_CMCI       0x2F0   /* LVT Corrected Machine Check Interrupt */
#define APIC_REG_ICR_LOW        0x300   /* Interrupt Command Register (bits 0-31) */
#define APIC_REG_ICR_HIGH       0x310   /* Interrupt Command Register (bits 32-63) */
#define APIC_REG_LVT_TIMER      0x320   /* LVT Timer Register */
#define APIC_REG_LVT_THERMAL    0x330   /* LVT Thermal Sensor Register */
#define APIC_REG_LVT_PERF       0x340   /* LVT Performance Monitoring Register */
#define APIC_REG_LVT_LINT0      0x350   /* LVT LINT0 Register */
#define APIC_REG_LVT_LINT1      0x360   /* LVT LINT1 Register */
#define APIC_REG_LVT_ERROR      0x370   /* LVT Error Register */
#define APIC_REG_TIMER_ICR      0x380   /* Timer Initial Count Register */
#define APIC_REG_TIMER_CCR      0x390   /* Timer Current Count Register */
#define APIC_REG_TIMER_DCR      0x3E0   /* Timer Divide Configuration Register */

/* ============================================================================
 * APIC Register Field Definitions
 * ============================================================================ */

/* Spurious Interrupt Vector Register (SVR) bits */
#define APIC_SVR_ENABLE         (1 << 8)    /* APIC Software Enable */
#define APIC_SVR_FOCUS_DISABLE  (1 << 9)    /* Focus Processor Checking */
#define APIC_SVR_EOI_BROADCAST  (1 << 12)   /* EOI Broadcast Suppression */

/* LVT Entry bits */
#define APIC_LVT_MASKED         (1 << 16)   /* Interrupt masked */
#define APIC_LVT_LEVEL          (1 << 15)   /* Level triggered (vs edge) */
#define APIC_LVT_REMOTE_IRR     (1 << 14)   /* Remote IRR flag */
#define APIC_LVT_ACTIVE_LOW     (1 << 13)   /* Active low polarity */
#define APIC_LVT_PENDING        (1 << 12)   /* Delivery status pending */

/* LVT Timer modes */
#define APIC_TIMER_MODE_ONESHOT     (0 << 17)   /* One-shot mode */
#define APIC_TIMER_MODE_PERIODIC    (1 << 17)   /* Periodic mode */
#define APIC_TIMER_MODE_TSC_DEADLINE (2 << 17)  /* TSC-Deadline mode */

/* Timer divide configuration values */
#define APIC_TIMER_DIV_1        0x0B    /* Divide by 1 */
#define APIC_TIMER_DIV_2        0x00    /* Divide by 2 */
#define APIC_TIMER_DIV_4        0x01    /* Divide by 4 */
#define APIC_TIMER_DIV_8        0x02    /* Divide by 8 */
#define APIC_TIMER_DIV_16       0x03    /* Divide by 16 */
#define APIC_TIMER_DIV_32       0x08    /* Divide by 32 */
#define APIC_TIMER_DIV_64       0x09    /* Divide by 64 */
#define APIC_TIMER_DIV_128      0x0A    /* Divide by 128 */

/* ICR Delivery modes */
#define APIC_ICR_DELIVERY_FIXED     (0 << 8)    /* Fixed delivery */
#define APIC_ICR_DELIVERY_LOWPRI    (1 << 8)    /* Lowest priority */
#define APIC_ICR_DELIVERY_SMI       (2 << 8)    /* SMI */
#define APIC_ICR_DELIVERY_NMI       (4 << 8)    /* NMI */
#define APIC_ICR_DELIVERY_INIT      (5 << 8)    /* INIT */
#define APIC_ICR_DELIVERY_STARTUP   (6 << 8)    /* Startup IPI */

/* ICR Destination modes */
#define APIC_ICR_DEST_PHYSICAL      (0 << 11)   /* Physical destination */
#define APIC_ICR_DEST_LOGICAL       (1 << 11)   /* Logical destination */

/* ICR Level/Trigger */
#define APIC_ICR_LEVEL_DEASSERT     (0 << 14)   /* Level de-assert */
#define APIC_ICR_LEVEL_ASSERT       (1 << 14)   /* Level assert */
#define APIC_ICR_TRIGGER_EDGE       (0 << 15)   /* Edge triggered */
#define APIC_ICR_TRIGGER_LEVEL      (1 << 15)   /* Level triggered */

/* ICR Destination shortcuts */
#define APIC_ICR_DEST_NOSHORTHAND   (0 << 18)   /* No shorthand */
#define APIC_ICR_DEST_SELF          (1 << 18)   /* Self */
#define APIC_ICR_DEST_ALL_INC_SELF  (2 << 18)   /* All including self */
#define APIC_ICR_DEST_ALL_EXC_SELF  (3 << 18)   /* All excluding self */

/* ============================================================================
 * APIC Interrupt Vectors
 * ============================================================================ */

/* APIC interrupt vectors (above PIC IRQs, which use 32-47) */
#define APIC_TIMER_VECTOR       0x20    /* APIC Timer (shared with PIT when APIC disabled) */
#define APIC_SPURIOUS_VECTOR    0xFF    /* Spurious interrupt vector */
#define APIC_ERROR_VECTOR       0xFE    /* APIC error interrupt */
#define APIC_LINT0_VECTOR       0xFD    /* LINT0 vector */
#define APIC_LINT1_VECTOR       0xFC    /* LINT1 vector (NMI) */

/* IPI vectors for SMP */
#define IPI_VECTOR_RESCHEDULE   0xF0    /* Reschedule IPI */
#define IPI_VECTOR_TLB_FLUSH    0xF1    /* TLB flush IPI */
#define IPI_VECTOR_STOP         0xF2    /* Stop/halt IPI */
#define IPI_VECTOR_CALL         0xF3    /* Function call IPI */

/* ============================================================================
 * PIT Constants (for timer calibration)
 * ============================================================================ */

#define PIT_CHANNEL0_DATA       0x40
#define PIT_CHANNEL2_DATA       0x42
#define PIT_CMD                 0x43
#define PIT_FREQUENCY           1193182     /* PIT oscillator frequency (Hz) */

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/**
 * APIC information structure
 */
typedef struct {
    uint64_t base_address;      /* APIC base physical address */
    uint8_t  id;                /* Local APIC ID */
    uint8_t  version;           /* APIC version */
    uint8_t  max_lvt;           /* Maximum LVT entry */
    bool     enabled;           /* APIC enabled flag */
    bool     is_bsp;            /* Is this the bootstrap processor? */
    bool     x2apic_supported;  /* x2APIC mode supported */
    bool     x2apic_enabled;    /* x2APIC mode enabled */
    uint32_t timer_frequency;   /* Calibrated timer frequency (Hz) */
    uint64_t ticks;             /* Total timer ticks */
} apic_info_t;

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/**
 * Check if APIC is available on this CPU
 * @return true if APIC is present, false otherwise
 */
bool apic_is_available(void);

/**
 * Initialize the Local APIC
 * This detects and enables the APIC, disables the legacy PIC,
 * and sets up basic APIC configuration.
 * @return true on success, false on failure
 */
bool apic_init(void);

/**
 * Enable the Local APIC
 * Sets the software enable bit in the SVR register
 */
void apic_enable(void);

/**
 * Disable the Local APIC
 * Clears the software enable bit in the SVR register
 */
void apic_disable(void);

/**
 * Get the Local APIC ID
 * @return The APIC ID of this processor
 */
uint8_t apic_get_id(void);

/**
 * Get the Local APIC version
 * @return The APIC version number
 */
uint8_t apic_get_version(void);

/**
 * Send End-Of-Interrupt to the APIC
 * Must be called at the end of each interrupt handler for APIC-serviced interrupts
 */
void apic_eoi(void);

/**
 * Read from a Local APIC register
 * @param reg Register offset
 * @return Register value
 */
uint32_t apic_read(uint32_t reg);

/**
 * Write to a Local APIC register
 * @param reg Register offset
 * @param value Value to write
 */
void apic_write(uint32_t reg, uint32_t value);

/**
 * Initialize the APIC timer for periodic interrupts
 * @param frequency Desired timer frequency in Hz
 * @return true on success, false on failure
 */
bool apic_timer_init(uint32_t frequency);

/**
 * Stop the APIC timer
 */
void apic_timer_stop(void);

/**
 * Get the current APIC timer count
 * @return Current timer counter value
 */
uint32_t apic_timer_get_count(void);

/**
 * Get total timer ticks since initialization
 * @return Total tick count
 */
uint64_t apic_timer_get_ticks(void);

/**
 * APIC timer interrupt handler
 * @param frame Interrupt frame
 */
void apic_timer_handler(interrupt_frame_t *frame);

/**
 * APIC spurious interrupt handler
 * @param frame Interrupt frame
 */
void apic_spurious_handler(interrupt_frame_t *frame);

/**
 * APIC error interrupt handler
 * @param frame Interrupt frame
 */
void apic_error_handler(interrupt_frame_t *frame);

/**
 * Send an Inter-Processor Interrupt (IPI)
 * @param dest_apic Destination APIC ID
 * @param vector Interrupt vector number
 * @return true on success, false on failure
 */
bool apic_send_ipi(uint8_t dest_apic, uint8_t vector);

/**
 * Send an IPI to all processors (excluding self)
 * @param vector Interrupt vector number
 * @return true on success, false on failure
 */
bool apic_send_ipi_all(uint8_t vector);

/**
 * Send an IPI to self
 * @param vector Interrupt vector number
 * @return true on success, false on failure
 */
bool apic_send_ipi_self(uint8_t vector);

/**
 * Send INIT IPI to a processor
 * Used during AP startup sequence
 * @param dest_apic Destination APIC ID
 * @return true on success, false on failure
 */
bool apic_send_init_ipi(uint8_t dest_apic);

/**
 * Send Startup IPI (SIPI) to a processor
 * Used during AP startup sequence
 * @param dest_apic Destination APIC ID
 * @param vector Startup vector (physical address >> 12)
 * @return true on success, false on failure
 */
bool apic_send_startup_ipi(uint8_t dest_apic, uint8_t vector);

/**
 * Wait for IPI delivery to complete
 */
void apic_ipi_wait(void);

/**
 * Get APIC information structure
 * @return Pointer to APIC info structure
 */
const apic_info_t* apic_get_info(void);

/**
 * Disable the legacy 8259 PIC
 * This masks all PIC interrupts and sets up for APIC mode
 */
void apic_disable_pic(void);

/* ============================================================================
 * MSR Access Functions (inline)
 * ============================================================================ */

/**
 * Read from a Model Specific Register (MSR)
 * @param msr MSR number
 * @return MSR value
 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * Write to a Model Specific Register (MSR)
 * @param msr MSR number
 * @param value Value to write
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/**
 * Execute CPUID instruction
 * @param leaf CPUID leaf (EAX input)
 * @param eax Output EAX
 * @param ebx Output EBX
 * @param ecx Output ECX
 * @param edx Output EDX
 */
static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx) {
    __asm__ __volatile__("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

/**
 * Execute CPUID instruction with subleaf
 * @param leaf CPUID leaf (EAX input)
 * @param subleaf CPUID subleaf (ECX input)
 * @param eax Output EAX
 * @param ebx Output EBX
 * @param ecx Output ECX
 * @param edx Output EDX
 */
static inline void cpuid_ext(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
    __asm__ __volatile__("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
}

#endif /* _AAAOS_ARCH_APIC_H */
