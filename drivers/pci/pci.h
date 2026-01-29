/**
 * AAAos Kernel - PCI Bus Driver
 *
 * Provides PCI bus enumeration and configuration space access.
 * Implements standard PCI configuration mechanism using I/O ports
 * 0xCF8 (CONFIG_ADDRESS) and 0xCFC (CONFIG_DATA).
 */

#ifndef _AAAOS_PCI_H
#define _AAAOS_PCI_H

#include "../../kernel/include/types.h"

/* PCI Configuration Space I/O Ports */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI Configuration Space Registers (offsets) */
#define PCI_VENDOR_ID           0x00    /* 16-bit */
#define PCI_DEVICE_ID           0x02    /* 16-bit */
#define PCI_COMMAND             0x04    /* 16-bit */
#define PCI_STATUS              0x06    /* 16-bit */
#define PCI_REVISION_ID         0x08    /* 8-bit */
#define PCI_PROG_IF             0x09    /* 8-bit */
#define PCI_SUBCLASS            0x0A    /* 8-bit */
#define PCI_CLASS               0x0B    /* 8-bit */
#define PCI_CACHE_LINE_SIZE     0x0C    /* 8-bit */
#define PCI_LATENCY_TIMER       0x0D    /* 8-bit */
#define PCI_HEADER_TYPE         0x0E    /* 8-bit */
#define PCI_BIST                0x0F    /* 8-bit */
#define PCI_BAR0                0x10    /* 32-bit */
#define PCI_BAR1                0x14    /* 32-bit */
#define PCI_BAR2                0x18    /* 32-bit */
#define PCI_BAR3                0x1C    /* 32-bit */
#define PCI_BAR4                0x20    /* 32-bit */
#define PCI_BAR5                0x24    /* 32-bit */
#define PCI_CARDBUS_CIS         0x28    /* 32-bit */
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C    /* 16-bit */
#define PCI_SUBSYSTEM_ID        0x2E    /* 16-bit */
#define PCI_EXPANSION_ROM       0x30    /* 32-bit */
#define PCI_CAPABILITIES        0x34    /* 8-bit */
#define PCI_INTERRUPT_LINE      0x3C    /* 8-bit */
#define PCI_INTERRUPT_PIN       0x3D    /* 8-bit */
#define PCI_MIN_GRANT           0x3E    /* 8-bit */
#define PCI_MAX_LATENCY         0x3F    /* 8-bit */

/* PCI Command Register bits */
#define PCI_CMD_IO_SPACE        (1 << 0)    /* I/O space access enable */
#define PCI_CMD_MEMORY_SPACE    (1 << 1)    /* Memory space access enable */
#define PCI_CMD_BUS_MASTER      (1 << 2)    /* Bus mastering enable */
#define PCI_CMD_SPECIAL_CYCLES  (1 << 3)    /* Special cycles enable */
#define PCI_CMD_MWI_ENABLE      (1 << 4)    /* Memory write & invalidate */
#define PCI_CMD_VGA_PALETTE     (1 << 5)    /* VGA palette snoop */
#define PCI_CMD_PARITY_ERROR    (1 << 6)    /* Parity error response */
#define PCI_CMD_SERR_ENABLE     (1 << 8)    /* SERR# enable */
#define PCI_CMD_FAST_BTB        (1 << 9)    /* Fast back-to-back enable */
#define PCI_CMD_INT_DISABLE     (1 << 10)   /* Interrupt disable */

/* PCI Header Type bits */
#define PCI_HEADER_TYPE_MASK    0x7F
#define PCI_HEADER_TYPE_NORMAL  0x00
#define PCI_HEADER_TYPE_BRIDGE  0x01
#define PCI_HEADER_TYPE_CARDBUS 0x02
#define PCI_HEADER_MULTIFUNCTION 0x80

/* BAR type bits */
#define PCI_BAR_IO_SPACE        (1 << 0)    /* BAR is I/O space */
#define PCI_BAR_MEM_TYPE_MASK   (3 << 1)    /* Memory type mask */
#define PCI_BAR_MEM_32BIT       (0 << 1)    /* 32-bit memory */
#define PCI_BAR_MEM_1MB         (1 << 1)    /* Below 1MB (legacy) */
#define PCI_BAR_MEM_64BIT       (2 << 1)    /* 64-bit memory */
#define PCI_BAR_PREFETCHABLE    (1 << 3)    /* Prefetchable memory */

/* Invalid vendor ID (device not present) */
#define PCI_VENDOR_INVALID      0xFFFF

/* PCI Class Codes */
#define PCI_CLASS_UNCLASSIFIED      0x00
#define PCI_CLASS_STORAGE           0x01
#define PCI_CLASS_NETWORK           0x02
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_CLASS_MEMORY            0x05
#define PCI_CLASS_BRIDGE            0x06
#define PCI_CLASS_COMMUNICATION     0x07
#define PCI_CLASS_SYSTEM            0x08
#define PCI_CLASS_INPUT             0x09
#define PCI_CLASS_DOCKING           0x0A
#define PCI_CLASS_PROCESSOR         0x0B
#define PCI_CLASS_SERIAL            0x0C
#define PCI_CLASS_WIRELESS          0x0D
#define PCI_CLASS_INTELLIGENT       0x0E
#define PCI_CLASS_SATELLITE         0x0F
#define PCI_CLASS_ENCRYPTION        0x10
#define PCI_CLASS_SIGNAL            0x11
#define PCI_CLASS_ACCELERATOR       0x12
#define PCI_CLASS_NON_ESSENTIAL     0x13
#define PCI_CLASS_COPROCESSOR       0x40
#define PCI_CLASS_UNDEFINED         0xFF

/* PCI Storage Subclasses */
#define PCI_SUBCLASS_SCSI           0x00
#define PCI_SUBCLASS_IDE            0x01
#define PCI_SUBCLASS_FLOPPY         0x02
#define PCI_SUBCLASS_IPI            0x03
#define PCI_SUBCLASS_RAID           0x04
#define PCI_SUBCLASS_ATA            0x05
#define PCI_SUBCLASS_SATA           0x06
#define PCI_SUBCLASS_SAS            0x07
#define PCI_SUBCLASS_NVM            0x08

/* PCI Network Subclasses */
#define PCI_SUBCLASS_ETHERNET       0x00
#define PCI_SUBCLASS_TOKEN_RING     0x01
#define PCI_SUBCLASS_FDDI           0x02
#define PCI_SUBCLASS_ATM            0x03
#define PCI_SUBCLASS_ISDN           0x04
#define PCI_SUBCLASS_WORLDFIP       0x05
#define PCI_SUBCLASS_PICMG          0x06
#define PCI_SUBCLASS_INFINIBAND     0x07
#define PCI_SUBCLASS_FABRIC         0x08

/* PCI Display Subclasses */
#define PCI_SUBCLASS_VGA            0x00
#define PCI_SUBCLASS_XGA            0x01
#define PCI_SUBCLASS_3D             0x02

/* PCI Bridge Subclasses */
#define PCI_SUBCLASS_HOST_BRIDGE    0x00
#define PCI_SUBCLASS_ISA_BRIDGE     0x01
#define PCI_SUBCLASS_EISA_BRIDGE    0x02
#define PCI_SUBCLASS_MCA_BRIDGE     0x03
#define PCI_SUBCLASS_PCI_BRIDGE     0x04
#define PCI_SUBCLASS_PCMCIA_BRIDGE  0x05
#define PCI_SUBCLASS_NUBUS_BRIDGE   0x06
#define PCI_SUBCLASS_CARDBUS_BRIDGE 0x07
#define PCI_SUBCLASS_RACEWAY_BRIDGE 0x08
#define PCI_SUBCLASS_PCI_PCI_BRIDGE 0x09
#define PCI_SUBCLASS_INFINI_BRIDGE  0x0A

/* PCI Serial Bus Subclasses */
#define PCI_SUBCLASS_FIREWIRE       0x00
#define PCI_SUBCLASS_ACCESS_BUS     0x01
#define PCI_SUBCLASS_SSA            0x02
#define PCI_SUBCLASS_USB            0x03
#define PCI_SUBCLASS_FIBRE          0x04
#define PCI_SUBCLASS_SMBUS          0x05
#define PCI_SUBCLASS_INFINIBAND_SB  0x06
#define PCI_SUBCLASS_IPMI           0x07
#define PCI_SUBCLASS_SERCOS         0x08
#define PCI_SUBCLASS_CANBUS         0x09

/* Maximum devices we track */
#define PCI_MAX_DEVICES     256

/**
 * PCI device structure
 * Represents a single PCI device/function on the bus
 */
typedef struct pci_device {
    uint8_t     bus;            /* Bus number (0-255) */
    uint8_t     device;         /* Device number (0-31) */
    uint8_t     function;       /* Function number (0-7) */

    uint16_t    vendor_id;      /* Vendor identifier */
    uint16_t    device_id;      /* Device identifier */

    uint8_t     class_code;     /* Class code */
    uint8_t     subclass;       /* Subclass code */
    uint8_t     prog_if;        /* Programming interface */
    uint8_t     revision;       /* Revision ID */

    uint8_t     header_type;    /* Header type */
    uint8_t     interrupt_line; /* IRQ line */
    uint8_t     interrupt_pin;  /* Interrupt pin (A-D) */

    uint32_t    bar[6];         /* Base Address Registers */

    bool        present;        /* Device is present and valid */
} pci_device_t;

/* ============================================================================
 * PCI Configuration Space Access Functions
 * ============================================================================ */

/**
 * Read 8-bit value from PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255)
 * @return          8-bit value read
 */
uint8_t pci_read_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/**
 * Read 16-bit value from PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255, must be 2-byte aligned)
 * @return          16-bit value read
 */
uint16_t pci_read_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/**
 * Read 32-bit value from PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255, must be 4-byte aligned)
 * @return          32-bit value read
 */
uint32_t pci_read_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

/**
 * Write 8-bit value to PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255)
 * @param value     Value to write
 */
void pci_write_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);

/**
 * Write 16-bit value to PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255, must be 2-byte aligned)
 * @param value     Value to write
 */
void pci_write_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);

/**
 * Write 32-bit value to PCI configuration space
 * @param bus       Bus number (0-255)
 * @param device    Device number (0-31)
 * @param function  Function number (0-7)
 * @param offset    Register offset (0-255, must be 4-byte aligned)
 * @param value     Value to write
 */
void pci_write_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

/* ============================================================================
 * PCI Initialization and Enumeration
 * ============================================================================ */

/**
 * Initialize the PCI subsystem
 * Scans all PCI buses and enumerates devices
 */
void pci_init(void);

/**
 * Get total number of detected PCI devices
 * @return Number of detected devices
 */
uint32_t pci_get_device_count(void);

/**
 * Get PCI device by index
 * @param index Device index (0 to pci_get_device_count()-1)
 * @return Pointer to device structure, or NULL if invalid index
 */
pci_device_t* pci_get_device(uint32_t index);

/* ============================================================================
 * PCI Device Discovery
 * ============================================================================ */

/**
 * Find a PCI device by vendor and device ID
 * @param vendor_id Vendor ID to search for
 * @param device_id Device ID to search for
 * @return Pointer to device structure, or NULL if not found
 */
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);

/**
 * Find a PCI device by class and subclass
 * @param class_code    Class code to search for
 * @param subclass      Subclass code to search for
 * @return Pointer to device structure, or NULL if not found
 */
pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass);

/**
 * Find the next PCI device by vendor and device ID (for multiple matches)
 * @param vendor_id Vendor ID to search for
 * @param device_id Device ID to search for
 * @param start     Start searching after this device (NULL for first)
 * @return Pointer to device structure, or NULL if not found
 */
pci_device_t* pci_find_device_next(uint16_t vendor_id, uint16_t device_id, pci_device_t* start);

/**
 * Find the next PCI device by class and subclass (for multiple matches)
 * @param class_code    Class code to search for
 * @param subclass      Subclass code to search for
 * @param start         Start searching after this device (NULL for first)
 * @return Pointer to device structure, or NULL if not found
 */
pci_device_t* pci_find_class_next(uint8_t class_code, uint8_t subclass, pci_device_t* start);

/* ============================================================================
 * PCI Device Configuration
 * ============================================================================ */

/**
 * Get Base Address Register value
 * @param dev   Pointer to PCI device
 * @param bar   BAR index (0-5)
 * @return      BAR value (address), or 0 if invalid
 */
uint64_t pci_get_bar(pci_device_t* dev, int bar);

/**
 * Get BAR size by probing
 * @param dev   Pointer to PCI device
 * @param bar   BAR index (0-5)
 * @return      Size of the BAR region in bytes
 */
uint64_t pci_get_bar_size(pci_device_t* dev, int bar);

/**
 * Check if BAR is I/O space
 * @param dev   Pointer to PCI device
 * @param bar   BAR index (0-5)
 * @return      true if I/O space, false if memory space
 */
bool pci_bar_is_io(pci_device_t* dev, int bar);

/**
 * Check if BAR is 64-bit memory
 * @param dev   Pointer to PCI device
 * @param bar   BAR index (0-5)
 * @return      true if 64-bit BAR
 */
bool pci_bar_is_64bit(pci_device_t* dev, int bar);

/**
 * Enable bus mastering for a device (required for DMA)
 * @param dev   Pointer to PCI device
 */
void pci_enable_bus_mastering(pci_device_t* dev);

/**
 * Disable bus mastering for a device
 * @param dev   Pointer to PCI device
 */
void pci_disable_bus_mastering(pci_device_t* dev);

/**
 * Enable memory space access for a device
 * @param dev   Pointer to PCI device
 */
void pci_enable_memory_space(pci_device_t* dev);

/**
 * Enable I/O space access for a device
 * @param dev   Pointer to PCI device
 */
void pci_enable_io_space(pci_device_t* dev);

/**
 * Enable interrupts for a device
 * @param dev   Pointer to PCI device
 */
void pci_enable_interrupts(pci_device_t* dev);

/**
 * Disable interrupts for a device
 * @param dev   Pointer to PCI device
 */
void pci_disable_interrupts(pci_device_t* dev);

/* ============================================================================
 * Debug/Utility Functions
 * ============================================================================ */

/**
 * Get human-readable class name
 * @param class_code Class code
 * @return String describing the class
 */
const char* pci_class_name(uint8_t class_code);

/**
 * Print information about a PCI device (for debugging)
 * @param dev Pointer to PCI device
 */
void pci_print_device(pci_device_t* dev);

/**
 * Print all detected PCI devices (for debugging)
 */
void pci_print_all_devices(void);

#endif /* _AAAOS_PCI_H */
