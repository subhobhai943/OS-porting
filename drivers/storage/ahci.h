/**
 * AAAos Kernel - AHCI (Advanced Host Controller Interface) Driver
 *
 * Provides SATA storage access via AHCI controllers.
 * Implements the AHCI 1.3 specification for detecting and
 * communicating with SATA drives.
 *
 * AHCI uses MMIO for all communication. The HBA (Host Bus Adapter)
 * base address comes from PCI BAR5.
 */

#ifndef _AAAOS_AHCI_H
#define _AAAOS_AHCI_H

#include "../../kernel/include/types.h"
#include "../pci/pci.h"

/* AHCI constants */
#define AHCI_MAX_PORTS          32      /* Maximum ports per HBA */
#define AHCI_MAX_CMD_SLOTS      32      /* Maximum command slots per port */
#define AHCI_SECTOR_SIZE        512     /* Standard sector size */

/* AHCI Programming Interface for SATA (class 0x01, subclass 0x06) */
#define AHCI_PROG_IF            0x01    /* AHCI mode */

/* Device signatures (from port SIG register) */
#define AHCI_SIG_ATA            0x00000101  /* SATA drive */
#define AHCI_SIG_ATAPI          0xEB140101  /* SATAPI device */
#define AHCI_SIG_SEMB           0xC33C0101  /* Enclosure management bridge */
#define AHCI_SIG_PM             0x96690101  /* Port multiplier */

/* Port status (SSTS - SStatus) DET field values */
#define AHCI_PORT_DET_NONE      0x0     /* No device, no PHY */
#define AHCI_PORT_DET_PRESENT   0x1     /* Device present, no PHY comm */
#define AHCI_PORT_DET_PHY       0x3     /* Device present, PHY established */
#define AHCI_PORT_DET_OFFLINE   0x4     /* PHY offline mode */

/* Port status (SSTS) IPM field values (Interface Power Management) */
#define AHCI_PORT_IPM_NONE      0x0     /* No device */
#define AHCI_PORT_IPM_ACTIVE    0x1     /* Interface active */
#define AHCI_PORT_IPM_PARTIAL   0x2     /* Partial power state */
#define AHCI_PORT_IPM_SLUMBER   0x6     /* Slumber power state */
#define AHCI_PORT_IPM_DEVSLEEP  0x8     /* DevSleep power state */

/* GHC (Global HBA Control) register bits */
#define AHCI_GHC_HR             (1 << 0)    /* HBA Reset */
#define AHCI_GHC_IE             (1 << 1)    /* Interrupt Enable */
#define AHCI_GHC_MRSM           (1 << 2)    /* MSI Revert to Single Message */
#define AHCI_GHC_AE             (1 << 31)   /* AHCI Enable */

/* CAP (Capabilities) register bits */
#define AHCI_CAP_S64A           (1 << 31)   /* 64-bit addressing */
#define AHCI_CAP_SNCQ           (1 << 30)   /* Native Command Queuing */
#define AHCI_CAP_SSNTF          (1 << 29)   /* SNotification */
#define AHCI_CAP_SMPS           (1 << 28)   /* Mechanical Presence Switch */
#define AHCI_CAP_SSS            (1 << 27)   /* Staggered Spin-up */
#define AHCI_CAP_SALP           (1 << 26)   /* Aggressive Link Power Mgmt */
#define AHCI_CAP_SAL            (1 << 25)   /* Activity LED */
#define AHCI_CAP_SCLO           (1 << 24)   /* Command List Override */
#define AHCI_CAP_NCS_MASK       0x1F00      /* Number of Command Slots */
#define AHCI_CAP_NCS_SHIFT      8
#define AHCI_CAP_NP_MASK        0x1F        /* Number of Ports */

/* Port Command (CMD) register bits */
#define AHCI_PORT_CMD_ST        (1 << 0)    /* Start */
#define AHCI_PORT_CMD_SUD       (1 << 1)    /* Spin-Up Device */
#define AHCI_PORT_CMD_POD       (1 << 2)    /* Power On Device */
#define AHCI_PORT_CMD_CLO       (1 << 3)    /* Command List Override */
#define AHCI_PORT_CMD_FRE       (1 << 4)    /* FIS Receive Enable */
#define AHCI_PORT_CMD_CCS_MASK  (0x1F << 8) /* Current Command Slot */
#define AHCI_PORT_CMD_MPSS      (1 << 13)   /* Mechanical Presence Switch State */
#define AHCI_PORT_CMD_FR        (1 << 14)   /* FIS Receive Running */
#define AHCI_PORT_CMD_CR        (1 << 15)   /* Command List Running */
#define AHCI_PORT_CMD_CPS       (1 << 16)   /* Cold Presence State */
#define AHCI_PORT_CMD_PMA       (1 << 17)   /* Port Multiplier Attached */
#define AHCI_PORT_CMD_HPCP      (1 << 18)   /* Hot Plug Capable Port */
#define AHCI_PORT_CMD_MPSP      (1 << 19)   /* Mechanical Presence Switch Port */
#define AHCI_PORT_CMD_CPD       (1 << 20)   /* Cold Presence Detection */
#define AHCI_PORT_CMD_ESP       (1 << 21)   /* External SATA Port */
#define AHCI_PORT_CMD_FBSCP     (1 << 22)   /* FIS-based Switching Cap Port */
#define AHCI_PORT_CMD_APSTE     (1 << 23)   /* Auto Partial to Slumber Trans */
#define AHCI_PORT_CMD_ATAPI     (1 << 24)   /* Device is ATAPI */
#define AHCI_PORT_CMD_DLAE      (1 << 25)   /* Drive LED on ATAPI Enable */
#define AHCI_PORT_CMD_ALPE      (1 << 26)   /* Aggressive Link Power Mgmt En */
#define AHCI_PORT_CMD_ASP       (1 << 27)   /* Aggressive Slumber/Partial */
#define AHCI_PORT_CMD_ICC_MASK  (0xF << 28) /* Interface Communication Ctrl */

/* Port Task File Data (TFD) register bits */
#define AHCI_PORT_TFD_ERR       (1 << 0)    /* Error */
#define AHCI_PORT_TFD_DRQ       (1 << 3)    /* Data Request */
#define AHCI_PORT_TFD_BSY       (1 << 7)    /* Busy */

/* Port Interrupt Status (IS) and Enable (IE) bits */
#define AHCI_PORT_INT_DHRS      (1 << 0)    /* Device to Host Register FIS */
#define AHCI_PORT_INT_PSS       (1 << 1)    /* PIO Setup FIS */
#define AHCI_PORT_INT_DSS       (1 << 2)    /* DMA Setup FIS */
#define AHCI_PORT_INT_SDBS      (1 << 3)    /* Set Device Bits */
#define AHCI_PORT_INT_UFS       (1 << 4)    /* Unknown FIS */
#define AHCI_PORT_INT_DPS       (1 << 5)    /* Descriptor Processed */
#define AHCI_PORT_INT_PCS       (1 << 6)    /* Port Connect Change */
#define AHCI_PORT_INT_DMPS      (1 << 7)    /* Device Mechanical Presence */
#define AHCI_PORT_INT_PRCS      (1 << 22)   /* PhyRdy Change */
#define AHCI_PORT_INT_IPMS      (1 << 23)   /* Incorrect Port Multiplier */
#define AHCI_PORT_INT_OFS       (1 << 24)   /* Overflow */
#define AHCI_PORT_INT_INFS      (1 << 26)   /* Interface Non-Fatal Error */
#define AHCI_PORT_INT_IFS       (1 << 27)   /* Interface Fatal Error */
#define AHCI_PORT_INT_HBDS      (1 << 28)   /* Host Bus Data Error */
#define AHCI_PORT_INT_HBFS      (1 << 29)   /* Host Bus Fatal Error */
#define AHCI_PORT_INT_TFES      (1 << 30)   /* Task File Error */
#define AHCI_PORT_INT_CPDS      (1U << 31)  /* Cold Port Detect */

/* FIS Types */
#define FIS_TYPE_REG_H2D        0x27    /* Register FIS - host to device */
#define FIS_TYPE_REG_D2H        0x34    /* Register FIS - device to host */
#define FIS_TYPE_DMA_ACT        0x39    /* DMA activate FIS */
#define FIS_TYPE_DMA_SETUP      0x41    /* DMA setup FIS */
#define FIS_TYPE_DATA           0x46    /* Data FIS */
#define FIS_TYPE_BIST           0x58    /* BIST activate FIS */
#define FIS_TYPE_PIO_SETUP      0x5F    /* PIO setup FIS */
#define FIS_TYPE_DEV_BITS       0xA1    /* Set device bits FIS */

/* ATA Commands */
#define ATA_CMD_READ_DMA_EXT    0x25    /* Read DMA Extended (48-bit LBA) */
#define ATA_CMD_WRITE_DMA_EXT   0x35    /* Write DMA Extended (48-bit LBA) */
#define ATA_CMD_IDENTIFY        0xEC    /* Identify Device */
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* Identify Packet Device (ATAPI) */
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA    /* Flush Cache Extended */

/* ATA device types */
typedef enum {
    AHCI_DEV_NULL = 0,              /* No device */
    AHCI_DEV_SATA,                  /* SATA drive */
    AHCI_DEV_SATAPI,                /* SATAPI device (CD/DVD) */
    AHCI_DEV_SEMB,                  /* Enclosure management bridge */
    AHCI_DEV_PM                     /* Port multiplier */
} ahci_device_type_t;

/* ============================================================================
 * AHCI Memory-Mapped Structures (AHCI 1.3 Specification)
 * ============================================================================ */

/**
 * HBA Port Registers (one per port, 0x80 bytes each)
 * Offset from ABAR: 0x100 + (port * 0x80)
 */
typedef volatile struct PACKED {
    uint32_t clb;           /* 0x00: Command List Base Address (low) */
    uint32_t clbu;          /* 0x04: Command List Base Address (high) */
    uint32_t fb;            /* 0x08: FIS Base Address (low) */
    uint32_t fbu;           /* 0x0C: FIS Base Address (high) */
    uint32_t is;            /* 0x10: Interrupt Status */
    uint32_t ie;            /* 0x14: Interrupt Enable */
    uint32_t cmd;           /* 0x18: Command and Status */
    uint32_t rsv0;          /* 0x1C: Reserved */
    uint32_t tfd;           /* 0x20: Task File Data */
    uint32_t sig;           /* 0x24: Signature */
    uint32_t ssts;          /* 0x28: SATA Status (SCR0: SStatus) */
    uint32_t sctl;          /* 0x2C: SATA Control (SCR2: SControl) */
    uint32_t serr;          /* 0x30: SATA Error (SCR1: SError) */
    uint32_t sact;          /* 0x34: SATA Active (SCR3: SActive) */
    uint32_t ci;            /* 0x38: Command Issue */
    uint32_t sntf;          /* 0x3C: SATA Notification (SCR4: SNotification) */
    uint32_t fbs;           /* 0x40: FIS-based Switching Control */
    uint32_t devslp;        /* 0x44: Device Sleep */
    uint32_t rsv1[10];      /* 0x48-0x6F: Reserved */
    uint32_t vendor[4];     /* 0x70-0x7F: Vendor Specific */
} ahci_port_t;

/**
 * HBA Memory Registers (Generic Host Control)
 * Located at ABAR (BAR5 of AHCI controller)
 */
typedef volatile struct PACKED {
    /* Generic Host Control (0x00-0x2B) */
    uint32_t cap;           /* 0x00: Host Capabilities */
    uint32_t ghc;           /* 0x04: Global Host Control */
    uint32_t is;            /* 0x08: Interrupt Status */
    uint32_t pi;            /* 0x0C: Ports Implemented */
    uint32_t vs;            /* 0x10: Version */
    uint32_t ccc_ctl;       /* 0x14: Command Completion Coalescing Control */
    uint32_t ccc_ports;     /* 0x18: Command Completion Coalescing Ports */
    uint32_t em_loc;        /* 0x1C: Enclosure Management Location */
    uint32_t em_ctl;        /* 0x20: Enclosure Management Control */
    uint32_t cap2;          /* 0x24: Host Capabilities Extended */
    uint32_t bohc;          /* 0x28: BIOS/OS Handoff Control and Status */

    /* Reserved (0x2C-0x9F) */
    uint8_t rsv[0xA0 - 0x2C];

    /* Vendor Specific (0xA0-0xFF) */
    uint8_t vendor[0x100 - 0xA0];

    /* Port Registers (0x100+) */
    ahci_port_t ports[AHCI_MAX_PORTS];
} ahci_hba_t;

/**
 * Command Header (one per command slot, 32 bytes)
 * Command List has 32 command headers
 */
typedef struct PACKED {
    /* DW0 */
    uint8_t cfl : 5;        /* Command FIS Length (in DWORDs) */
    uint8_t a : 1;          /* ATAPI */
    uint8_t w : 1;          /* Write (1 = H2D, 0 = D2H) */
    uint8_t p : 1;          /* Prefetchable */

    uint8_t r : 1;          /* Reset */
    uint8_t b : 1;          /* BIST */
    uint8_t c : 1;          /* Clear Busy upon R_OK */
    uint8_t rsv0 : 1;       /* Reserved */
    uint8_t pmp : 4;        /* Port Multiplier Port */

    uint16_t prdtl;         /* Physical Region Descriptor Table Length */

    /* DW1 */
    uint32_t prdbc;         /* PRD Byte Count (transferred) */

    /* DW2-3 */
    uint32_t ctba;          /* Command Table Base Address (low) */
    uint32_t ctbau;         /* Command Table Base Address (high) */

    /* DW4-7: Reserved */
    uint32_t rsv1[4];
} ahci_cmd_header_t;

/**
 * Physical Region Descriptor (PRD) Entry
 * Each PRD describes a contiguous physical memory region
 */
typedef struct PACKED {
    uint32_t dba;           /* Data Base Address (low) */
    uint32_t dbau;          /* Data Base Address (high) */
    uint32_t rsv0;          /* Reserved */
    uint32_t dbc : 22;      /* Data Byte Count (0-based, max 4MB) */
    uint32_t rsv1 : 9;      /* Reserved */
    uint32_t i : 1;         /* Interrupt on Completion */
} ahci_prdt_entry_t;

/**
 * FIS - Register Host to Device (27 bytes)
 * Used for sending commands to the device
 */
typedef struct PACKED {
    /* DW0 */
    uint8_t fis_type;       /* FIS_TYPE_REG_H2D */
    uint8_t pmport : 4;     /* Port multiplier */
    uint8_t rsv0 : 3;       /* Reserved */
    uint8_t c : 1;          /* 1 = Command, 0 = Control */
    uint8_t command;        /* Command register */
    uint8_t featurel;       /* Feature register (low) */

    /* DW1 */
    uint8_t lba0;           /* LBA low register, 7:0 */
    uint8_t lba1;           /* LBA mid register, 15:8 */
    uint8_t lba2;           /* LBA high register, 23:16 */
    uint8_t device;         /* Device register */

    /* DW2 */
    uint8_t lba3;           /* LBA register, 31:24 */
    uint8_t lba4;           /* LBA register, 39:32 */
    uint8_t lba5;           /* LBA register, 47:40 */
    uint8_t featureh;       /* Feature register (high) */

    /* DW3 */
    uint8_t countl;         /* Count register (low) */
    uint8_t counth;         /* Count register (high) */
    uint8_t icc;            /* Isochronous command completion */
    uint8_t control;        /* Control register */

    /* DW4 */
    uint8_t rsv1[4];        /* Reserved */
} ahci_fis_reg_h2d_t;

/**
 * FIS - Register Device to Host (20 bytes)
 * Device response to a command
 */
typedef struct PACKED {
    /* DW0 */
    uint8_t fis_type;       /* FIS_TYPE_REG_D2H */
    uint8_t pmport : 4;     /* Port multiplier */
    uint8_t rsv0 : 2;       /* Reserved */
    uint8_t i : 1;          /* Interrupt bit */
    uint8_t rsv1 : 1;       /* Reserved */
    uint8_t status;         /* Status register */
    uint8_t error;          /* Error register */

    /* DW1 */
    uint8_t lba0;           /* LBA low register, 7:0 */
    uint8_t lba1;           /* LBA mid register, 15:8 */
    uint8_t lba2;           /* LBA high register, 23:16 */
    uint8_t device;         /* Device register */

    /* DW2 */
    uint8_t lba3;           /* LBA register, 31:24 */
    uint8_t lba4;           /* LBA register, 39:32 */
    uint8_t lba5;           /* LBA register, 47:40 */
    uint8_t rsv2;           /* Reserved */

    /* DW3 */
    uint8_t countl;         /* Count register (low) */
    uint8_t counth;         /* Count register (high) */
    uint8_t rsv3[2];        /* Reserved */

    /* DW4 */
    uint8_t rsv4[4];        /* Reserved */
} ahci_fis_reg_d2h_t;

/**
 * FIS - Data (variable size)
 * Used for data payload transfer
 */
typedef struct PACKED {
    uint8_t fis_type;       /* FIS_TYPE_DATA */
    uint8_t pmport : 4;     /* Port multiplier */
    uint8_t rsv0 : 4;       /* Reserved */
    uint8_t rsv1[2];        /* Reserved */
    /* Data payload follows */
} ahci_fis_data_t;

/**
 * FIS - PIO Setup (20 bytes)
 * PIO data transfer setup
 */
typedef struct PACKED {
    /* DW0 */
    uint8_t fis_type;       /* FIS_TYPE_PIO_SETUP */
    uint8_t pmport : 4;     /* Port multiplier */
    uint8_t rsv0 : 1;       /* Reserved */
    uint8_t d : 1;          /* Data transfer direction, 1 = D2H */
    uint8_t i : 1;          /* Interrupt bit */
    uint8_t rsv1 : 1;       /* Reserved */
    uint8_t status;         /* Status register */
    uint8_t error;          /* Error register */

    /* DW1 */
    uint8_t lba0;           /* LBA low register, 7:0 */
    uint8_t lba1;           /* LBA mid register, 15:8 */
    uint8_t lba2;           /* LBA high register, 23:16 */
    uint8_t device;         /* Device register */

    /* DW2 */
    uint8_t lba3;           /* LBA register, 31:24 */
    uint8_t lba4;           /* LBA register, 39:32 */
    uint8_t lba5;           /* LBA register, 47:40 */
    uint8_t rsv2;           /* Reserved */

    /* DW3 */
    uint8_t countl;         /* Count register (low) */
    uint8_t counth;         /* Count register (high) */
    uint8_t rsv3;           /* Reserved */
    uint8_t e_status;       /* New value of status register */

    /* DW4 */
    uint16_t tc;            /* Transfer count */
    uint8_t rsv4[2];        /* Reserved */
} ahci_fis_pio_setup_t;

/**
 * FIS - DMA Setup (28 bytes)
 * First-party DMA setup
 */
typedef struct PACKED {
    /* DW0 */
    uint8_t fis_type;       /* FIS_TYPE_DMA_SETUP */
    uint8_t pmport : 4;     /* Port multiplier */
    uint8_t rsv0 : 1;       /* Reserved */
    uint8_t d : 1;          /* Data transfer direction, 1 = D2H */
    uint8_t i : 1;          /* Interrupt bit */
    uint8_t a : 1;          /* Auto-activate */
    uint8_t rsv1[2];        /* Reserved */

    /* DW1-2 */
    uint64_t dma_buffer_id; /* DMA Buffer Identifier */

    /* DW3 */
    uint32_t rsv2;          /* Reserved */

    /* DW4 */
    uint32_t dma_buf_offset;/* Byte offset into buffer */

    /* DW5 */
    uint32_t transfer_count;/* Number of bytes to transfer */

    /* DW6 */
    uint32_t rsv3;          /* Reserved */
} ahci_fis_dma_setup_t;

/**
 * Received FIS structure (256 bytes, 256-byte aligned)
 * Area where device sends FIS responses
 */
typedef struct PACKED ALIGNED(256) {
    ahci_fis_dma_setup_t dsfis;     /* 0x00: DMA Setup FIS */
    uint8_t pad0[4];

    ahci_fis_pio_setup_t psfis;     /* 0x20: PIO Setup FIS */
    uint8_t pad1[12];

    ahci_fis_reg_d2h_t rfis;        /* 0x40: Register - Device to Host FIS */
    uint8_t pad2[4];

    uint8_t sdbfis[8];              /* 0x58: Set Device Bit FIS */

    uint8_t ufis[64];               /* 0x60: Unknown FIS */

    uint8_t rsv[0x100 - 0xA0];      /* 0xA0: Reserved */
} ahci_fis_t;

/**
 * Command Table (variable size, 128-byte aligned)
 * Contains the command FIS and PRDT entries
 */
typedef struct PACKED ALIGNED(128) {
    uint8_t cfis[64];               /* Command FIS */
    uint8_t acmd[16];               /* ATAPI command (12 or 16 bytes) */
    uint8_t rsv[48];                /* Reserved */
    ahci_prdt_entry_t prdt_entry[]; /* Physical Region Descriptor Table */
} ahci_cmd_table_t;

/* Maximum PRD entries per command (can be up to 65535) */
#define AHCI_MAX_PRDT_ENTRIES   8

/**
 * Per-port driver state
 */
typedef struct {
    ahci_device_type_t type;        /* Device type */
    bool present;                   /* Device is present */
    ahci_cmd_header_t* cmd_list;    /* Command list (32 entries) */
    ahci_fis_t* fis_area;           /* FIS receive area */
    ahci_cmd_table_t* cmd_tables[AHCI_MAX_CMD_SLOTS]; /* Command tables */
    uint64_t sector_count;          /* Total sectors (from IDENTIFY) */
    char model[41];                 /* Model string (from IDENTIFY) */
    char serial[21];                /* Serial number (from IDENTIFY) */
} ahci_port_info_t;

/**
 * AHCI driver state
 */
typedef struct {
    pci_device_t* pci_dev;          /* PCI device */
    ahci_hba_t* hba;                /* HBA memory mapped registers */
    uint32_t ports_impl;            /* Bitmap of implemented ports */
    uint32_t num_ports;             /* Number of implemented ports */
    uint32_t num_cmd_slots;         /* Number of command slots */
    bool supports_64bit;            /* 64-bit DMA addressing */
    ahci_port_info_t port_info[AHCI_MAX_PORTS]; /* Per-port info */
} ahci_controller_t;

/* ============================================================================
 * AHCI Driver API
 * ============================================================================ */

/**
 * Initialize the AHCI driver
 * Scans PCI for AHCI controllers and initializes them
 * @return Number of AHCI controllers found and initialized
 */
int ahci_init(void);

/**
 * Probe a port to check if a device is connected
 * @param port Port number (0-31)
 * @return Device type (AHCI_DEV_NULL if no device)
 */
ahci_device_type_t ahci_probe_port(int port);

/**
 * Get information about a connected device
 * @param port Port number
 * @return Pointer to port info, or NULL if invalid/not present
 */
ahci_port_info_t* ahci_get_port_info(int port);

/**
 * Read sectors from a SATA drive
 * @param port Port number
 * @param lba Starting Logical Block Address
 * @param count Number of sectors to read (max 65535)
 * @param buf Buffer to store data (must be sector-aligned)
 * @return 0 on success, negative error code on failure
 */
int ahci_read_sectors(int port, uint64_t lba, uint32_t count, void* buf);

/**
 * Write sectors to a SATA drive
 * @param port Port number
 * @param lba Starting Logical Block Address
 * @param count Number of sectors to write (max 65535)
 * @param buf Buffer containing data to write
 * @return 0 on success, negative error code on failure
 */
int ahci_write_sectors(int port, uint64_t lba, uint32_t count, const void* buf);

/**
 * Get drive identification data (ATA IDENTIFY command)
 * @param port Port number
 * @param buf Buffer to store 512 bytes of identify data
 * @return 0 on success, negative error code on failure
 */
int ahci_identify(int port, void* buf);

/**
 * Flush the drive's write cache
 * @param port Port number
 * @return 0 on success, negative error code on failure
 */
int ahci_flush(int port);

/**
 * Get number of initialized AHCI controllers
 * @return Number of controllers
 */
int ahci_get_controller_count(void);

/**
 * Get AHCI controller by index
 * @param index Controller index
 * @return Pointer to controller, or NULL if invalid index
 */
ahci_controller_t* ahci_get_controller(int index);

/* Error codes */
#define AHCI_SUCCESS            0
#define AHCI_ERR_NO_DEVICE      (-1)    /* No device on port */
#define AHCI_ERR_NOT_READY      (-2)    /* Device not ready */
#define AHCI_ERR_TIMEOUT        (-3)    /* Command timeout */
#define AHCI_ERR_PORT_HUNG      (-4)    /* Port is hung/unresponsive */
#define AHCI_ERR_TASK_FILE      (-5)    /* Task file error */
#define AHCI_ERR_INVALID_PORT   (-6)    /* Invalid port number */
#define AHCI_ERR_NO_MEMORY      (-7)    /* Memory allocation failed */
#define AHCI_ERR_UNSUPPORTED    (-8)    /* Unsupported device type */

#endif /* _AAAOS_AHCI_H */
