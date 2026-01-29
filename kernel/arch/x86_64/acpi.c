/**
 * AAAos Kernel - ACPI (Advanced Configuration and Power Interface) Implementation
 *
 * Implements ACPI table discovery and parsing for hardware configuration
 * and power management support.
 */

#include "acpi.h"
#include "io.h"
#include "../../include/serial.h"

/* ============================================================================
 * Private State
 * ============================================================================ */

/* ACPI initialization state */
static bool acpi_initialized = false;
static uint8_t acpi_revision = 0;

/* Cached table pointers */
static rsdp_t *cached_rsdp = NULL;
static rsdt_t *cached_rsdt = NULL;
static xsdt_t *cached_xsdt = NULL;
static madt_t *cached_madt = NULL;
static fadt_t *cached_fadt = NULL;

/* Parsed MADT information */
static acpi_madt_info_t madt_info;
static bool madt_parsed = false;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Compare two memory regions
 */
static bool memcmp_eq(const void *a, const void *b, size_t len) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < len; i++) {
        if (pa[i] != pb[i]) {
            return false;
        }
    }
    return true;
}

/**
 * Zero out a memory region
 */
static void memzero(void *dest, size_t len) {
    uint8_t *p = (uint8_t *)dest;
    for (size_t i = 0; i < len; i++) {
        p[i] = 0;
    }
}

/**
 * Calculate checksum of a memory region
 * Valid ACPI structures sum to 0
 */
static uint8_t acpi_checksum(const void *data, size_t length) {
    uint8_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum;
}

/**
 * Validate ACPI table checksum
 */
static bool acpi_validate_table(const acpi_sdt_header_t *header) {
    if (header == NULL) {
        return false;
    }
    return acpi_checksum(header, header->length) == 0;
}

/**
 * Convert physical address to virtual address
 * For now, we assume identity mapping for low memory
 * TODO: Use proper VMM mapping for addresses above 4GB
 */
static void *phys_to_virt(uint64_t phys) {
    /* Identity mapping for low memory */
    return (void *)(uintptr_t)phys;
}

/* ============================================================================
 * RSDP Search Functions
 * ============================================================================ */

/**
 * Validate RSDP structure
 */
static bool rsdp_validate(const rsdp_t *rsdp) {
    if (rsdp == NULL) {
        return false;
    }

    /* Check signature */
    if (!memcmp_eq(rsdp->signature, ACPI_RSDP_SIGNATURE, 8)) {
        return false;
    }

    /* Validate ACPI 1.0 checksum (first 20 bytes) */
    if (acpi_checksum(rsdp, sizeof(rsdp_v1_t)) != 0) {
        kprintf("[ACPI] RSDP v1 checksum failed\n");
        return false;
    }

    /* For ACPI 2.0+, validate extended checksum */
    if (rsdp->revision >= 2) {
        if (acpi_checksum(rsdp, rsdp->length) != 0) {
            kprintf("[ACPI] RSDP extended checksum failed\n");
            return false;
        }
    }

    return true;
}

/**
 * Search for RSDP in a memory range
 */
static rsdp_t *rsdp_search_range(uintptr_t start, uintptr_t end) {
    /* RSDP must be 16-byte aligned */
    start = ALIGN_UP(start, ACPI_RSDP_ALIGNMENT);

    for (uintptr_t addr = start; addr < end; addr += ACPI_RSDP_ALIGNMENT) {
        rsdp_t *rsdp = (rsdp_t *)addr;

        if (rsdp_validate(rsdp)) {
            return rsdp;
        }
    }

    return NULL;
}

/**
 * Find RSDP by searching standard locations
 * 1. First KB of EBDA (Extended BIOS Data Area)
 * 2. BIOS ROM area (0xE0000 - 0xFFFFF)
 */
static rsdp_t *rsdp_find(void) {
    rsdp_t *rsdp = NULL;

    kprintf("[ACPI] Searching for RSDP...\n");

    /* Search EBDA (Extended BIOS Data Area) */
    /* EBDA pointer is at 0x040E (16-bit segment address) */
    uint16_t ebda_segment = *(volatile uint16_t *)ACPI_EBDA_PTR_LOCATION;
    uintptr_t ebda_addr = (uintptr_t)ebda_segment << 4;

    if (ebda_addr != 0 && ebda_addr < 0xA0000) {
        kprintf("[ACPI] Searching EBDA at 0x%x\n", (uint32_t)ebda_addr);
        rsdp = rsdp_search_range(ebda_addr, ebda_addr + 1024);
        if (rsdp != NULL) {
            kprintf("[ACPI] Found RSDP in EBDA at %p\n", rsdp);
            return rsdp;
        }
    }

    /* Search BIOS ROM area */
    kprintf("[ACPI] Searching BIOS ROM area (0xE0000-0xFFFFF)\n");
    rsdp = rsdp_search_range(ACPI_BIOS_ROM_START, ACPI_BIOS_ROM_END);
    if (rsdp != NULL) {
        kprintf("[ACPI] Found RSDP in BIOS ROM at %p\n", rsdp);
        return rsdp;
    }

    kprintf("[ACPI] RSDP not found\n");
    return NULL;
}

/* ============================================================================
 * Table Parsing Functions
 * ============================================================================ */

/**
 * Parse MADT (Multiple APIC Description Table)
 */
static bool parse_madt(const madt_t *madt) {
    if (madt == NULL) {
        return false;
    }

    kprintf("[ACPI] Parsing MADT (length=%u)\n", madt->header.length);

    /* Clear parsed info */
    memzero(&madt_info, sizeof(madt_info));

    /* Store Local APIC address */
    madt_info.local_apic_address = madt->local_apic_address;
    kprintf("[ACPI] Local APIC address: 0x%08x\n", madt->local_apic_address);

    /* Check for 8259 PIC compatibility */
    madt_info.has_8259_pic = (madt->flags & ACPI_MADT_FLAG_PCAT_COMPAT) != 0;
    kprintf("[ACPI] PCAT compatible (8259 PICs): %s\n",
            madt_info.has_8259_pic ? "yes" : "no");

    /* Parse MADT entries */
    const uint8_t *ptr = (const uint8_t *)madt + sizeof(madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (ptr < end) {
        const madt_entry_header_t *entry = (const madt_entry_header_t *)ptr;

        if (entry->length < 2 || ptr + entry->length > end) {
            kprintf("[ACPI] Invalid MADT entry at offset %u\n",
                    (uint32_t)(ptr - (const uint8_t *)madt));
            break;
        }

        switch (entry->type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                const madt_local_apic_t *lapic = (const madt_local_apic_t *)entry;
                if (madt_info.local_apic_count < ARRAY_SIZE(madt_info.local_apics)) {
                    uint32_t idx = madt_info.local_apic_count++;
                    madt_info.local_apics[idx].processor_id = lapic->acpi_processor_id;
                    madt_info.local_apics[idx].apic_id = lapic->apic_id;
                    madt_info.local_apics[idx].enabled =
                        (lapic->flags & ACPI_MADT_LAPIC_FLAG_ENABLED) != 0;

                    kprintf("[ACPI] Local APIC: processor=%u, apic_id=%u, %s\n",
                            lapic->acpi_processor_id, lapic->apic_id,
                            madt_info.local_apics[idx].enabled ? "enabled" : "disabled");
                }
                break;
            }

            case ACPI_MADT_TYPE_IO_APIC: {
                const madt_io_apic_t *ioapic = (const madt_io_apic_t *)entry;
                if (madt_info.io_apic_count < ARRAY_SIZE(madt_info.io_apics)) {
                    uint32_t idx = madt_info.io_apic_count++;
                    madt_info.io_apics[idx].id = ioapic->io_apic_id;
                    madt_info.io_apics[idx].address = ioapic->io_apic_address;
                    madt_info.io_apics[idx].gsi_base = ioapic->gsi_base;

                    kprintf("[ACPI] I/O APIC: id=%u, addr=0x%08x, gsi_base=%u\n",
                            ioapic->io_apic_id, ioapic->io_apic_address,
                            ioapic->gsi_base);
                }
                break;
            }

            case ACPI_MADT_TYPE_INT_SRC_OVERRIDE: {
                const madt_int_src_override_t *ovr =
                    (const madt_int_src_override_t *)entry;
                if (madt_info.override_count < ARRAY_SIZE(madt_info.overrides)) {
                    uint32_t idx = madt_info.override_count++;
                    madt_info.overrides[idx].irq_source = ovr->irq_source;
                    madt_info.overrides[idx].gsi = ovr->gsi;
                    madt_info.overrides[idx].flags = ovr->flags;

                    kprintf("[ACPI] IRQ Override: IRQ%u -> GSI%u (flags=0x%04x)\n",
                            ovr->irq_source, ovr->gsi, ovr->flags);
                }
                break;
            }

            case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                const madt_local_apic_nmi_t *nmi =
                    (const madt_local_apic_nmi_t *)entry;
                kprintf("[ACPI] Local APIC NMI: processor=%u, LINT%u (flags=0x%04x)\n",
                        nmi->acpi_processor_id, nmi->lint, nmi->flags);
                break;
            }

            case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE: {
                /* 64-bit Local APIC address override */
                uint64_t addr = *(const uint64_t *)(ptr + 4);
                madt_info.local_apic_address = (uint32_t)addr; /* Truncate for now */
                kprintf("[ACPI] Local APIC Override: 0x%016llx\n", addr);
                break;
            }

            default:
                kprintf("[ACPI] Unknown MADT entry type %u (length=%u)\n",
                        entry->type, entry->length);
                break;
        }

        ptr += entry->length;
    }

    kprintf("[ACPI] MADT: %u CPUs, %u I/O APICs, %u overrides\n",
            madt_info.local_apic_count, madt_info.io_apic_count,
            madt_info.override_count);

    madt_parsed = true;
    return true;
}

/**
 * Find a table in RSDT by signature
 */
static acpi_sdt_header_t *find_table_rsdt(const rsdt_t *rsdt,
                                          const char signature[4]) {
    if (rsdt == NULL) {
        return NULL;
    }

    /* Calculate number of entries */
    size_t entry_count = (rsdt->header.length - sizeof(acpi_sdt_header_t)) /
                         sizeof(uint32_t);

    for (size_t i = 0; i < entry_count; i++) {
        acpi_sdt_header_t *header =
            (acpi_sdt_header_t *)phys_to_virt(rsdt->entries[i]);

        if (header != NULL && memcmp_eq(header->signature, signature, 4)) {
            if (acpi_validate_table(header)) {
                return header;
            } else {
                kprintf("[ACPI] Table '%.4s' checksum invalid\n", signature);
            }
        }
    }

    return NULL;
}

/**
 * Find a table in XSDT by signature
 */
static acpi_sdt_header_t *find_table_xsdt(const xsdt_t *xsdt,
                                          const char signature[4]) {
    if (xsdt == NULL) {
        return NULL;
    }

    /* Calculate number of entries */
    size_t entry_count = (xsdt->header.length - sizeof(acpi_sdt_header_t)) /
                         sizeof(uint64_t);

    for (size_t i = 0; i < entry_count; i++) {
        acpi_sdt_header_t *header =
            (acpi_sdt_header_t *)phys_to_virt(xsdt->entries[i]);

        if (header != NULL && memcmp_eq(header->signature, signature, 4)) {
            if (acpi_validate_table(header)) {
                return header;
            } else {
                kprintf("[ACPI] Table '%.4s' checksum invalid\n", signature);
            }
        }
    }

    return NULL;
}

/* ============================================================================
 * Power Management Functions
 * ============================================================================ */

/**
 * Write to PM1 control register
 */
static void pm1_control_write(uint16_t value) {
    if (cached_fadt == NULL) {
        return;
    }

    /* Write to PM1a control block */
    if (cached_fadt->pm1a_control_block != 0) {
        outw(cached_fadt->pm1a_control_block, value);
    }

    /* Write to PM1b control block if present */
    if (cached_fadt->pm1b_control_block != 0) {
        outw(cached_fadt->pm1b_control_block, value);
    }
}

/**
 * Read from PM1 control register
 */
static uint16_t pm1_control_read(void) {
    if (cached_fadt == NULL || cached_fadt->pm1a_control_block == 0) {
        return 0;
    }
    return inw(cached_fadt->pm1a_control_block);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

bool acpi_init(void) {
    kprintf("[ACPI] Initializing ACPI subsystem\n");

    if (acpi_initialized) {
        kprintf("[ACPI] Already initialized\n");
        return true;
    }

    /* Find RSDP */
    cached_rsdp = rsdp_find();
    if (cached_rsdp == NULL) {
        kprintf("[ACPI] Failed to find RSDP\n");
        return false;
    }

    acpi_revision = cached_rsdp->revision;
    kprintf("[ACPI] ACPI revision: %u.0\n",
            acpi_revision >= 2 ? acpi_revision : 1);
    kprintf("[ACPI] OEM ID: %.6s\n", cached_rsdp->oem_id);

    /* Use XSDT if available (ACPI 2.0+), otherwise use RSDT */
    if (acpi_revision >= 2 && cached_rsdp->xsdt_address != 0) {
        cached_xsdt = (xsdt_t *)phys_to_virt(cached_rsdp->xsdt_address);
        kprintf("[ACPI] Using XSDT at 0x%016llx\n", cached_rsdp->xsdt_address);

        if (!acpi_validate_table(&cached_xsdt->header)) {
            kprintf("[ACPI] XSDT checksum invalid, falling back to RSDT\n");
            cached_xsdt = NULL;
        }
    }

    if (cached_xsdt == NULL && cached_rsdp->rsdt_address != 0) {
        cached_rsdt = (rsdt_t *)phys_to_virt(cached_rsdp->rsdt_address);
        kprintf("[ACPI] Using RSDT at 0x%08x\n", cached_rsdp->rsdt_address);

        if (!acpi_validate_table(&cached_rsdt->header)) {
            kprintf("[ACPI] RSDT checksum invalid\n");
            cached_rsdt = NULL;
        }
    }

    if (cached_xsdt == NULL && cached_rsdt == NULL) {
        kprintf("[ACPI] No valid RSDT or XSDT found\n");
        return false;
    }

    /* Find and cache important tables */

    /* Find FADT (Fixed ACPI Description Table) */
    cached_fadt = (fadt_t *)acpi_find_table(ACPI_FADT_SIGNATURE);
    if (cached_fadt != NULL) {
        kprintf("[ACPI] Found FADT (revision %u)\n", cached_fadt->header.revision);
        kprintf("[ACPI] SCI Interrupt: %u\n", cached_fadt->sci_interrupt);
        kprintf("[ACPI] SMI Command Port: 0x%04x\n", cached_fadt->smi_command_port);
        kprintf("[ACPI] PM1a Event Block: 0x%04x\n", cached_fadt->pm1a_event_block);
        kprintf("[ACPI] PM1a Control Block: 0x%04x\n", cached_fadt->pm1a_control_block);
    } else {
        kprintf("[ACPI] FADT not found\n");
    }

    /* Find MADT (Multiple APIC Description Table) */
    cached_madt = (madt_t *)acpi_find_table(ACPI_MADT_SIGNATURE);
    if (cached_madt != NULL) {
        parse_madt(cached_madt);
    } else {
        kprintf("[ACPI] MADT not found (using legacy PIC configuration)\n");
    }

    acpi_initialized = true;
    kprintf("[ACPI] Initialization complete\n");
    return true;
}

acpi_sdt_header_t *acpi_find_table(const char signature[4]) {
    if (!acpi_initialized && cached_xsdt == NULL && cached_rsdt == NULL) {
        /* Try to find directly if init hasn't completed */
        if (cached_rsdp == NULL) {
            return NULL;
        }
    }

    acpi_sdt_header_t *table = NULL;

    /* Prefer XSDT for 64-bit addresses */
    if (cached_xsdt != NULL) {
        table = find_table_xsdt(cached_xsdt, signature);
    }

    /* Fall back to RSDT */
    if (table == NULL && cached_rsdt != NULL) {
        table = find_table_rsdt(cached_rsdt, signature);
    }

    return table;
}

const acpi_madt_info_t *acpi_get_madt(void) {
    if (!acpi_initialized) {
        return NULL;
    }

    if (!madt_parsed && cached_madt != NULL) {
        parse_madt(cached_madt);
    }

    return madt_parsed ? &madt_info : NULL;
}

const fadt_t *acpi_get_fadt(void) {
    return cached_fadt;
}

bool acpi_available(void) {
    return acpi_initialized;
}

uint8_t acpi_get_revision(void) {
    return acpi_revision;
}

bool acpi_enable(void) {
    if (cached_fadt == NULL) {
        kprintf("[ACPI] Cannot enable ACPI: FADT not found\n");
        return false;
    }

    /* Check if ACPI is already enabled */
    uint16_t pm1_cnt = pm1_control_read();
    if (pm1_cnt & ACPI_PM1_CNT_SCI_EN) {
        kprintf("[ACPI] ACPI already enabled\n");
        return true;
    }

    /* Check if SMI command port is available */
    if (cached_fadt->smi_command_port == 0) {
        kprintf("[ACPI] No SMI command port (hardware-reduced ACPI?)\n");
        return true;  /* Might be hardware-reduced ACPI */
    }

    /* Check if ACPI enable command is available */
    if (cached_fadt->acpi_enable == 0) {
        kprintf("[ACPI] No ACPI enable command defined\n");
        return false;
    }

    kprintf("[ACPI] Enabling ACPI mode...\n");

    /* Write ACPI enable command to SMI command port */
    outb(cached_fadt->smi_command_port, cached_fadt->acpi_enable);

    /* Wait for ACPI to be enabled (SCI_EN bit set in PM1 control) */
    for (int i = 0; i < 1000; i++) {
        pm1_cnt = pm1_control_read();
        if (pm1_cnt & ACPI_PM1_CNT_SCI_EN) {
            kprintf("[ACPI] ACPI mode enabled successfully\n");
            return true;
        }
        /* Small delay */
        io_wait();
    }

    kprintf("[ACPI] Failed to enable ACPI mode (timeout)\n");
    return false;
}

bool acpi_disable(void) {
    if (cached_fadt == NULL) {
        kprintf("[ACPI] Cannot disable ACPI: FADT not found\n");
        return false;
    }

    /* Check if SMI command port is available */
    if (cached_fadt->smi_command_port == 0) {
        kprintf("[ACPI] No SMI command port\n");
        return false;
    }

    /* Check if ACPI disable command is available */
    if (cached_fadt->acpi_disable == 0) {
        kprintf("[ACPI] No ACPI disable command defined\n");
        return false;
    }

    kprintf("[ACPI] Disabling ACPI mode...\n");

    /* Write ACPI disable command to SMI command port */
    outb(cached_fadt->smi_command_port, cached_fadt->acpi_disable);

    /* Wait for ACPI to be disabled (SCI_EN bit cleared in PM1 control) */
    for (int i = 0; i < 1000; i++) {
        uint16_t pm1_cnt = pm1_control_read();
        if (!(pm1_cnt & ACPI_PM1_CNT_SCI_EN)) {
            kprintf("[ACPI] Legacy mode enabled successfully\n");
            return true;
        }
        io_wait();
    }

    kprintf("[ACPI] Failed to disable ACPI mode (timeout)\n");
    return false;
}

void acpi_shutdown(void) {
    kprintf("[ACPI] Initiating system shutdown...\n");

    if (cached_fadt == NULL) {
        kprintf("[ACPI] Cannot shutdown: FADT not found\n");
        goto halt;
    }

    /* Ensure ACPI is enabled */
    if (!acpi_enable()) {
        kprintf("[ACPI] Failed to enable ACPI for shutdown\n");
        goto halt;
    }

    /*
     * To enter S5 (soft power off), we need to:
     * 1. Write SLP_TYP value for S5 to SLP_TYP field
     * 2. Set SLP_EN bit
     *
     * Note: The actual SLP_TYP value should come from \_S5 object in DSDT/SSDT,
     * but for simplicity we use a common value. Many systems use 5 for S5.
     */
    uint16_t slp_typ = ACPI_SLP_TYP_S5 << ACPI_PM1_CNT_SLP_TYP_SHIFT;
    uint16_t pm1_value = slp_typ | ACPI_PM1_CNT_SLP_EN;

    kprintf("[ACPI] Writing 0x%04x to PM1 control\n", pm1_value);

    /* Write to PM1a control */
    if (cached_fadt->pm1a_control_block != 0) {
        outw(cached_fadt->pm1a_control_block, pm1_value);
    }

    /* Write to PM1b control if present */
    if (cached_fadt->pm1b_control_block != 0) {
        outw(cached_fadt->pm1b_control_block, pm1_value);
    }

    /* If we're still running, shutdown didn't work */
    kprintf("[ACPI] Shutdown failed, halting CPU\n");

halt:
    /* Disable interrupts and halt forever */
    __asm__ __volatile__(
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
    );

    /* Should never reach here */
    __builtin_unreachable();
}

void acpi_reboot(void) {
    kprintf("[ACPI] Initiating system reboot...\n");

    /* Try ACPI reset register first (ACPI 2.0+) */
    if (cached_fadt != NULL && cached_fadt->header.revision >= 2) {
        if (cached_fadt->reset_reg.address != 0) {
            kprintf("[ACPI] Using ACPI reset register at 0x%llx\n",
                    cached_fadt->reset_reg.address);

            switch (cached_fadt->reset_reg.address_space_id) {
                case 0:  /* Memory space */
                    *(volatile uint8_t *)(uintptr_t)cached_fadt->reset_reg.address =
                        cached_fadt->reset_value;
                    break;

                case 1:  /* I/O space */
                    outb((uint16_t)cached_fadt->reset_reg.address,
                         cached_fadt->reset_value);
                    break;

                default:
                    kprintf("[ACPI] Unsupported reset register space: %u\n",
                            cached_fadt->reset_reg.address_space_id);
                    break;
            }

            /* Small delay to let reset take effect */
            for (int i = 0; i < 1000; i++) {
                io_wait();
            }
        }
    }

    /* Fall back to keyboard controller reset */
    kprintf("[ACPI] Trying keyboard controller reset...\n");

    /* Wait for keyboard controller to be ready */
    for (int i = 0; i < 1000; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
        io_wait();
    }

    /* Send reset command to keyboard controller */
    outb(0x64, 0xFE);

    /* If keyboard controller reset didn't work, try triple fault */
    kprintf("[ACPI] Keyboard reset failed, trying triple fault...\n");

    /* Small delay */
    for (int i = 0; i < 1000; i++) {
        io_wait();
    }

    /* Load NULL IDT and trigger interrupt for triple fault */
    struct {
        uint16_t limit;
        uint64_t base;
    } PACKED null_idt = { 0, 0 };

    __asm__ __volatile__(
        "lidt %0\n"
        "int $3\n"
        : : "m"(null_idt)
    );

    /* Should never reach here */
    __asm__ __volatile__(
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
    );

    __builtin_unreachable();
}
