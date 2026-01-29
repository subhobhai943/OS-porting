/**
 * AAAos Network Driver - Intel e1000 Gigabit Ethernet Controller
 *
 * This driver supports the Intel 82540EM (e1000) network adapter,
 * which is QEMU's default emulated network card.
 *
 * PCI Vendor: 0x8086 (Intel)
 * PCI Device: 0x100E (82540EM)
 */

#ifndef _AAAOS_DRIVERS_E1000_H
#define _AAAOS_DRIVERS_E1000_H

#include "../../kernel/include/types.h"
#include "../../kernel/arch/x86_64/include/idt.h"

/* PCI identification */
#define E1000_VENDOR_ID         0x8086
#define E1000_DEVICE_ID         0x100E

/* PCI configuration ports */
#define PCI_CONFIG_ADDRESS      0xCF8
#define PCI_CONFIG_DATA         0xCFC

/* PCI configuration space offsets */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_INTERRUPT_LINE      0x3C

/* PCI command register bits */
#define PCI_CMD_IO              BIT(0)
#define PCI_CMD_MEMORY          BIT(1)
#define PCI_CMD_BUS_MASTER      BIT(2)
#define PCI_CMD_INT_DISABLE     BIT(10)

/* e1000 MMIO Register Offsets */
#define E1000_CTRL              0x0000      /* Device Control */
#define E1000_STATUS            0x0008      /* Device Status */
#define E1000_EECD              0x0010      /* EEPROM/Flash Control/Data */
#define E1000_EERD              0x0014      /* EEPROM Read */
#define E1000_CTRL_EXT          0x0018      /* Extended Device Control */
#define E1000_ICR               0x00C0      /* Interrupt Cause Read */
#define E1000_ITR               0x00C4      /* Interrupt Throttling Rate */
#define E1000_ICS               0x00C8      /* Interrupt Cause Set */
#define E1000_IMS               0x00D0      /* Interrupt Mask Set/Read */
#define E1000_IMC               0x00D8      /* Interrupt Mask Clear */

/* Receive Control */
#define E1000_RCTL              0x0100      /* Receive Control */
#define E1000_RDBAL             0x2800      /* RX Descriptor Base Low */
#define E1000_RDBAH             0x2804      /* RX Descriptor Base High */
#define E1000_RDLEN             0x2808      /* RX Descriptor Length */
#define E1000_RDH               0x2810      /* RX Descriptor Head */
#define E1000_RDT               0x2818      /* RX Descriptor Tail */
#define E1000_RDTR              0x2820      /* RX Delay Timer */

/* Transmit Control */
#define E1000_TCTL              0x0400      /* Transmit Control */
#define E1000_TIPG              0x0410      /* TX Inter-Packet Gap */
#define E1000_TDBAL             0x3800      /* TX Descriptor Base Low */
#define E1000_TDBAH             0x3804      /* TX Descriptor Base High */
#define E1000_TDLEN             0x3808      /* TX Descriptor Length */
#define E1000_TDH               0x3810      /* TX Descriptor Head */
#define E1000_TDT               0x3818      /* TX Descriptor Tail */

/* MAC Address */
#define E1000_RAL               0x5400      /* Receive Address Low */
#define E1000_RAH               0x5404      /* Receive Address High */

/* Multicast Table Array */
#define E1000_MTA               0x5200      /* Multicast Table Array (128 entries) */

/* Device Control Register (CTRL) bits */
#define E1000_CTRL_FD           BIT(0)      /* Full Duplex */
#define E1000_CTRL_LRST         BIT(3)      /* Link Reset */
#define E1000_CTRL_ASDE         BIT(5)      /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU          BIT(6)      /* Set Link Up */
#define E1000_CTRL_ILOS         BIT(7)      /* Invert Loss of Signal */
#define E1000_CTRL_RST          BIT(26)     /* Device Reset */
#define E1000_CTRL_VME          BIT(30)     /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST      BIT(31)     /* PHY Reset */

/* Device Status Register bits */
#define E1000_STATUS_FD         BIT(0)      /* Full Duplex */
#define E1000_STATUS_LU         BIT(1)      /* Link Up */
#define E1000_STATUS_TXOFF      BIT(4)      /* Transmission Paused */
#define E1000_STATUS_SPEED_MASK (3 << 6)    /* Link Speed */
#define E1000_STATUS_SPEED_10   (0 << 6)    /* 10 Mb/s */
#define E1000_STATUS_SPEED_100  (1 << 6)    /* 100 Mb/s */
#define E1000_STATUS_SPEED_1000 (2 << 6)    /* 1000 Mb/s */

/* Receive Control Register (RCTL) bits */
#define E1000_RCTL_EN           BIT(1)      /* Receiver Enable */
#define E1000_RCTL_SBP          BIT(2)      /* Store Bad Packets */
#define E1000_RCTL_UPE          BIT(3)      /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE          BIT(4)      /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE          BIT(5)      /* Long Packet Enable */
#define E1000_RCTL_LBM_MASK     (3 << 6)    /* Loopback Mode */
#define E1000_RCTL_LBM_NONE     (0 << 6)    /* No Loopback */
#define E1000_RCTL_RDMTS_HALF   (0 << 8)    /* RX Desc Min Threshold: 1/2 */
#define E1000_RCTL_RDMTS_QUAR   (1 << 8)    /* RX Desc Min Threshold: 1/4 */
#define E1000_RCTL_RDMTS_EIGHTH (2 << 8)    /* RX Desc Min Threshold: 1/8 */
#define E1000_RCTL_MO_SHIFT     12          /* Multicast Offset shift */
#define E1000_RCTL_BAM          BIT(15)     /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048   (0 << 16)   /* Buffer Size: 2048 */
#define E1000_RCTL_BSIZE_1024   (1 << 16)   /* Buffer Size: 1024 */
#define E1000_RCTL_BSIZE_512    (2 << 16)   /* Buffer Size: 512 */
#define E1000_RCTL_BSIZE_256    (3 << 16)   /* Buffer Size: 256 */
#define E1000_RCTL_BSIZE_16384  (1 << 16) | BIT(25)  /* Buffer Size: 16384 (BSEX) */
#define E1000_RCTL_VFE          BIT(18)     /* VLAN Filter Enable */
#define E1000_RCTL_CFIEN        BIT(19)     /* Canonical Form Indicator Enable */
#define E1000_RCTL_CFI          BIT(20)     /* Canonical Form Indicator */
#define E1000_RCTL_DPF          BIT(22)     /* Discard Pause Frames */
#define E1000_RCTL_PMCF         BIT(23)     /* Pass MAC Control Frames */
#define E1000_RCTL_BSEX         BIT(25)     /* Buffer Size Extension */
#define E1000_RCTL_SECRC        BIT(26)     /* Strip Ethernet CRC */

/* Transmit Control Register (TCTL) bits */
#define E1000_TCTL_EN           BIT(1)      /* Transmit Enable */
#define E1000_TCTL_PSP          BIT(3)      /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT     4           /* Collision Threshold shift */
#define E1000_TCTL_CT_MASK      (0xFF << 4) /* Collision Threshold mask */
#define E1000_TCTL_COLD_SHIFT   12          /* Collision Distance shift */
#define E1000_TCTL_COLD_MASK    (0x3FF << 12)   /* Collision Distance mask */
#define E1000_TCTL_SWXOFF       BIT(22)     /* Software XOFF */
#define E1000_TCTL_RTLC         BIT(24)     /* Retransmit on Late Collision */

/* Transmit IPG values (for Gigabit) */
#define E1000_TIPG_IPGT_SHIFT   0
#define E1000_TIPG_IPGR1_SHIFT  10
#define E1000_TIPG_IPGR2_SHIFT  20
#define E1000_TIPG_DEFAULT      ((10 << E1000_TIPG_IPGT_SHIFT) | \
                                 (8 << E1000_TIPG_IPGR1_SHIFT) | \
                                 (6 << E1000_TIPG_IPGR2_SHIFT))

/* Interrupt Cause bits */
#define E1000_ICR_TXDW          BIT(0)      /* TX Descriptor Written Back */
#define E1000_ICR_TXQE          BIT(1)      /* TX Queue Empty */
#define E1000_ICR_LSC           BIT(2)      /* Link Status Change */
#define E1000_ICR_RXSEQ         BIT(3)      /* RX Sequence Error */
#define E1000_ICR_RXDMT0        BIT(4)      /* RX Descriptor Min Threshold */
#define E1000_ICR_RXO           BIT(6)      /* RX Overrun */
#define E1000_ICR_RXT0          BIT(7)      /* RX Timer Interrupt */

/* EEPROM Read Register bits */
#define E1000_EERD_START        BIT(0)      /* Start Read */
#define E1000_EERD_DONE         BIT(4)      /* Read Done */
#define E1000_EERD_ADDR_SHIFT   8           /* Address shift */
#define E1000_EERD_DATA_SHIFT   16          /* Data shift */

/* Receive Address High Register bits */
#define E1000_RAH_AV            BIT(31)     /* Address Valid */

/* Descriptor ring sizes */
#define E1000_NUM_RX_DESC       256
#define E1000_NUM_TX_DESC       256

/* Buffer sizes */
#define E1000_RX_BUFFER_SIZE    2048
#define E1000_TX_BUFFER_SIZE    2048
#define E1000_MAX_PACKET_SIZE   1518        /* Ethernet MTU + headers */

/**
 * Receive Descriptor (legacy format)
 * 16 bytes, must be 16-byte aligned
 */
typedef struct PACKED {
    uint64_t buffer_addr;       /* Physical address of receive buffer */
    uint16_t length;            /* Length of received data */
    uint16_t checksum;          /* Packet checksum */
    uint8_t  status;            /* Descriptor status */
    uint8_t  errors;            /* Descriptor errors */
    uint16_t special;           /* VLAN tag */
} e1000_rx_desc_t;

/* RX Descriptor Status bits */
#define E1000_RXD_STAT_DD       BIT(0)      /* Descriptor Done */
#define E1000_RXD_STAT_EOP      BIT(1)      /* End of Packet */
#define E1000_RXD_STAT_IXSM     BIT(2)      /* Ignore Checksum */
#define E1000_RXD_STAT_VP       BIT(3)      /* VLAN Packet */
#define E1000_RXD_STAT_TCPCS    BIT(5)      /* TCP Checksum Calculated */
#define E1000_RXD_STAT_IPCS     BIT(6)      /* IP Checksum Calculated */
#define E1000_RXD_STAT_PIF      BIT(7)      /* Passed In-exact Filter */

/* RX Descriptor Error bits */
#define E1000_RXD_ERR_CE        BIT(0)      /* CRC Error */
#define E1000_RXD_ERR_SE        BIT(1)      /* Symbol Error */
#define E1000_RXD_ERR_SEQ       BIT(2)      /* Sequence Error */
#define E1000_RXD_ERR_CXE       BIT(4)      /* Carrier Extension Error */
#define E1000_RXD_ERR_TCPE      BIT(5)      /* TCP/UDP Checksum Error */
#define E1000_RXD_ERR_IPE       BIT(6)      /* IP Checksum Error */
#define E1000_RXD_ERR_RXE       BIT(7)      /* RX Data Error */

/**
 * Transmit Descriptor (legacy format)
 * 16 bytes, must be 16-byte aligned
 */
typedef struct PACKED {
    uint64_t buffer_addr;       /* Physical address of transmit buffer */
    uint16_t length;            /* Length of data to transmit */
    uint8_t  cso;               /* Checksum Offset */
    uint8_t  cmd;               /* Command field */
    uint8_t  status;            /* Descriptor status */
    uint8_t  css;               /* Checksum Start */
    uint16_t special;           /* VLAN tag */
} e1000_tx_desc_t;

/* TX Descriptor Command bits */
#define E1000_TXD_CMD_EOP       BIT(0)      /* End of Packet */
#define E1000_TXD_CMD_IFCS      BIT(1)      /* Insert FCS/CRC */
#define E1000_TXD_CMD_IC        BIT(2)      /* Insert Checksum */
#define E1000_TXD_CMD_RS        BIT(3)      /* Report Status */
#define E1000_TXD_CMD_RPS       BIT(4)      /* Report Packet Sent */
#define E1000_TXD_CMD_DEXT      BIT(5)      /* Descriptor Extension */
#define E1000_TXD_CMD_VLE       BIT(6)      /* VLAN Enable */
#define E1000_TXD_CMD_IDE       BIT(7)      /* Interrupt Delay Enable */

/* TX Descriptor Status bits */
#define E1000_TXD_STAT_DD       BIT(0)      /* Descriptor Done */
#define E1000_TXD_STAT_EC       BIT(1)      /* Excess Collisions */
#define E1000_TXD_STAT_LC       BIT(2)      /* Late Collision */
#define E1000_TXD_STAT_TU       BIT(3)      /* Transmit Underrun */

/**
 * e1000 device state structure
 */
typedef struct {
    /* PCI information */
    uint8_t  pci_bus;           /* PCI bus number */
    uint8_t  pci_device;        /* PCI device number */
    uint8_t  pci_function;      /* PCI function number */
    uint8_t  irq;               /* Interrupt line */

    /* MMIO access */
    virtaddr_t mmio_base;       /* MMIO base virtual address */
    physaddr_t mmio_phys;       /* MMIO base physical address */
    size_t     mmio_size;       /* MMIO region size */

    /* MAC address */
    uint8_t  mac[6];            /* Hardware MAC address */

    /* Receive ring */
    e1000_rx_desc_t *rx_descs;  /* RX descriptor ring */
    physaddr_t rx_descs_phys;   /* Physical address of RX descriptors */
    void     *rx_buffers[E1000_NUM_RX_DESC];  /* RX buffer pointers */
    physaddr_t rx_buffers_phys[E1000_NUM_RX_DESC]; /* Physical addresses */
    uint32_t   rx_cur;          /* Current RX descriptor index */

    /* Transmit ring */
    e1000_tx_desc_t *tx_descs;  /* TX descriptor ring */
    physaddr_t tx_descs_phys;   /* Physical address of TX descriptors */
    void     *tx_buffers[E1000_NUM_TX_DESC];  /* TX buffer pointers */
    physaddr_t tx_buffers_phys[E1000_NUM_TX_DESC]; /* Physical addresses */
    uint32_t   tx_cur;          /* Current TX descriptor index */

    /* Statistics */
    uint64_t   packets_sent;    /* Total packets sent */
    uint64_t   packets_received; /* Total packets received */
    uint64_t   bytes_sent;      /* Total bytes sent */
    uint64_t   bytes_received;  /* Total bytes received */
    uint64_t   errors;          /* Total errors */

    /* State flags */
    bool       initialized;     /* Driver initialized flag */
    bool       link_up;         /* Link status */
} e1000_device_t;

/**
 * Initialize the e1000 network driver
 * Scans PCI bus for e1000 device, sets up descriptor rings,
 * configures interrupts, and enables the device.
 *
 * @return true on success, false if no e1000 device found
 */
bool e1000_init(void);

/**
 * Send a network packet
 *
 * @param data Pointer to packet data
 * @param len Length of packet in bytes
 * @return Number of bytes sent, or -1 on error
 */
ssize_t e1000_send_packet(const void *data, size_t len);

/**
 * Receive a network packet
 *
 * @param buf Buffer to store received packet
 * @param max_len Maximum buffer size
 * @return Number of bytes received, 0 if no packet available, or -1 on error
 */
ssize_t e1000_receive_packet(void *buf, size_t max_len);

/**
 * Get the MAC address of the e1000 device
 *
 * @param mac Buffer to store 6-byte MAC address
 */
void e1000_get_mac(uint8_t mac[6]);

/**
 * e1000 interrupt handler
 * Called when the e1000 generates an interrupt (packet received, etc.)
 *
 * @param frame Interrupt frame
 */
void e1000_handler(interrupt_frame_t *frame);

/**
 * Check if link is up
 *
 * @return true if link is up, false otherwise
 */
bool e1000_link_up(void);

/**
 * Get device statistics
 *
 * @param packets_sent Output: total packets sent
 * @param packets_received Output: total packets received
 * @param bytes_sent Output: total bytes sent
 * @param bytes_received Output: total bytes received
 */
void e1000_get_stats(uint64_t *packets_sent, uint64_t *packets_received,
                     uint64_t *bytes_sent, uint64_t *bytes_received);

/**
 * Reset the e1000 device
 */
void e1000_reset(void);

/**
 * Enable/disable promiscuous mode
 *
 * @param enable true to enable, false to disable
 */
void e1000_set_promiscuous(bool enable);

#endif /* _AAAOS_DRIVERS_E1000_H */
