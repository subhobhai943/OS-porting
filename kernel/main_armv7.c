#include "include/types.h"
#include "arch/armv7/include/arch.h"

void kernel_main_armv7(void) {
    arch_interrupts_disable();

    for (;;) {
        arch_cpu_idle();
    }
}
