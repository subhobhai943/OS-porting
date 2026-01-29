/**
 * AAAos Kernel - PCI Bus Driver Implementation
 *
 * Implements PCI bus enumeration using Configuration Mechanism #1.
 * This uses I/O ports 0xCF8 (CONFIG_ADDRESS) and 0xCFC (CONFIG_DATA)
 * to access the PCI configuration space.
 */

#include "pci.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"

/* ============================================================================
 * Private Data
 * ============================================================================ */

/* Array of detected PCI devices */
static pci_device_t pci_devices[PCI_MAX_DEVICES];

/* Number of detected devices */
static uint32_t pci_device_count = 0;

/* ============================================================================
 * Private Helper Functions
 * ============================================================================ */

/**
 * Build PCI configuration address
 * Format: (1 << 31) | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC)
 */
static inline uint32_t pci_build_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return (uint32_t)(
        (1U << 31) |                    /* Enable bit */
        ((uint32_t)bus << 16) |         /* Bus number */
        ((uint32_t)(device & 0x1F) << 11) |   /* Device number (5 bits) */
        ((uint32_t)(function & 0x07) << 8) |  /* Function number (3 bits) */
        ((uint32_t)(offset & 0xFC))     /* Register offset (aligned to 4 bytes) */
    );
}

/**
 * Check if a device exists at the given bus/device/function
 */
static bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    uint16_t vendor = pci_read_config16(bus, device, function, PCI_VENDOR_ID);
    return (vendor != PCI_VENDOR_INVALID);
}

/**
 * Read device information and populate pci_device_t structure
 */
static void pci_read_device_info(pci_device_t* dev, uint8_t bus, uint8_t device, uint8_t function) {
    dev->bus = bus;
    dev->device = device;
    dev->function = function;
    dev->present = true;

    /* Read identification */
    dev->vendor_id = pci_read_config16(bus, device, function, PCI_VENDOR_ID);
    dev->device_id = pci_read_config16(bus, device, function, PCI_DEVICE_ID);

    /* Read class information */
    dev->class_code = pci_read_config8(bus, device, function, PCI_CLASS);
    dev->subclass = pci_read_config8(bus, device, function, PCI_SUBCLASS);
    dev->prog_if = pci_read_config8(bus, device, function, PCI_PROG_IF);
    dev->revision = pci_read_config8(bus, device, function, PCI_REVISION_ID);

    /* Read header type and interrupt info */
    dev->header_type = pci_read_config8(bus, device, function, PCI_HEADER_TYPE);
    dev->interrupt_line = pci_read_config8(bus, device, function, PCI_INTERRUPT_LINE);
    dev->interrupt_pin = pci_read_config8(bus, device, function, PCI_INTERRUPT_PIN);

    /* Read Base Address Registers (only for standard header type) */
    if ((dev->header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_NORMAL) {
        for (int i = 0; i < 6; i++) {
            dev->bar[i] = pci_read_config32(bus, device, function, PCI_BAR0 + (i * 4));
        }
    } else {
        /* For bridges, only BAR0 and BAR1 are available */
        for (int i = 0; i < 2; i++) {
            dev->bar[i] = pci_read_config32(bus, device, function, PCI_BAR0 + (i * 4));
        }
        for (int i = 2; i < 6; i++) {
            dev->bar[i] = 0;
        }
    }
}

/**
 * Scan a single PCI function
 */
static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t function) {
    if (!pci_device_exists(bus, device, function)) {
        return;
    }

    if (pci_device_count >= PCI_MAX_DEVICES) {
        kprintf("[PCI] Warning: Device array full, cannot add more devices\n");
        return;
    }

    /* Read device info */
    pci_device_t* dev = &pci_devices[pci_device_count];
    pci_read_device_info(dev, bus, device, function);
    pci_device_count++;

    /* If this is a PCI-to-PCI bridge, scan the secondary bus */
    if (dev->class_code == PCI_CLASS_BRIDGE && dev->subclass == PCI_SUBCLASS_PCI_BRIDGE) {
        uint8_t secondary_bus = pci_read_config8(bus, device, function, 0x19);
        kprintf("[PCI] Found PCI-to-PCI bridge at %02x:%02x.%x, secondary bus %02x\n",
                bus, device, function, secondary_bus);
        /* Note: We don't recursively scan here as the main enumeration will scan all buses */
    }
}

/**
 * Scan a single PCI device (check all functions)
 */
static void pci_scan_device(uint8_t bus, uint8_t device) {
    if (!pci_device_exists(bus, device, 0)) {
        return;
    }

    /* Scan function 0 */
    pci_scan_function(bus, device, 0);

    /* Check if this is a multi-function device */
    uint8_t header_type = pci_read_config8(bus, device, 0, PCI_HEADER_TYPE);
    if (header_type & PCI_HEADER_MULTIFUNCTION) {
        /* Scan functions 1-7 */
        for (uint8_t function = 1; function < 8; function++) {
            pci_scan_function(bus, device, function);
        }
    }
}

/**
 * Scan a single PCI bus
 */
static void pci_scan_bus(uint8_t bus) {
    for (uint8_t device = 0; device < 32; device++) {
        pci_scan_device(bus, device);
    }
}

/* ============================================================================
 * PCI Configuration Space Access
 * ============================================================================ */

uint8_t pci_read_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    /* Read 32 bits and extract the correct byte */
    uint32_t value = inl(PCI_CONFIG_DATA);
    return (uint8_t)((value >> ((offset & 3) * 8)) & 0xFF);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    /* Read 32 bits and extract the correct word */
    uint32_t value = inl(PCI_CONFIG_DATA);
    return (uint16_t)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

uint32_t pci_read_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    /* Read-modify-write to preserve other bytes */
    uint32_t old_value = inl(PCI_CONFIG_DATA);
    int shift = (offset & 3) * 8;
    old_value &= ~(0xFF << shift);
    old_value |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, old_value);
}

void pci_write_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    /* Read-modify-write to preserve other bytes */
    uint32_t old_value = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    old_value &= ~(0xFFFF << shift);
    old_value |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, old_value);
}

void pci_write_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = pci_build_address(bus, device, function, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ============================================================================
 * PCI Initialization and Enumeration
 * ============================================================================ */

void pci_init(void) {
    kprintf("[PCI] Initializing PCI bus driver...\n");

    /* Reset device count */
    pci_device_count = 0;

    /* Check if PCI is available by reading host bridge */
    uint16_t vendor = pci_read_config16(0, 0, 0, PCI_VENDOR_ID);
    if (vendor == PCI_VENDOR_INVALID) {
        kprintf("[PCI] Error: No PCI host bridge detected!\n");
        return;
    }

    kprintf("[PCI] PCI host bridge detected (vendor: 0x%04x)\n", vendor);

    /* Check if host bridge is multi-function (multiple PCI buses) */
    uint8_t header_type = pci_read_config8(0, 0, 0, PCI_HEADER_TYPE);

    if ((header_type & PCI_HEADER_MULTIFUNCTION) == 0) {
        /* Single PCI host controller - scan bus 0 only for direct devices,
         * but scan all possible buses for devices behind bridges */
        kprintf("[PCI] Single PCI host controller, scanning all buses...\n");
        for (int bus = 0; bus < 256; bus++) {
            pci_scan_bus((uint8_t)bus);
        }
    } else {
        /* Multiple PCI host controllers */
        kprintf("[PCI] Multiple PCI host controllers detected\n");
        for (uint8_t function = 0; function < 8; function++) {
            if (!pci_device_exists(0, 0, function)) {
                break;
            }
            /* Each function represents a different PCI domain, scan all buses */
            for (int bus = 0; bus < 256; bus++) {
                pci_scan_bus((uint8_t)bus);
            }
        }
    }

    kprintf("[PCI] Enumeration complete: %u devices found\n", pci_device_count);

    /* Print all detected devices */
    pci_print_all_devices();
}

uint32_t pci_get_device_count(void) {
    return pci_device_count;
}

pci_device_t* pci_get_device(uint32_t index) {
    if (index >= pci_device_count) {
        return NULL;
    }
    return &pci_devices[index];
}

/* ============================================================================
 * PCI Device Discovery
 * ============================================================================ */

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    return pci_find_device_next(vendor_id, device_id, NULL);
}

pci_device_t* pci_find_device_next(uint16_t vendor_id, uint16_t device_id, pci_device_t* start) {
    uint32_t start_index = 0;

    if (start != NULL) {
        /* Find the index of the start device */
        for (uint32_t i = 0; i < pci_device_count; i++) {
            if (&pci_devices[i] == start) {
                start_index = i + 1;
                break;
            }
        }
    }

    for (uint32_t i = start_index; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }

    return NULL;
}

pci_device_t* pci_find_class(uint8_t class_code, uint8_t subclass) {
    return pci_find_class_next(class_code, subclass, NULL);
}

pci_device_t* pci_find_class_next(uint8_t class_code, uint8_t subclass, pci_device_t* start) {
    uint32_t start_index = 0;

    if (start != NULL) {
        /* Find the index of the start device */
        for (uint32_t i = 0; i < pci_device_count; i++) {
            if (&pci_devices[i] == start) {
                start_index = i + 1;
                break;
            }
        }
    }

    for (uint32_t i = start_index; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }

    return NULL;
}

/* ============================================================================
 * PCI Device Configuration
 * ============================================================================ */

uint64_t pci_get_bar(pci_device_t* dev, int bar) {
    if (dev == NULL || bar < 0 || bar > 5) {
        return 0;
    }

    uint32_t bar_value = dev->bar[bar];

    /* Check if it's I/O space */
    if (bar_value & PCI_BAR_IO_SPACE) {
        /* I/O BAR - mask off the type bits */
        return (uint64_t)(bar_value & ~0x3);
    }

    /* Memory BAR */
    uint64_t address = bar_value & ~0xF;  /* Mask off type bits */

    /* Check if it's a 64-bit BAR */
    if ((bar_value & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_64BIT) {
        if (bar < 5) {
            /* Combine with the next BAR for 64-bit address */
            address |= ((uint64_t)dev->bar[bar + 1] << 32);
        }
    }

    return address;
}

uint64_t pci_get_bar_size(pci_device_t* dev, int bar) {
    if (dev == NULL || bar < 0 || bar > 5) {
        return 0;
    }

    uint8_t bus = dev->bus;
    uint8_t device = dev->device;
    uint8_t function = dev->function;
    uint8_t offset = PCI_BAR0 + (bar * 4);

    /* Save original BAR value */
    uint32_t original = pci_read_config32(bus, device, function, offset);

    /* Write all 1s to the BAR */
    pci_write_config32(bus, device, function, offset, 0xFFFFFFFF);

    /* Read back the value */
    uint32_t size_mask = pci_read_config32(bus, device, function, offset);

    /* Restore original BAR value */
    pci_write_config32(bus, device, function, offset, original);

    if (size_mask == 0) {
        return 0;
    }

    /* Calculate size */
    if (original & PCI_BAR_IO_SPACE) {
        /* I/O BAR */
        size_mask &= ~0x3;
        return (uint64_t)(~size_mask + 1) & 0xFFFF;
    } else {
        /* Memory BAR */
        size_mask &= ~0xF;

        /* Handle 64-bit BARs */
        if ((original & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_64BIT && bar < 5) {
            uint32_t original_high = pci_read_config32(bus, device, function, offset + 4);
            pci_write_config32(bus, device, function, offset + 4, 0xFFFFFFFF);
            uint32_t size_mask_high = pci_read_config32(bus, device, function, offset + 4);
            pci_write_config32(bus, device, function, offset + 4, original_high);

            uint64_t full_mask = ((uint64_t)size_mask_high << 32) | size_mask;
            return ~full_mask + 1;
        }

        return (uint64_t)(~size_mask + 1);
    }
}

bool pci_bar_is_io(pci_device_t* dev, int bar) {
    if (dev == NULL || bar < 0 || bar > 5) {
        return false;
    }
    return (dev->bar[bar] & PCI_BAR_IO_SPACE) != 0;
}

bool pci_bar_is_64bit(pci_device_t* dev, int bar) {
    if (dev == NULL || bar < 0 || bar > 5) {
        return false;
    }

    uint32_t bar_value = dev->bar[bar];

    /* I/O BARs are never 64-bit */
    if (bar_value & PCI_BAR_IO_SPACE) {
        return false;
    }

    return (bar_value & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_64BIT;
}

void pci_enable_bus_mastering(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_CMD_BUS_MASTER;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Enabled bus mastering for %02x:%02x.%x\n",
            dev->bus, dev->device, dev->function);
}

void pci_disable_bus_mastering(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command &= ~PCI_CMD_BUS_MASTER;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Disabled bus mastering for %02x:%02x.%x\n",
            dev->bus, dev->device, dev->function);
}

void pci_enable_memory_space(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_CMD_MEMORY_SPACE;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Enabled memory space for %02x:%02x.%x\n",
            dev->bus, dev->device, dev->function);
}

void pci_enable_io_space(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_CMD_IO_SPACE;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Enabled I/O space for %02x:%02x.%x\n",
            dev->bus, dev->device, dev->function);
}

void pci_enable_interrupts(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command &= ~PCI_CMD_INT_DISABLE;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Enabled interrupts for %02x:%02x.%x (IRQ %u)\n",
            dev->bus, dev->device, dev->function, dev->interrupt_line);
}

void pci_disable_interrupts(pci_device_t* dev) {
    if (dev == NULL) {
        return;
    }

    uint16_t command = pci_read_config16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    command |= PCI_CMD_INT_DISABLE;
    pci_write_config16(dev->bus, dev->device, dev->function, PCI_COMMAND, command);

    kprintf("[PCI] Disabled interrupts for %02x:%02x.%x\n",
            dev->bus, dev->device, dev->function);
}

/* ============================================================================
 * Debug/Utility Functions
 * ============================================================================ */

const char* pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:    return "Unclassified";
        case PCI_CLASS_STORAGE:         return "Mass Storage Controller";
        case PCI_CLASS_NETWORK:         return "Network Controller";
        case PCI_CLASS_DISPLAY:         return "Display Controller";
        case PCI_CLASS_MULTIMEDIA:      return "Multimedia Controller";
        case PCI_CLASS_MEMORY:          return "Memory Controller";
        case PCI_CLASS_BRIDGE:          return "Bridge";
        case PCI_CLASS_COMMUNICATION:   return "Communication Controller";
        case PCI_CLASS_SYSTEM:          return "System Peripheral";
        case PCI_CLASS_INPUT:           return "Input Device Controller";
        case PCI_CLASS_DOCKING:         return "Docking Station";
        case PCI_CLASS_PROCESSOR:       return "Processor";
        case PCI_CLASS_SERIAL:          return "Serial Bus Controller";
        case PCI_CLASS_WIRELESS:        return "Wireless Controller";
        case PCI_CLASS_INTELLIGENT:     return "Intelligent Controller";
        case PCI_CLASS_SATELLITE:       return "Satellite Communication";
        case PCI_CLASS_ENCRYPTION:      return "Encryption Controller";
        case PCI_CLASS_SIGNAL:          return "Signal Processing Controller";
        case PCI_CLASS_ACCELERATOR:     return "Processing Accelerator";
        case PCI_CLASS_NON_ESSENTIAL:   return "Non-Essential Instrumentation";
        case PCI_CLASS_COPROCESSOR:     return "Co-Processor";
        case PCI_CLASS_UNDEFINED:       return "Undefined";
        default:                        return "Unknown";
    }
}

void pci_print_device(pci_device_t* dev) {
    if (dev == NULL || !dev->present) {
        return;
    }

    kprintf("  %02x:%02x.%x - %04x:%04x - %s (class %02x:%02x, rev %02x)\n",
            dev->bus, dev->device, dev->function,
            dev->vendor_id, dev->device_id,
            pci_class_name(dev->class_code),
            dev->class_code, dev->subclass,
            dev->revision);

    /* Print BARs if any are configured */
    for (int i = 0; i < 6; i++) {
        if (dev->bar[i] != 0) {
            uint64_t addr = pci_get_bar(dev, i);
            bool is_io = pci_bar_is_io(dev, i);
            bool is_64bit = pci_bar_is_64bit(dev, i);

            if (is_io) {
                kprintf("           BAR%d: I/O  0x%04x\n", i, (uint32_t)addr);
            } else if (is_64bit) {
                kprintf("           BAR%d: Mem  0x%016llx (64-bit)\n", i, addr);
                i++;  /* Skip next BAR (part of 64-bit address) */
            } else {
                kprintf("           BAR%d: Mem  0x%08x (32-bit)\n", i, (uint32_t)addr);
            }
        }
    }

    /* Print interrupt info if configured */
    if (dev->interrupt_line != 0 && dev->interrupt_line != 0xFF) {
        kprintf("           IRQ: %u (pin %c)\n",
                dev->interrupt_line, 'A' + dev->interrupt_pin - 1);
    }
}

void pci_print_all_devices(void) {
    kprintf("[PCI] Detected devices:\n");
    kprintf("------------------------------------------------------------\n");

    for (uint32_t i = 0; i < pci_device_count; i++) {
        pci_print_device(&pci_devices[i]);
    }

    kprintf("------------------------------------------------------------\n");
    kprintf("[PCI] Total: %u devices\n", pci_device_count);
}
