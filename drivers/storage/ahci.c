/**
 * AAAos Kernel - AHCI (Advanced Host Controller Interface) Driver
 *
 * Implementation of SATA storage access via AHCI controllers.
 * Based on AHCI 1.3 specification.
 */

#include "ahci.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"
#include "../../kernel/arch/x86_64/io.h"

/* Maximum number of AHCI controllers supported */
#define AHCI_MAX_CONTROLLERS    4

/* Command timeout in milliseconds (approximately) */
#define AHCI_CMD_TIMEOUT        5000
#define AHCI_SPIN_TIMEOUT       1000000

/* Memory allocation sizes */
#define AHCI_CMD_LIST_SIZE      (sizeof(ahci_cmd_header_t) * AHCI_MAX_CMD_SLOTS)  /* 1KB */
#define AHCI_FIS_SIZE           256
#define AHCI_CMD_TABLE_SIZE     (sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * AHCI_MAX_PRDT_ENTRIES)

/* Global state */
static ahci_controller_t ahci_controllers[AHCI_MAX_CONTROLLERS];
static int ahci_controller_count = 0;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Simple memory set
 */
static void ahci_memset(void* dest, uint8_t val, size_t size) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < size; i++) {
        d[i] = val;
    }
}

/**
 * Simple memory copy
 */
static void ahci_memcpy(void* dest, const void* src, size_t size) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

/**
 * Simple delay loop (approximately microseconds)
 */
static void ahci_delay(uint32_t microseconds) {
    /* This is a rough approximation - real implementation would use timer */
    volatile uint32_t count = microseconds * 100;
    while (count--) {
        __asm__ __volatile__("pause");
    }
}

/**
 * Copy and trim ATA string (byte-swapped)
 */
static void ahci_copy_ata_string(char* dest, const uint16_t* src, int words) {
    for (int i = 0; i < words; i++) {
        dest[i * 2] = (char)(src[i] >> 8);
        dest[i * 2 + 1] = (char)(src[i] & 0xFF);
    }
    dest[words * 2] = '\0';

    /* Trim trailing spaces */
    int len = words * 2;
    while (len > 0 && dest[len - 1] == ' ') {
        dest[--len] = '\0';
    }
}

/* ============================================================================
 * Port Management
 * ============================================================================ */

/**
 * Get port register address
 */
static ahci_port_t* ahci_get_port(ahci_controller_t* ctrl, int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) {
        return NULL;
    }
    if (!(ctrl->ports_impl & (1 << port))) {
        return NULL;
    }
    return &ctrl->hba->ports[port];
}

/**
 * Check the type of device on a port by its signature
 */
static ahci_device_type_t ahci_check_port_type(ahci_port_t* port) {
    uint32_t ssts = port->ssts;

    uint8_t det = ssts & 0x0F;          /* Device detection */
    uint8_t ipm = (ssts >> 8) & 0x0F;   /* Interface power management */

    /* Check if device present and communication established */
    if (det != AHCI_PORT_DET_PHY) {
        return AHCI_DEV_NULL;
    }
    if (ipm != AHCI_PORT_IPM_ACTIVE) {
        return AHCI_DEV_NULL;
    }

    /* Check signature */
    switch (port->sig) {
        case AHCI_SIG_ATA:
            return AHCI_DEV_SATA;
        case AHCI_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case AHCI_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case AHCI_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_NULL;
    }
}

/**
 * Stop command engine on a port
 */
static int ahci_stop_cmd(ahci_port_t* port) {
    /* Clear ST (Start) bit */
    port->cmd &= ~AHCI_PORT_CMD_ST;

    /* Wait for CR (Command List Running) to clear */
    int timeout = AHCI_SPIN_TIMEOUT;
    while (port->cmd & AHCI_PORT_CMD_CR) {
        if (--timeout == 0) {
            kprintf("[AHCI] Timeout waiting for command list to stop\n");
            return AHCI_ERR_TIMEOUT;
        }
        ahci_delay(1);
    }

    /* Clear FRE (FIS Receive Enable) bit */
    port->cmd &= ~AHCI_PORT_CMD_FRE;

    /* Wait for FR (FIS Receive Running) to clear */
    timeout = AHCI_SPIN_TIMEOUT;
    while (port->cmd & AHCI_PORT_CMD_FR) {
        if (--timeout == 0) {
            kprintf("[AHCI] Timeout waiting for FIS receive to stop\n");
            return AHCI_ERR_TIMEOUT;
        }
        ahci_delay(1);
    }

    return AHCI_SUCCESS;
}

/**
 * Start command engine on a port
 */
static void ahci_start_cmd(ahci_port_t* port) {
    /* Wait until CR is cleared */
    while (port->cmd & AHCI_PORT_CMD_CR) {
        ahci_delay(1);
    }

    /* Set FRE and ST */
    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

/**
 * Allocate memory for port command structures
 */
static int ahci_port_alloc_memory(ahci_controller_t* ctrl, int port) {
    ahci_port_t* pt = ahci_get_port(ctrl, port);
    ahci_port_info_t* info = &ctrl->port_info[port];

    if (!pt) {
        return AHCI_ERR_INVALID_PORT;
    }

    /* Stop command engine first */
    ahci_stop_cmd(pt);

    /* Allocate Command List (1KB, 1KB aligned) */
    physaddr_t cmd_list_phys = pmm_alloc_page();
    if (cmd_list_phys == 0) {
        kprintf("[AHCI] Failed to allocate command list for port %d\n", port);
        return AHCI_ERR_NO_MEMORY;
    }

    /* Map to virtual address - using direct physical mapping */
    virtaddr_t cmd_list_virt = VMM_KERNEL_PHYS_MAP + cmd_list_phys;
    info->cmd_list = (ahci_cmd_header_t*)cmd_list_virt;
    ahci_memset(info->cmd_list, 0, PAGE_SIZE);

    /* Set CLB (Command List Base) */
    pt->clb = (uint32_t)(cmd_list_phys & 0xFFFFFFFF);
    pt->clbu = (uint32_t)(cmd_list_phys >> 32);

    /* Allocate FIS area (256 bytes, 256-byte aligned) */
    physaddr_t fis_phys = pmm_alloc_page();
    if (fis_phys == 0) {
        kprintf("[AHCI] Failed to allocate FIS area for port %d\n", port);
        pmm_free_page(cmd_list_phys);
        return AHCI_ERR_NO_MEMORY;
    }

    virtaddr_t fis_virt = VMM_KERNEL_PHYS_MAP + fis_phys;
    info->fis_area = (ahci_fis_t*)fis_virt;
    ahci_memset(info->fis_area, 0, PAGE_SIZE);

    /* Set FB (FIS Base) */
    pt->fb = (uint32_t)(fis_phys & 0xFFFFFFFF);
    pt->fbu = (uint32_t)(fis_phys >> 32);

    /* Allocate command tables for each command slot */
    for (uint32_t i = 0; i < ctrl->num_cmd_slots; i++) {
        physaddr_t ct_phys = pmm_alloc_page();
        if (ct_phys == 0) {
            kprintf("[AHCI] Failed to allocate command table %d for port %d\n", i, port);
            /* Clean up previously allocated tables */
            for (uint32_t j = 0; j < i; j++) {
                physaddr_t prev_phys = (physaddr_t)((virtaddr_t)info->cmd_tables[j] - VMM_KERNEL_PHYS_MAP);
                pmm_free_page(prev_phys);
            }
            pmm_free_page(cmd_list_phys);
            pmm_free_page(fis_phys);
            return AHCI_ERR_NO_MEMORY;
        }

        virtaddr_t ct_virt = VMM_KERNEL_PHYS_MAP + ct_phys;
        info->cmd_tables[i] = (ahci_cmd_table_t*)ct_virt;
        ahci_memset(info->cmd_tables[i], 0, PAGE_SIZE);

        /* Set up command header to point to this table */
        info->cmd_list[i].ctba = (uint32_t)(ct_phys & 0xFFFFFFFF);
        info->cmd_list[i].ctbau = (uint32_t)(ct_phys >> 32);
    }

    /* Clear interrupt status and error */
    pt->serr = (uint32_t)-1;    /* Clear all error bits */
    pt->is = (uint32_t)-1;      /* Clear all interrupt status bits */

    /* Enable interrupts we care about */
    pt->ie = AHCI_PORT_INT_DHRS | AHCI_PORT_INT_PSS | AHCI_PORT_INT_DSS |
             AHCI_PORT_INT_SDBS | AHCI_PORT_INT_DPS | AHCI_PORT_INT_TFES;

    /* Start command engine */
    ahci_start_cmd(pt);

    return AHCI_SUCCESS;
}

/**
 * Find a free command slot
 */
static int ahci_find_cmdslot(ahci_port_t* port, uint32_t num_slots) {
    /* Slots in use are set in SACT and CI registers */
    uint32_t slots = port->sact | port->ci;

    for (uint32_t i = 0; i < num_slots; i++) {
        if ((slots & (1 << i)) == 0) {
            return i;
        }
    }

    return -1;  /* No free slot */
}

/**
 * Wait for command completion
 */
static int ahci_wait_cmd(ahci_port_t* port, int slot, int timeout_ms) {
    uint32_t slot_bit = 1 << slot;
    int spin = 0;

    /* Wait for completion */
    while (1) {
        /* Check if slot is clear (command completed) */
        if ((port->ci & slot_bit) == 0) {
            break;
        }

        /* Check for errors */
        if (port->is & AHCI_PORT_INT_TFES) {
            kprintf("[AHCI] Task file error during command\n");
            return AHCI_ERR_TASK_FILE;
        }

        spin++;
        if (spin >= timeout_ms * 1000) {
            kprintf("[AHCI] Command timeout\n");
            return AHCI_ERR_TIMEOUT;
        }

        ahci_delay(1);
    }

    /* Check for errors after completion */
    if (port->is & AHCI_PORT_INT_TFES) {
        kprintf("[AHCI] Task file error after command\n");
        return AHCI_ERR_TASK_FILE;
    }

    return AHCI_SUCCESS;
}

/* ============================================================================
 * ATA Commands
 * ============================================================================ */

/**
 * Issue an ATA command
 */
static int ahci_issue_cmd(ahci_controller_t* ctrl, int port_num,
                          ahci_fis_reg_h2d_t* fis,
                          void* buf, uint32_t buf_size,
                          int write) {
    ahci_port_t* port = ahci_get_port(ctrl, port_num);
    ahci_port_info_t* info = &ctrl->port_info[port_num];

    if (!port || !info->present) {
        return AHCI_ERR_NO_DEVICE;
    }

    /* Clear interrupt status */
    port->is = (uint32_t)-1;

    /* Find a free command slot */
    int slot = ahci_find_cmdslot(port, ctrl->num_cmd_slots);
    if (slot < 0) {
        kprintf("[AHCI] No free command slot for port %d\n", port_num);
        return AHCI_ERR_NOT_READY;
    }

    /* Get command header and table */
    ahci_cmd_header_t* hdr = &info->cmd_list[slot];
    ahci_cmd_table_t* tbl = info->cmd_tables[slot];

    /* Clear command table */
    ahci_memset(tbl, 0, sizeof(ahci_cmd_table_t) + sizeof(ahci_prdt_entry_t) * AHCI_MAX_PRDT_ENTRIES);

    /* Set up command header */
    hdr->cfl = sizeof(ahci_fis_reg_h2d_t) / 4;  /* FIS length in DWORDs */
    hdr->w = write ? 1 : 0;                      /* Write direction */
    hdr->a = 0;                                  /* Not ATAPI */
    hdr->p = 0;                                  /* Not prefetchable */
    hdr->r = 0;                                  /* Not reset */
    hdr->b = 0;                                  /* Not BIST */
    hdr->c = 1;                                  /* Clear busy on R_OK */
    hdr->pmp = 0;                                /* Port multiplier port */
    hdr->prdtl = 0;                              /* Will set if buffer provided */
    hdr->prdbc = 0;                              /* PRD byte count */

    /* Copy the FIS to command table */
    ahci_memcpy(tbl->cfis, fis, sizeof(ahci_fis_reg_h2d_t));

    /* Set up PRDT if buffer provided */
    if (buf && buf_size > 0) {
        /* Get physical address of buffer */
        physaddr_t buf_phys = (physaddr_t)((virtaddr_t)buf - VMM_KERNEL_PHYS_MAP);

        /* Calculate number of PRD entries needed */
        uint32_t prdt_count = (buf_size + 0x3FFFFF) / 0x400000;  /* Max 4MB per entry */
        if (prdt_count > AHCI_MAX_PRDT_ENTRIES) {
            prdt_count = AHCI_MAX_PRDT_ENTRIES;
        }

        hdr->prdtl = prdt_count;

        /* Set up PRD entries */
        uint32_t remaining = buf_size;
        physaddr_t addr = buf_phys;

        for (uint32_t i = 0; i < prdt_count; i++) {
            uint32_t chunk_size = (remaining > 0x400000) ? 0x400000 : remaining;

            tbl->prdt_entry[i].dba = (uint32_t)(addr & 0xFFFFFFFF);
            tbl->prdt_entry[i].dbau = (uint32_t)(addr >> 32);
            tbl->prdt_entry[i].dbc = chunk_size - 1;  /* 0-based count */
            tbl->prdt_entry[i].i = (i == prdt_count - 1) ? 1 : 0;  /* Interrupt on last */

            addr += chunk_size;
            remaining -= chunk_size;
        }
    }

    /* Issue the command */
    port->ci = 1 << slot;

    /* Wait for completion */
    int result = ahci_wait_cmd(port, slot, AHCI_CMD_TIMEOUT);

    return result;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * Initialize a single AHCI controller
 */
static int ahci_init_controller(pci_device_t* pci_dev) {
    if (ahci_controller_count >= AHCI_MAX_CONTROLLERS) {
        kprintf("[AHCI] Maximum controller count reached\n");
        return -1;
    }

    ahci_controller_t* ctrl = &ahci_controllers[ahci_controller_count];
    ahci_memset(ctrl, 0, sizeof(ahci_controller_t));

    ctrl->pci_dev = pci_dev;

    /* Get ABAR (AHCI Base Address Register) from BAR5 */
    uint64_t abar = pci_get_bar(pci_dev, 5);
    if (abar == 0) {
        kprintf("[AHCI] BAR5 not configured for device %02x:%02x.%x\n",
                pci_dev->bus, pci_dev->device, pci_dev->function);
        return -1;
    }

    kprintf("[AHCI] Found controller at %02x:%02x.%x, ABAR=0x%llx\n",
            pci_dev->bus, pci_dev->device, pci_dev->function, abar);

    /* Enable PCI bus mastering and memory space access */
    pci_enable_bus_mastering(pci_dev);
    pci_enable_memory_space(pci_dev);

    /* Map the HBA memory - assuming identity or direct mapping available */
    ctrl->hba = (ahci_hba_t*)(VMM_KERNEL_PHYS_MAP + abar);

    /* Read capabilities */
    uint32_t cap = ctrl->hba->cap;
    ctrl->num_ports = (cap & AHCI_CAP_NP_MASK) + 1;
    ctrl->num_cmd_slots = ((cap & AHCI_CAP_NCS_MASK) >> AHCI_CAP_NCS_SHIFT) + 1;
    ctrl->supports_64bit = (cap & AHCI_CAP_S64A) != 0;
    ctrl->ports_impl = ctrl->hba->pi;

    kprintf("[AHCI] Capabilities: %d ports, %d cmd slots, 64-bit=%s\n",
            ctrl->num_ports, ctrl->num_cmd_slots,
            ctrl->supports_64bit ? "yes" : "no");
    kprintf("[AHCI] Ports implemented: 0x%08x\n", ctrl->ports_impl);

    /* Print version */
    uint32_t vs = ctrl->hba->vs;
    kprintf("[AHCI] Version: %d.%d%d\n",
            (vs >> 16) & 0xFF, (vs >> 8) & 0xFF, vs & 0xFF);

    /* Enable AHCI mode if needed */
    if (!(ctrl->hba->ghc & AHCI_GHC_AE)) {
        kprintf("[AHCI] Enabling AHCI mode\n");
        ctrl->hba->ghc |= AHCI_GHC_AE;
    }

    /* Perform HBA reset */
    kprintf("[AHCI] Performing HBA reset\n");
    ctrl->hba->ghc |= AHCI_GHC_HR;

    /* Wait for reset to complete (HR bit clears) */
    int timeout = AHCI_SPIN_TIMEOUT;
    while (ctrl->hba->ghc & AHCI_GHC_HR) {
        if (--timeout == 0) {
            kprintf("[AHCI] HBA reset timeout\n");
            return -1;
        }
        ahci_delay(1);
    }

    /* Re-enable AHCI mode after reset */
    ctrl->hba->ghc |= AHCI_GHC_AE;

    /* Clear global interrupt status */
    ctrl->hba->is = (uint32_t)-1;

    /* Enable interrupts */
    ctrl->hba->ghc |= AHCI_GHC_IE;

    /* Probe each implemented port */
    int devices_found = 0;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ctrl->ports_impl & (1 << i)) {
            ahci_port_t* port = &ctrl->hba->ports[i];
            ahci_device_type_t type = ahci_check_port_type(port);

            ctrl->port_info[i].type = type;
            ctrl->port_info[i].present = (type != AHCI_DEV_NULL);

            if (type != AHCI_DEV_NULL) {
                const char* type_str;
                switch (type) {
                    case AHCI_DEV_SATA:   type_str = "SATA"; break;
                    case AHCI_DEV_SATAPI: type_str = "SATAPI"; break;
                    case AHCI_DEV_SEMB:   type_str = "SEMB"; break;
                    case AHCI_DEV_PM:     type_str = "PM"; break;
                    default:              type_str = "Unknown"; break;
                }
                kprintf("[AHCI] Port %d: %s device found (sig=0x%08x)\n",
                        i, type_str, port->sig);

                /* Initialize port memory structures */
                if (ahci_port_alloc_memory(ctrl, i) == AHCI_SUCCESS) {
                    devices_found++;

                    /* For SATA drives, get identification info */
                    if (type == AHCI_DEV_SATA) {
                        /* Allocate buffer for identify data */
                        physaddr_t id_phys = pmm_alloc_page();
                        if (id_phys != 0) {
                            void* id_buf = (void*)(VMM_KERNEL_PHYS_MAP + id_phys);
                            ahci_memset(id_buf, 0, 512);

                            if (ahci_identify(i, id_buf) == AHCI_SUCCESS) {
                                uint16_t* id = (uint16_t*)id_buf;

                                /* Get model string (words 27-46) */
                                ahci_copy_ata_string(ctrl->port_info[i].model, &id[27], 20);

                                /* Get serial number (words 10-19) */
                                ahci_copy_ata_string(ctrl->port_info[i].serial, &id[10], 10);

                                /* Get sector count (words 100-103 for 48-bit LBA) */
                                ctrl->port_info[i].sector_count =
                                    ((uint64_t)id[103] << 48) |
                                    ((uint64_t)id[102] << 32) |
                                    ((uint64_t)id[101] << 16) |
                                    (uint64_t)id[100];

                                /* If 48-bit count is 0, use 28-bit count */
                                if (ctrl->port_info[i].sector_count == 0) {
                                    ctrl->port_info[i].sector_count =
                                        ((uint32_t)id[61] << 16) | id[60];
                                }

                                uint64_t size_mb = (ctrl->port_info[i].sector_count * 512) / (1024 * 1024);
                                kprintf("[AHCI] Port %d: Model: %s\n", i, ctrl->port_info[i].model);
                                kprintf("[AHCI] Port %d: Serial: %s\n", i, ctrl->port_info[i].serial);
                                kprintf("[AHCI] Port %d: Capacity: %llu MB (%llu sectors)\n",
                                        i, size_mb, ctrl->port_info[i].sector_count);
                            }

                            pmm_free_page(id_phys);
                        }
                    }
                } else {
                    ctrl->port_info[i].present = false;
                }
            }
        }
    }

    kprintf("[AHCI] Controller initialized with %d device(s)\n", devices_found);
    ahci_controller_count++;

    return devices_found;
}

int ahci_init(void) {
    kprintf("[AHCI] Initializing AHCI driver\n");

    ahci_controller_count = 0;
    int total_devices = 0;

    /* Find all AHCI controllers (class 0x01, subclass 0x06, prog_if 0x01) */
    pci_device_t* dev = NULL;

    while ((dev = pci_find_class_next(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, dev)) != NULL) {
        /* Check if this is AHCI (prog_if = 0x01) */
        if (dev->prog_if == AHCI_PROG_IF) {
            int devices = ahci_init_controller(dev);
            if (devices > 0) {
                total_devices += devices;
            }
        }
    }

    if (ahci_controller_count == 0) {
        kprintf("[AHCI] No AHCI controllers found\n");
    } else {
        kprintf("[AHCI] Found %d AHCI controller(s) with %d device(s)\n",
                ahci_controller_count, total_devices);
    }

    return ahci_controller_count;
}

ahci_device_type_t ahci_probe_port(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) {
        return AHCI_DEV_NULL;
    }

    /* Check all controllers for this port */
    for (int i = 0; i < ahci_controller_count; i++) {
        ahci_controller_t* ctrl = &ahci_controllers[i];
        if (ctrl->ports_impl & (1 << port)) {
            return ctrl->port_info[port].type;
        }
    }

    return AHCI_DEV_NULL;
}

ahci_port_info_t* ahci_get_port_info(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) {
        return NULL;
    }

    /* Check all controllers for this port */
    for (int i = 0; i < ahci_controller_count; i++) {
        ahci_controller_t* ctrl = &ahci_controllers[i];
        if ((ctrl->ports_impl & (1 << port)) && ctrl->port_info[port].present) {
            return &ctrl->port_info[port];
        }
    }

    return NULL;
}

int ahci_identify(int port, void* buf) {
    if (port < 0 || port >= AHCI_MAX_PORTS || !buf) {
        return AHCI_ERR_INVALID_PORT;
    }

    /* Find controller with this port */
    ahci_controller_t* ctrl = NULL;
    for (int i = 0; i < ahci_controller_count; i++) {
        if (ahci_controllers[i].ports_impl & (1 << port)) {
            ctrl = &ahci_controllers[i];
            break;
        }
    }

    if (!ctrl || !ctrl->port_info[port].present) {
        return AHCI_ERR_NO_DEVICE;
    }

    /* Build IDENTIFY DEVICE command */
    ahci_fis_reg_h2d_t fis;
    ahci_memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.c = 1;  /* This is a command */
    fis.command = (ctrl->port_info[port].type == AHCI_DEV_SATAPI)
                  ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
    fis.device = 0;

    /* Issue command */
    return ahci_issue_cmd(ctrl, port, &fis, buf, 512, 0);
}

int ahci_read_sectors(int port, uint64_t lba, uint32_t count, void* buf) {
    if (port < 0 || port >= AHCI_MAX_PORTS || !buf || count == 0) {
        return AHCI_ERR_INVALID_PORT;
    }

    if (count > 65535) {
        kprintf("[AHCI] Read count %u exceeds maximum of 65535\n", count);
        return AHCI_ERR_INVALID_PORT;
    }

    /* Find controller with this port */
    ahci_controller_t* ctrl = NULL;
    for (int i = 0; i < ahci_controller_count; i++) {
        if (ahci_controllers[i].ports_impl & (1 << port)) {
            ctrl = &ahci_controllers[i];
            break;
        }
    }

    if (!ctrl || !ctrl->port_info[port].present) {
        return AHCI_ERR_NO_DEVICE;
    }

    if (ctrl->port_info[port].type != AHCI_DEV_SATA) {
        return AHCI_ERR_UNSUPPORTED;
    }

    kprintf("[AHCI] Reading %u sectors from LBA %llu on port %d\n", count, lba, port);

    /* Build READ DMA EXT command */
    ahci_fis_reg_h2d_t fis;
    ahci_memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.c = 1;  /* Command */
    fis.command = ATA_CMD_READ_DMA_EXT;

    /* LBA mode, 48-bit addressing */
    fis.device = 1 << 6;  /* LBA mode */

    /* Set LBA */
    fis.lba0 = (uint8_t)(lba & 0xFF);
    fis.lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis.lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis.lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis.lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis.lba5 = (uint8_t)((lba >> 40) & 0xFF);

    /* Set sector count */
    fis.countl = (uint8_t)(count & 0xFF);
    fis.counth = (uint8_t)((count >> 8) & 0xFF);

    /* Issue command */
    int result = ahci_issue_cmd(ctrl, port, &fis, buf, count * AHCI_SECTOR_SIZE, 0);

    if (result == AHCI_SUCCESS) {
        kprintf("[AHCI] Read completed successfully\n");
    } else {
        kprintf("[AHCI] Read failed with error %d\n", result);
    }

    return result;
}

int ahci_write_sectors(int port, uint64_t lba, uint32_t count, const void* buf) {
    if (port < 0 || port >= AHCI_MAX_PORTS || !buf || count == 0) {
        return AHCI_ERR_INVALID_PORT;
    }

    if (count > 65535) {
        kprintf("[AHCI] Write count %u exceeds maximum of 65535\n", count);
        return AHCI_ERR_INVALID_PORT;
    }

    /* Find controller with this port */
    ahci_controller_t* ctrl = NULL;
    for (int i = 0; i < ahci_controller_count; i++) {
        if (ahci_controllers[i].ports_impl & (1 << port)) {
            ctrl = &ahci_controllers[i];
            break;
        }
    }

    if (!ctrl || !ctrl->port_info[port].present) {
        return AHCI_ERR_NO_DEVICE;
    }

    if (ctrl->port_info[port].type != AHCI_DEV_SATA) {
        return AHCI_ERR_UNSUPPORTED;
    }

    kprintf("[AHCI] Writing %u sectors to LBA %llu on port %d\n", count, lba, port);

    /* Build WRITE DMA EXT command */
    ahci_fis_reg_h2d_t fis;
    ahci_memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.c = 1;  /* Command */
    fis.command = ATA_CMD_WRITE_DMA_EXT;

    /* LBA mode, 48-bit addressing */
    fis.device = 1 << 6;  /* LBA mode */

    /* Set LBA */
    fis.lba0 = (uint8_t)(lba & 0xFF);
    fis.lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis.lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis.lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis.lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis.lba5 = (uint8_t)((lba >> 40) & 0xFF);

    /* Set sector count */
    fis.countl = (uint8_t)(count & 0xFF);
    fis.counth = (uint8_t)((count >> 8) & 0xFF);

    /* Issue command (write direction) */
    int result = ahci_issue_cmd(ctrl, port, &fis, (void*)buf, count * AHCI_SECTOR_SIZE, 1);

    if (result == AHCI_SUCCESS) {
        kprintf("[AHCI] Write completed successfully\n");
    } else {
        kprintf("[AHCI] Write failed with error %d\n", result);
    }

    return result;
}

int ahci_flush(int port) {
    if (port < 0 || port >= AHCI_MAX_PORTS) {
        return AHCI_ERR_INVALID_PORT;
    }

    /* Find controller with this port */
    ahci_controller_t* ctrl = NULL;
    for (int i = 0; i < ahci_controller_count; i++) {
        if (ahci_controllers[i].ports_impl & (1 << port)) {
            ctrl = &ahci_controllers[i];
            break;
        }
    }

    if (!ctrl || !ctrl->port_info[port].present) {
        return AHCI_ERR_NO_DEVICE;
    }

    if (ctrl->port_info[port].type != AHCI_DEV_SATA) {
        return AHCI_ERR_UNSUPPORTED;
    }

    kprintf("[AHCI] Flushing cache on port %d\n", port);

    /* Build FLUSH CACHE EXT command */
    ahci_fis_reg_h2d_t fis;
    ahci_memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.c = 1;  /* Command */
    fis.command = ATA_CMD_FLUSH_CACHE_EXT;

    /* Issue command (no data transfer) */
    return ahci_issue_cmd(ctrl, port, &fis, NULL, 0, 0);
}

int ahci_get_controller_count(void) {
    return ahci_controller_count;
}

ahci_controller_t* ahci_get_controller(int index) {
    if (index < 0 || index >= ahci_controller_count) {
        return NULL;
    }
    return &ahci_controllers[index];
}
