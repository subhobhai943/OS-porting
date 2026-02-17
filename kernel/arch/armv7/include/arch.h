#ifndef _AAAOS_ARMV7_ARCH_H
#define _AAAOS_ARMV7_ARCH_H

static inline void arch_interrupts_enable(void) {
    __asm__ __volatile__("cpsie i" ::: "memory");
}

static inline void arch_interrupts_disable(void) {
    __asm__ __volatile__("cpsid i" ::: "memory");
}

static inline void arch_cpu_idle(void) {
    __asm__ __volatile__("wfi");
}

#endif
