/**
 * AAAos Network Driver - Intel e1000 Gigabit Ethernet Controller
 *
 * Implementation of the e1000 driver for QEMU's default NIC.
 */

#include "e1000.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/pmm.h"
#include "../../kernel/mm/vmm.h"

/* Global device state */
static e1000_device_t e1000_dev;

/* Forward declarations */
static bool e1000_detect_pci(void);
static void e1000_read_mac(void);
static bool e1000_init_rx(void);
static bool e1000_init_tx(void);
static void e1000_enable_interrupts(void);
static uint16_t e1000_eeprom_read(uint8_t addr);

/* Memory copy function (simple implementation) */
static void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* Memory set function */
static void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

/**
 * Read a 32-bit value from e1000 MMIO register
 */
static inline uint32_t e1000_read_reg(uint32_t offset) {
    return *((volatile uint32_t *)(e1000_dev.mmio_base + offset));
}

/**
 * Write a 32-bit value to e1000 MMIO register
 */
static inline void e1000_write_reg(uint32_t offset, uint32_t value) {
    *((volatile uint32_t *)(e1000_dev.mmio_base + offset)) = value;
}

/**
 * PCI configuration space read (32-bit)
 */
static uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31) |              /* Enable bit */
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);          /* Align to 4 bytes */
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

/**
 * PCI configuration space write (32-bit)
 */
static void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1U << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

/**
 * Scan PCI bus for e1000 device
 */
static bool e1000_detect_pci(void) {
    kprintf("[e1000] Scanning PCI bus for Intel e1000...\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config(bus, device, func, PCI_VENDOR_ID);
                uint16_t vendor = vendor_device & 0xFFFF;
                uint16_t dev_id = (vendor_device >> 16) & 0xFFFF;

                /* Skip invalid entries */
                if (vendor == 0xFFFF) {
                    continue;
                }

                /* Check for e1000 */
                if (vendor == E1000_VENDOR_ID && dev_id == E1000_DEVICE_ID) {
                    e1000_dev.pci_bus = bus;
                    e1000_dev.pci_device = device;
                    e1000_dev.pci_function = func;

                    kprintf("[e1000] Found Intel e1000 at PCI %d:%d.%d\n",
                            bus, device, func);

                    /* Get BAR0 (MMIO base address) */
                    uint32_t bar0 = pci_read_config(bus, device, func, PCI_BAR0);
                    if (bar0 & 0x01) {
                        kprintf("[e1000] ERROR: BAR0 is I/O mapped, expected MMIO\n");
                        return false;
                    }

                    /* BAR0 bits [31:4] contain the base address */
                    e1000_dev.mmio_phys = bar0 & 0xFFFFFFF0;
                    e1000_dev.mmio_size = 128 * 1024;  /* 128 KB MMIO region */

                    kprintf("[e1000] MMIO base physical: 0x%x\n", (uint32_t)e1000_dev.mmio_phys);

                    /* Get interrupt line */
                    uint32_t int_info = pci_read_config(bus, device, func, PCI_INTERRUPT_LINE);
                    e1000_dev.irq = int_info & 0xFF;
                    kprintf("[e1000] IRQ: %d\n", e1000_dev.irq);

                    /* Enable bus mastering and memory space access */
                    uint32_t cmd = pci_read_config(bus, device, func, PCI_COMMAND);
                    cmd |= PCI_CMD_MEMORY | PCI_CMD_BUS_MASTER;
                    cmd &= ~PCI_CMD_INT_DISABLE;  /* Enable interrupts */
                    pci_write_config(bus, device, func, PCI_COMMAND, cmd);

                    kprintf("[e1000] PCI command register: 0x%x\n", cmd);

                    return true;
                }
            }
        }
    }

    kprintf("[e1000] No Intel e1000 device found\n");
    return false;
}

/**
 * Read MAC address from EEPROM or RAL/RAH registers
 */
static void e1000_read_mac(void) {
    uint32_t ral, rah;

    /* First try reading from EEPROM */
    uint32_t eecd = e1000_read_reg(E1000_EECD);
    bool has_eeprom = false;

    /* Try EEPROM read to detect if present */
    e1000_write_reg(E1000_EERD, E1000_EERD_START);
    for (int i = 0; i < 1000; i++) {
        uint32_t val = e1000_read_reg(E1000_EERD);
        if (val & E1000_EERD_DONE) {
            has_eeprom = true;
            break;
        }
    }

    if (has_eeprom) {
        kprintf("[e1000] Reading MAC from EEPROM\n");
        uint16_t mac_word;

        /* Read MAC address words from EEPROM */
        mac_word = e1000_eeprom_read(0);
        e1000_dev.mac[0] = mac_word & 0xFF;
        e1000_dev.mac[1] = (mac_word >> 8) & 0xFF;

        mac_word = e1000_eeprom_read(1);
        e1000_dev.mac[2] = mac_word & 0xFF;
        e1000_dev.mac[3] = (mac_word >> 8) & 0xFF;

        mac_word = e1000_eeprom_read(2);
        e1000_dev.mac[4] = mac_word & 0xFF;
        e1000_dev.mac[5] = (mac_word >> 8) & 0xFF;
    } else {
        /* Read from RAL/RAH registers (hardcoded in some emulators) */
        kprintf("[e1000] Reading MAC from RAL/RAH registers\n");
        ral = e1000_read_reg(E1000_RAL);
        rah = e1000_read_reg(E1000_RAH);

        e1000_dev.mac[0] = ral & 0xFF;
        e1000_dev.mac[1] = (ral >> 8) & 0xFF;
        e1000_dev.mac[2] = (ral >> 16) & 0xFF;
        e1000_dev.mac[3] = (ral >> 24) & 0xFF;
        e1000_dev.mac[4] = rah & 0xFF;
        e1000_dev.mac[5] = (rah >> 8) & 0xFF;
    }

    kprintf("[e1000] MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            e1000_dev.mac[0], e1000_dev.mac[1], e1000_dev.mac[2],
            e1000_dev.mac[3], e1000_dev.mac[4], e1000_dev.mac[5]);

    /* Write MAC to RAL/RAH to ensure it's set */
    ral = e1000_dev.mac[0] |
          ((uint32_t)e1000_dev.mac[1] << 8) |
          ((uint32_t)e1000_dev.mac[2] << 16) |
          ((uint32_t)e1000_dev.mac[3] << 24);
    rah = e1000_dev.mac[4] |
          ((uint32_t)e1000_dev.mac[5] << 8) |
          E1000_RAH_AV;  /* Address Valid bit */

    e1000_write_reg(E1000_RAL, ral);
    e1000_write_reg(E1000_RAH, rah);
}

/**
 * Read a word from EEPROM
 */
static uint16_t e1000_eeprom_read(uint8_t addr) {
    uint32_t val;

    /* Start read operation */
    e1000_write_reg(E1000_EERD, E1000_EERD_START | ((uint32_t)addr << E1000_EERD_ADDR_SHIFT));

    /* Wait for read to complete */
    while (!((val = e1000_read_reg(E1000_EERD)) & E1000_EERD_DONE)) {
        /* Busy wait */
    }

    return (val >> E1000_EERD_DATA_SHIFT) & 0xFFFF;
}

/**
 * Initialize receive descriptor ring
 */
static bool e1000_init_rx(void) {
    kprintf("[e1000] Initializing receive ring (%d descriptors)\n", E1000_NUM_RX_DESC);

    /* Allocate descriptor ring (must be 16-byte aligned, 256 descriptors * 16 bytes = 4KB) */
    size_t desc_size = E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t);
    size_t desc_pages = (desc_size + PAGE_SIZE - 1) / PAGE_SIZE;

    e1000_dev.rx_descs_phys = pmm_alloc_pages(desc_pages);
    if (e1000_dev.rx_descs_phys == 0) {
        kprintf("[e1000] ERROR: Failed to allocate RX descriptor ring\n");
        return false;
    }

    /* Map descriptor ring to virtual memory */
    virtaddr_t rx_desc_virt = VMM_KERNEL_PHYS_MAP + e1000_dev.rx_descs_phys;
    e1000_dev.rx_descs = (e1000_rx_desc_t *)rx_desc_virt;

    /* Clear descriptors */
    memset(e1000_dev.rx_descs, 0, desc_size);

    /* Allocate receive buffers */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        physaddr_t buf_phys = pmm_alloc_page();
        if (buf_phys == 0) {
            kprintf("[e1000] ERROR: Failed to allocate RX buffer %d\n", i);
            return false;
        }

        e1000_dev.rx_buffers_phys[i] = buf_phys;
        e1000_dev.rx_buffers[i] = (void *)(VMM_KERNEL_PHYS_MAP + buf_phys);

        /* Set up descriptor */
        e1000_dev.rx_descs[i].buffer_addr = buf_phys;
        e1000_dev.rx_descs[i].status = 0;
    }

    e1000_dev.rx_cur = 0;

    /* Configure RX descriptor ring registers */
    e1000_write_reg(E1000_RDBAL, (uint32_t)(e1000_dev.rx_descs_phys & 0xFFFFFFFF));
    e1000_write_reg(E1000_RDBAH, (uint32_t)(e1000_dev.rx_descs_phys >> 32));
    e1000_write_reg(E1000_RDLEN, desc_size);
    e1000_write_reg(E1000_RDH, 0);
    e1000_write_reg(E1000_RDT, E1000_NUM_RX_DESC - 1);

    /* Configure receive control */
    uint32_t rctl = E1000_RCTL_EN |           /* Enable receiver */
                    E1000_RCTL_BAM |           /* Accept broadcast */
                    E1000_RCTL_BSIZE_2048 |    /* 2KB buffers */
                    E1000_RCTL_SECRC;          /* Strip CRC */

    e1000_write_reg(E1000_RCTL, rctl);

    kprintf("[e1000] RX ring initialized at phys 0x%x\n", (uint32_t)e1000_dev.rx_descs_phys);

    return true;
}

/**
 * Initialize transmit descriptor ring
 */
static bool e1000_init_tx(void) {
    kprintf("[e1000] Initializing transmit ring (%d descriptors)\n", E1000_NUM_TX_DESC);

    /* Allocate descriptor ring */
    size_t desc_size = E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t);
    size_t desc_pages = (desc_size + PAGE_SIZE - 1) / PAGE_SIZE;

    e1000_dev.tx_descs_phys = pmm_alloc_pages(desc_pages);
    if (e1000_dev.tx_descs_phys == 0) {
        kprintf("[e1000] ERROR: Failed to allocate TX descriptor ring\n");
        return false;
    }

    /* Map descriptor ring to virtual memory */
    virtaddr_t tx_desc_virt = VMM_KERNEL_PHYS_MAP + e1000_dev.tx_descs_phys;
    e1000_dev.tx_descs = (e1000_tx_desc_t *)tx_desc_virt;

    /* Clear descriptors */
    memset(e1000_dev.tx_descs, 0, desc_size);

    /* Allocate transmit buffers */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        physaddr_t buf_phys = pmm_alloc_page();
        if (buf_phys == 0) {
            kprintf("[e1000] ERROR: Failed to allocate TX buffer %d\n", i);
            return false;
        }

        e1000_dev.tx_buffers_phys[i] = buf_phys;
        e1000_dev.tx_buffers[i] = (void *)(VMM_KERNEL_PHYS_MAP + buf_phys);

        /* Set up descriptor */
        e1000_dev.tx_descs[i].buffer_addr = buf_phys;
        e1000_dev.tx_descs[i].status = E1000_TXD_STAT_DD;  /* Mark as done (available) */
        e1000_dev.tx_descs[i].cmd = 0;
    }

    e1000_dev.tx_cur = 0;

    /* Configure TX descriptor ring registers */
    e1000_write_reg(E1000_TDBAL, (uint32_t)(e1000_dev.tx_descs_phys & 0xFFFFFFFF));
    e1000_write_reg(E1000_TDBAH, (uint32_t)(e1000_dev.tx_descs_phys >> 32));
    e1000_write_reg(E1000_TDLEN, desc_size);
    e1000_write_reg(E1000_TDH, 0);
    e1000_write_reg(E1000_TDT, 0);

    /* Configure transmit control */
    uint32_t tctl = E1000_TCTL_EN |                           /* Enable transmitter */
                    E1000_TCTL_PSP |                          /* Pad short packets */
                    (15 << E1000_TCTL_CT_SHIFT) |             /* Collision threshold */
                    (64 << E1000_TCTL_COLD_SHIFT);            /* Collision distance */

    e1000_write_reg(E1000_TCTL, tctl);

    /* Configure Inter-Packet Gap */
    e1000_write_reg(E1000_TIPG, E1000_TIPG_DEFAULT);

    kprintf("[e1000] TX ring initialized at phys 0x%x\n", (uint32_t)e1000_dev.tx_descs_phys);

    return true;
}

/**
 * Enable e1000 interrupts
 */
static void e1000_enable_interrupts(void) {
    /* Clear any pending interrupts */
    e1000_read_reg(E1000_ICR);

    /* Enable desired interrupts */
    uint32_t ims = E1000_ICR_LSC |      /* Link status change */
                   E1000_ICR_RXT0 |     /* Receive timer */
                   E1000_ICR_RXO |      /* Receive overrun */
                   E1000_ICR_RXDMT0 |   /* RX descriptor min threshold */
                   E1000_ICR_TXDW;      /* TX descriptor written back */

    e1000_write_reg(E1000_IMS, ims);

    kprintf("[e1000] Interrupts enabled (IMS=0x%x)\n", ims);
}

/**
 * Initialize the e1000 driver
 */
bool e1000_init(void) {
    kprintf("[e1000] Initializing Intel e1000 network driver\n");

    /* Clear device state */
    memset(&e1000_dev, 0, sizeof(e1000_dev));

    /* Find e1000 on PCI bus */
    if (!e1000_detect_pci()) {
        return false;
    }

    /* Map MMIO region into kernel virtual address space */
    size_t mmio_pages = (e1000_dev.mmio_size + PAGE_SIZE - 1) / PAGE_SIZE;
    e1000_dev.mmio_base = VMM_KERNEL_PHYS_MAP + e1000_dev.mmio_phys;

    /* Map MMIO pages with no-cache flags */
    for (size_t i = 0; i < mmio_pages; i++) {
        physaddr_t phys = e1000_dev.mmio_phys + (i * PAGE_SIZE);
        virtaddr_t virt = e1000_dev.mmio_base + (i * PAGE_SIZE);
        vmm_map_page(virt, phys, VMM_FLAGS_MMIO);
    }

    kprintf("[e1000] MMIO mapped at virtual 0x%x\n", (uint32_t)e1000_dev.mmio_base);

    /* Reset the device */
    e1000_reset();

    /* Read MAC address */
    e1000_read_mac();

    /* Clear Multicast Table Array */
    for (int i = 0; i < 128; i++) {
        e1000_write_reg(E1000_MTA + (i * 4), 0);
    }

    /* Initialize descriptor rings */
    if (!e1000_init_rx()) {
        kprintf("[e1000] ERROR: Failed to initialize RX ring\n");
        return false;
    }

    if (!e1000_init_tx()) {
        kprintf("[e1000] ERROR: Failed to initialize TX ring\n");
        return false;
    }

    /* Register interrupt handler */
    idt_register_handler(IRQ_BASE + e1000_dev.irq, e1000_handler);
    kprintf("[e1000] Registered interrupt handler for IRQ %d (vector %d)\n",
            e1000_dev.irq, IRQ_BASE + e1000_dev.irq);

    /* Enable interrupts */
    e1000_enable_interrupts();

    /* Set link up */
    uint32_t ctrl = e1000_read_reg(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;  /* Set Link Up */
    e1000_write_reg(E1000_CTRL, ctrl);

    /* Check link status */
    uint32_t status = e1000_read_reg(E1000_STATUS);
    e1000_dev.link_up = (status & E1000_STATUS_LU) != 0;

    if (e1000_dev.link_up) {
        const char *speed = "Unknown";
        switch (status & E1000_STATUS_SPEED_MASK) {
            case E1000_STATUS_SPEED_10:   speed = "10 Mb/s"; break;
            case E1000_STATUS_SPEED_100:  speed = "100 Mb/s"; break;
            case E1000_STATUS_SPEED_1000: speed = "1000 Mb/s"; break;
        }
        kprintf("[e1000] Link UP at %s, %s duplex\n",
                speed, (status & E1000_STATUS_FD) ? "Full" : "Half");
    } else {
        kprintf("[e1000] Link DOWN\n");
    }

    e1000_dev.initialized = true;
    kprintf("[e1000] Driver initialized successfully\n");

    return true;
}

/**
 * Reset the e1000 device
 */
void e1000_reset(void) {
    kprintf("[e1000] Resetting device...\n");

    /* Disable interrupts */
    e1000_write_reg(E1000_IMC, 0xFFFFFFFF);

    /* Disable RX and TX */
    e1000_write_reg(E1000_RCTL, 0);
    e1000_write_reg(E1000_TCTL, 0);

    /* Issue device reset */
    uint32_t ctrl = e1000_read_reg(E1000_CTRL);
    e1000_write_reg(E1000_CTRL, ctrl | E1000_CTRL_RST);

    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        io_wait();
    }

    /* Clear pending interrupts */
    e1000_read_reg(E1000_ICR);

    kprintf("[e1000] Device reset complete\n");
}

/**
 * Send a network packet
 */
ssize_t e1000_send_packet(const void *data, size_t len) {
    if (!e1000_dev.initialized) {
        kprintf("[e1000] ERROR: Driver not initialized\n");
        return -1;
    }

    if (data == NULL || len == 0) {
        return -1;
    }

    if (len > E1000_MAX_PACKET_SIZE) {
        kprintf("[e1000] ERROR: Packet too large (%d > %d)\n", (int)len, E1000_MAX_PACKET_SIZE);
        return -1;
    }

    /* Get current TX descriptor */
    uint32_t cur = e1000_dev.tx_cur;
    e1000_tx_desc_t *desc = &e1000_dev.tx_descs[cur];

    /* Wait for descriptor to be available */
    int timeout = 10000;
    while (!(desc->status & E1000_TXD_STAT_DD)) {
        if (--timeout == 0) {
            kprintf("[e1000] ERROR: TX timeout waiting for descriptor\n");
            return -1;
        }
    }

    /* Copy packet data to TX buffer */
    memcpy(e1000_dev.tx_buffers[cur], data, len);

    /* Set up descriptor */
    desc->buffer_addr = e1000_dev.tx_buffers_phys[cur];
    desc->length = len;
    desc->cmd = E1000_TXD_CMD_EOP |    /* End of packet */
                E1000_TXD_CMD_IFCS |   /* Insert FCS/CRC */
                E1000_TXD_CMD_RS;      /* Report status */
    desc->status = 0;  /* Clear status */

    /* Advance tail pointer to trigger transmission */
    e1000_dev.tx_cur = (cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_TDT, e1000_dev.tx_cur);

    /* Update statistics */
    e1000_dev.packets_sent++;
    e1000_dev.bytes_sent += len;

    return len;
}

/**
 * Receive a network packet
 */
ssize_t e1000_receive_packet(void *buf, size_t max_len) {
    if (!e1000_dev.initialized) {
        return -1;
    }

    if (buf == NULL || max_len == 0) {
        return -1;
    }

    /* Get current RX descriptor */
    uint32_t cur = e1000_dev.rx_cur;
    e1000_rx_desc_t *desc = &e1000_dev.rx_descs[cur];

    /* Check if packet is available */
    if (!(desc->status & E1000_RXD_STAT_DD)) {
        return 0;  /* No packet available */
    }

    /* Check for errors */
    if (desc->errors) {
        kprintf("[e1000] RX error: 0x%02x\n", desc->errors);
        e1000_dev.errors++;

        /* Reset descriptor and continue */
        desc->status = 0;
        e1000_dev.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(E1000_RDT, cur);

        return -1;
    }

    /* Get packet length */
    uint16_t len = desc->length;
    if (len > max_len) {
        len = max_len;
    }

    /* Copy packet data */
    memcpy(buf, e1000_dev.rx_buffers[cur], len);

    /* Update statistics */
    e1000_dev.packets_received++;
    e1000_dev.bytes_received += len;

    /* Reset descriptor for reuse */
    desc->status = 0;

    /* Advance to next descriptor */
    uint32_t old_cur = cur;
    e1000_dev.rx_cur = (cur + 1) % E1000_NUM_RX_DESC;

    /* Update tail pointer to allow hardware to use this descriptor again */
    e1000_write_reg(E1000_RDT, old_cur);

    return len;
}

/**
 * Get the MAC address
 */
void e1000_get_mac(uint8_t mac[6]) {
    if (mac == NULL) {
        return;
    }

    for (int i = 0; i < 6; i++) {
        mac[i] = e1000_dev.mac[i];
    }
}

/**
 * e1000 interrupt handler
 */
void e1000_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Read interrupt cause to acknowledge */
    uint32_t icr = e1000_read_reg(E1000_ICR);

    if (icr == 0) {
        /* Spurious interrupt */
        return;
    }

    /* Handle link status change */
    if (icr & E1000_ICR_LSC) {
        uint32_t status = e1000_read_reg(E1000_STATUS);
        e1000_dev.link_up = (status & E1000_STATUS_LU) != 0;
        kprintf("[e1000] Link status changed: %s\n",
                e1000_dev.link_up ? "UP" : "DOWN");
    }

    /* Handle receive interrupt */
    if (icr & (E1000_ICR_RXT0 | E1000_ICR_RXDMT0)) {
        /* Packets are available - they will be handled by polling or a receive thread */
        /* For now, just log it */
        kprintf("[e1000] Receive interrupt (ICR=0x%x)\n", icr);
    }

    /* Handle receive overrun */
    if (icr & E1000_ICR_RXO) {
        kprintf("[e1000] WARNING: Receive overrun\n");
        e1000_dev.errors++;
    }

    /* Handle TX descriptor writeback */
    if (icr & E1000_ICR_TXDW) {
        /* Transmission complete - could wake up waiting threads */
    }

    /* Send EOI to PIC */
    if (e1000_dev.irq >= 8) {
        outb(0xA0, 0x20);  /* Slave PIC EOI */
    }
    outb(0x20, 0x20);  /* Master PIC EOI */
}

/**
 * Check if link is up
 */
bool e1000_link_up(void) {
    if (!e1000_dev.initialized) {
        return false;
    }

    uint32_t status = e1000_read_reg(E1000_STATUS);
    e1000_dev.link_up = (status & E1000_STATUS_LU) != 0;
    return e1000_dev.link_up;
}

/**
 * Get device statistics
 */
void e1000_get_stats(uint64_t *packets_sent, uint64_t *packets_received,
                     uint64_t *bytes_sent, uint64_t *bytes_received) {
    if (packets_sent) {
        *packets_sent = e1000_dev.packets_sent;
    }
    if (packets_received) {
        *packets_received = e1000_dev.packets_received;
    }
    if (bytes_sent) {
        *bytes_sent = e1000_dev.bytes_sent;
    }
    if (bytes_received) {
        *bytes_received = e1000_dev.bytes_received;
    }
}

/**
 * Enable/disable promiscuous mode
 */
void e1000_set_promiscuous(bool enable) {
    if (!e1000_dev.initialized) {
        return;
    }

    uint32_t rctl = e1000_read_reg(E1000_RCTL);

    if (enable) {
        rctl |= E1000_RCTL_UPE | E1000_RCTL_MPE;
        kprintf("[e1000] Promiscuous mode enabled\n");
    } else {
        rctl &= ~(E1000_RCTL_UPE | E1000_RCTL_MPE);
        kprintf("[e1000] Promiscuous mode disabled\n");
    }

    e1000_write_reg(E1000_RCTL, rctl);
}
