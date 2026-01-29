/**
 * AAAos Kernel - ACPI (Advanced Configuration and Power Interface) Support
 *
 * Provides ACPI table parsing for hardware discovery and power management.
 * Supports finding RSDP, parsing RSDT/XSDT, and accessing MADT/FADT tables.
 */

#ifndef _AAAOS_ARCH_ACPI_H
#define _AAAOS_ARCH_ACPI_H

#include "../../include/types.h"

/* ============================================================================
 * ACPI Signatures (4 bytes, stored as char arrays)
 * ============================================================================ */
#define ACPI_RSDP_SIGNATURE     "RSD PTR "  /* 8 bytes for RSDP */
#define ACPI_RSDT_SIGNATURE     "RSDT"
#define ACPI_XSDT_SIGNATURE     "XSDT"
#define ACPI_MADT_SIGNATURE     "APIC"      /* Multiple APIC Description Table */
#define ACPI_FADT_SIGNATURE     "FACP"      /* Fixed ACPI Description Table */

/* ============================================================================
 * RSDP Search Locations
 * ============================================================================ */
#define ACPI_EBDA_PTR_LOCATION  0x040E      /* EBDA pointer location in BDA */
#define ACPI_BIOS_ROM_START     0x000E0000  /* BIOS ROM area start */
#define ACPI_BIOS_ROM_END       0x000FFFFF  /* BIOS ROM area end */
#define ACPI_RSDP_ALIGNMENT     16          /* RSDP is 16-byte aligned */

/* ============================================================================
 * MADT Entry Types
 * ============================================================================ */
#define ACPI_MADT_TYPE_LOCAL_APIC            0
#define ACPI_MADT_TYPE_IO_APIC               1
#define ACPI_MADT_TYPE_INT_SRC_OVERRIDE      2
#define ACPI_MADT_TYPE_NMI_SOURCE            3
#define ACPI_MADT_TYPE_LOCAL_APIC_NMI        4
#define ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE   5
#define ACPI_MADT_TYPE_IO_SAPIC              6
#define ACPI_MADT_TYPE_LOCAL_SAPIC           7
#define ACPI_MADT_TYPE_PLATFORM_INT_SOURCES  8
#define ACPI_MADT_TYPE_LOCAL_X2APIC          9
#define ACPI_MADT_TYPE_LOCAL_X2APIC_NMI      10

/* ============================================================================
 * MADT Flags
 * ============================================================================ */
#define ACPI_MADT_FLAG_PCAT_COMPAT           BIT(0)  /* Dual 8259 PICs installed */

/* Local APIC flags */
#define ACPI_MADT_LAPIC_FLAG_ENABLED         BIT(0)
#define ACPI_MADT_LAPIC_FLAG_ONLINE_CAPABLE  BIT(1)

/* ============================================================================
 * FADT PM1 Control Flags
 * ============================================================================ */
#define ACPI_PM1_CNT_SCI_EN                  BIT(0)
#define ACPI_PM1_CNT_SLP_TYP_SHIFT           10
#define ACPI_PM1_CNT_SLP_TYP_MASK            (0x7 << ACPI_PM1_CNT_SLP_TYP_SHIFT)
#define ACPI_PM1_CNT_SLP_EN                  BIT(13)

/* Sleep type values (S5 = soft power off) */
#define ACPI_SLP_TYP_S5                      5

/* ============================================================================
 * ACPI Data Structures
 * ============================================================================ */

/**
 * RSDP (Root System Description Pointer)
 * ACPI 1.0 structure (20 bytes)
 */
typedef struct PACKED {
    char        signature[8];       /* "RSD PTR " */
    uint8_t     checksum;           /* ACPI 1.0 checksum */
    char        oem_id[6];          /* OEM identification string */
    uint8_t     revision;           /* ACPI revision (0 = 1.0, 2 = 2.0+) */
    uint32_t    rsdt_address;       /* Physical address of RSDT */
} rsdp_v1_t;

/**
 * RSDP Extended (ACPI 2.0+)
 * Full structure (36 bytes)
 */
typedef struct PACKED {
    /* ACPI 1.0 fields */
    char        signature[8];       /* "RSD PTR " */
    uint8_t     checksum;           /* ACPI 1.0 checksum */
    char        oem_id[6];          /* OEM identification string */
    uint8_t     revision;           /* ACPI revision (0 = 1.0, 2 = 2.0+) */
    uint32_t    rsdt_address;       /* Physical address of RSDT */

    /* ACPI 2.0+ fields */
    uint32_t    length;             /* Total structure length */
    uint64_t    xsdt_address;       /* Physical address of XSDT (64-bit) */
    uint8_t     extended_checksum;  /* Extended checksum (entire structure) */
    uint8_t     reserved[3];        /* Reserved */
} rsdp_t;

/**
 * Common ACPI System Description Table Header
 * All ACPI tables start with this header
 */
typedef struct PACKED {
    char        signature[4];       /* Table signature (e.g., "APIC", "FACP") */
    uint32_t    length;             /* Total table length including header */
    uint8_t     revision;           /* Table revision */
    uint8_t     checksum;           /* Checksum (entire table must sum to 0) */
    char        oem_id[6];          /* OEM identification */
    char        oem_table_id[8];    /* OEM table identification */
    uint32_t    oem_revision;       /* OEM revision */
    uint32_t    creator_id;         /* Creator ID */
    uint32_t    creator_revision;   /* Creator revision */
} acpi_sdt_header_t;

/**
 * RSDT (Root System Description Table)
 * Contains 32-bit pointers to other ACPI tables
 */
typedef struct PACKED {
    acpi_sdt_header_t header;
    uint32_t    entries[];          /* Array of 32-bit physical addresses */
} rsdt_t;

/**
 * XSDT (Extended System Description Table)
 * Contains 64-bit pointers to other ACPI tables (ACPI 2.0+)
 */
typedef struct PACKED {
    acpi_sdt_header_t header;
    uint64_t    entries[];          /* Array of 64-bit physical addresses */
} xsdt_t;

/**
 * Generic Address Structure (GAS)
 * Used in FADT for register addresses
 */
typedef struct PACKED {
    uint8_t     address_space_id;   /* Address space (0=memory, 1=I/O) */
    uint8_t     register_bit_width;
    uint8_t     register_bit_offset;
    uint8_t     access_size;        /* Access size (0=undefined, 1=byte, etc) */
    uint64_t    address;            /* 64-bit address */
} acpi_gas_t;

/**
 * MADT Header (Multiple APIC Description Table)
 * Describes interrupt controllers in the system
 */
typedef struct PACKED {
    acpi_sdt_header_t header;
    uint32_t    local_apic_address; /* Physical address of Local APIC */
    uint32_t    flags;              /* MADT flags (PCAT compatibility) */
    /* Variable-length entries follow */
} madt_t;

/**
 * MADT Entry Header
 * Common header for all MADT entries
 */
typedef struct PACKED {
    uint8_t     type;               /* Entry type */
    uint8_t     length;             /* Entry length */
} madt_entry_header_t;

/**
 * MADT Local APIC Entry (Type 0)
 * Describes a processor's Local APIC
 */
typedef struct PACKED {
    madt_entry_header_t header;
    uint8_t     acpi_processor_id;  /* ACPI processor UID */
    uint8_t     apic_id;            /* Local APIC ID */
    uint32_t    flags;              /* Flags (enabled, online capable) */
} madt_local_apic_t;

/**
 * MADT I/O APIC Entry (Type 1)
 * Describes an I/O APIC
 */
typedef struct PACKED {
    madt_entry_header_t header;
    uint8_t     io_apic_id;         /* I/O APIC ID */
    uint8_t     reserved;
    uint32_t    io_apic_address;    /* I/O APIC physical address */
    uint32_t    gsi_base;           /* Global System Interrupt base */
} madt_io_apic_t;

/**
 * MADT Interrupt Source Override Entry (Type 2)
 * Maps legacy ISA IRQs to GSIs
 */
typedef struct PACKED {
    madt_entry_header_t header;
    uint8_t     bus_source;         /* Bus (0 = ISA) */
    uint8_t     irq_source;         /* IRQ source (ISA IRQ number) */
    uint32_t    gsi;                /* Global System Interrupt */
    uint16_t    flags;              /* Polarity/Trigger mode flags */
} madt_int_src_override_t;

/**
 * MADT Local APIC NMI Entry (Type 4)
 * Describes NMI connection to Local APIC LINT pins
 */
typedef struct PACKED {
    madt_entry_header_t header;
    uint8_t     acpi_processor_id;  /* ACPI processor UID (0xFF = all) */
    uint16_t    flags;              /* Polarity/Trigger mode flags */
    uint8_t     lint;               /* LINT pin (0 or 1) */
} madt_local_apic_nmi_t;

/**
 * FADT (Fixed ACPI Description Table)
 * Contains fixed hardware information and power management ports
 */
typedef struct PACKED {
    acpi_sdt_header_t header;

    /* ACPI 1.0 fields */
    uint32_t    firmware_ctrl;      /* Physical address of FACS */
    uint32_t    dsdt;               /* Physical address of DSDT */

    uint8_t     reserved1;          /* INT_MODEL (obsolete) */
    uint8_t     preferred_pm_profile; /* Preferred power management profile */

    uint16_t    sci_interrupt;      /* SCI interrupt vector */
    uint32_t    smi_command_port;   /* SMI command port */
    uint8_t     acpi_enable;        /* Value to write to enable ACPI */
    uint8_t     acpi_disable;       /* Value to write to disable ACPI */
    uint8_t     s4bios_req;         /* Value for S4BIOS support */
    uint8_t     pstate_control;     /* Processor performance state control */

    /* Power management I/O register addresses */
    uint32_t    pm1a_event_block;   /* PM1a event register block */
    uint32_t    pm1b_event_block;   /* PM1b event register block */
    uint32_t    pm1a_control_block; /* PM1a control register block */
    uint32_t    pm1b_control_block; /* PM1b control register block */
    uint32_t    pm2_control_block;  /* PM2 control register block */
    uint32_t    pm_timer_block;     /* PM timer register block */
    uint32_t    gpe0_block;         /* GPE0 register block */
    uint32_t    gpe1_block;         /* GPE1 register block */

    /* Block lengths */
    uint8_t     pm1_event_length;
    uint8_t     pm1_control_length;
    uint8_t     pm2_control_length;
    uint8_t     pm_timer_length;
    uint8_t     gpe0_length;
    uint8_t     gpe1_length;
    uint8_t     gpe1_base;

    uint8_t     cstate_control;     /* C-state support */
    uint16_t    worst_c2_latency;   /* Worst-case C2 latency (us) */
    uint16_t    worst_c3_latency;   /* Worst-case C3 latency (us) */
    uint16_t    flush_size;         /* Cache flush size */
    uint16_t    flush_stride;       /* Cache flush stride */
    uint8_t     duty_offset;        /* Duty cycle offset */
    uint8_t     duty_width;         /* Duty cycle width */

    /* RTC registers */
    uint8_t     day_alarm;          /* Day alarm index in CMOS */
    uint8_t     month_alarm;        /* Month alarm index in CMOS */
    uint8_t     century;            /* Century index in CMOS */

    /* ACPI 2.0+ fields */
    uint16_t    boot_arch_flags;    /* Boot architecture flags */
    uint8_t     reserved2;
    uint32_t    flags;              /* Fixed feature flags */

    /* Reset register (ACPI 2.0+) */
    acpi_gas_t  reset_reg;          /* Reset register address */
    uint8_t     reset_value;        /* Value to write for reset */

    uint16_t    arm_boot_arch;      /* ARM boot architecture flags */
    uint8_t     fadt_minor_version; /* FADT minor version */

    /* 64-bit addresses (ACPI 2.0+) */
    uint64_t    x_firmware_ctrl;    /* 64-bit FACS address */
    uint64_t    x_dsdt;             /* 64-bit DSDT address */

    acpi_gas_t  x_pm1a_event_block;
    acpi_gas_t  x_pm1b_event_block;
    acpi_gas_t  x_pm1a_control_block;
    acpi_gas_t  x_pm1b_control_block;
    acpi_gas_t  x_pm2_control_block;
    acpi_gas_t  x_pm_timer_block;
    acpi_gas_t  x_gpe0_block;
    acpi_gas_t  x_gpe1_block;

    /* Sleep control registers (ACPI 5.0+) */
    acpi_gas_t  sleep_control_reg;
    acpi_gas_t  sleep_status_reg;

    /* Hypervisor vendor identity (ACPI 6.0+) */
    uint64_t    hypervisor_vendor_id;
} fadt_t;

/* ============================================================================
 * ACPI Parsed Information
 * ============================================================================ */

/**
 * Structure to hold parsed MADT information
 */
typedef struct {
    uint32_t    local_apic_address; /* Physical address of Local APIC */
    bool        has_8259_pic;       /* System has dual 8259 PICs */

    /* Local APIC entries */
    struct {
        uint8_t     processor_id;
        uint8_t     apic_id;
        bool        enabled;
    } local_apics[256];
    uint32_t    local_apic_count;

    /* I/O APIC entries */
    struct {
        uint8_t     id;
        uint32_t    address;
        uint32_t    gsi_base;
    } io_apics[16];
    uint32_t    io_apic_count;

    /* Interrupt source overrides */
    struct {
        uint8_t     irq_source;
        uint32_t    gsi;
        uint16_t    flags;
    } overrides[32];
    uint32_t    override_count;
} acpi_madt_info_t;

/* ============================================================================
 * ACPI Functions
 * ============================================================================ */

/**
 * Initialize ACPI subsystem
 * Searches for RSDP, parses RSDT/XSDT, and caches table locations.
 * @return true if ACPI tables found and valid, false otherwise
 */
bool acpi_init(void);

/**
 * Find an ACPI table by its 4-byte signature
 * @param signature 4-byte table signature (e.g., "APIC", "FACP")
 * @return Pointer to table header, or NULL if not found
 */
acpi_sdt_header_t *acpi_find_table(const char signature[4]);

/**
 * Get parsed MADT (Multiple APIC Description Table) information
 * Must be called after acpi_init()
 * @return Pointer to parsed MADT info, or NULL if MADT not found
 */
const acpi_madt_info_t *acpi_get_madt(void);

/**
 * Get FADT (Fixed ACPI Description Table)
 * Must be called after acpi_init()
 * @return Pointer to FADT, or NULL if not found
 */
const fadt_t *acpi_get_fadt(void);

/**
 * Check if ACPI is available and initialized
 * @return true if ACPI is available
 */
bool acpi_available(void);

/**
 * Get ACPI revision
 * @return ACPI revision (0 = 1.0, 2 = 2.0+)
 */
uint8_t acpi_get_revision(void);

/**
 * Power off the system using ACPI
 * This function does not return on success.
 */
void acpi_shutdown(void) NORETURN;

/**
 * Reboot the system using ACPI
 * Falls back to keyboard controller reset if ACPI reset unavailable.
 * This function does not return on success.
 */
void acpi_reboot(void) NORETURN;

/**
 * Enable ACPI mode (transition from legacy mode)
 * @return true if ACPI mode enabled successfully
 */
bool acpi_enable(void);

/**
 * Disable ACPI mode (transition to legacy mode)
 * @return true if legacy mode enabled successfully
 */
bool acpi_disable(void);

#endif /* _AAAOS_ARCH_ACPI_H */
