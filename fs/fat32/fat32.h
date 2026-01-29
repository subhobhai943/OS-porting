/**
 * AAAos FAT32 Filesystem Driver
 *
 * This driver implements the FAT32 filesystem for AAAos, providing:
 * - Reading and parsing FAT32 boot sector and BPB
 * - Directory traversal and file lookup
 * - File read and write operations
 * - VFS integration
 *
 * FAT32 Structure Overview:
 * - Boot Sector (Sector 0): Contains BPB and filesystem metadata
 * - Reserved Sectors: Contains boot code and FSInfo
 * - FAT Region: File Allocation Table(s)
 * - Data Region: Clusters containing file/directory data
 */

#ifndef _AAAOS_FAT32_H
#define _AAAOS_FAT32_H

#include "../../kernel/include/types.h"
#include "../vfs/vfs.h"

/*============================================================================
 * FAT32 Constants
 *============================================================================*/

/* FAT32 Signature bytes */
#define FAT32_BOOT_SIGNATURE        0xAA55
#define FAT32_FSINFO_LEAD_SIG       0x41615252
#define FAT32_FSINFO_STRUC_SIG      0x61417272
#define FAT32_FSINFO_TRAIL_SIG      0xAA550000

/* FAT entry special values */
#define FAT32_CLUSTER_FREE          0x00000000  /* Cluster is free */
#define FAT32_CLUSTER_RESERVED      0x00000001  /* Reserved cluster */
#define FAT32_CLUSTER_BAD           0x0FFFFFF7  /* Bad cluster */
#define FAT32_CLUSTER_EOF_MIN       0x0FFFFFF8  /* End of chain minimum */
#define FAT32_CLUSTER_EOF           0x0FFFFFFF  /* End of chain marker */

/* FAT32 cluster mask (28 bits used) */
#define FAT32_CLUSTER_MASK          0x0FFFFFFF

/* First valid data cluster */
#define FAT32_FIRST_DATA_CLUSTER    2

/* Directory entry attributes */
#define FAT32_ATTR_READ_ONLY        0x01
#define FAT32_ATTR_HIDDEN           0x02
#define FAT32_ATTR_SYSTEM           0x04
#define FAT32_ATTR_VOLUME_ID        0x08
#define FAT32_ATTR_DIRECTORY        0x10
#define FAT32_ATTR_ARCHIVE          0x20
#define FAT32_ATTR_LONG_NAME        (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | \
                                     FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID)
#define FAT32_ATTR_LONG_NAME_MASK   (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | \
                                     FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID | \
                                     FAT32_ATTR_DIRECTORY | FAT32_ATTR_ARCHIVE)

/* Long filename entry constants */
#define FAT32_LFN_LAST_ENTRY        0x40
#define FAT32_LFN_SEQ_MASK          0x1F
#define FAT32_LFN_CHARS_PER_ENTRY   13

/* Directory entry special first byte values */
#define FAT32_DIRENT_FREE           0xE5        /* Entry is free */
#define FAT32_DIRENT_END            0x00        /* Entry is free and all following entries */
#define FAT32_DIRENT_KANJI          0x05        /* First char is actually 0xE5 (Kanji) */

/* Standard sector size */
#define FAT32_SECTOR_SIZE           512

/* Maximum path component length */
#define FAT32_MAX_NAME              255
#define FAT32_SHORT_NAME_LEN        11

/* FAT cache size (number of sectors to cache) */
#define FAT32_FAT_CACHE_SIZE        16

/*============================================================================
 * FAT32 On-Disk Structures
 *============================================================================*/

/**
 * FAT32 BIOS Parameter Block (BPB)
 * Located at offset 0 in the boot sector
 * All multi-byte values are little-endian
 */
typedef struct PACKED {
    /* Common BIOS Parameter Block (offset 0-35) */
    uint8_t     jmp_boot[3];        /* 0x00: Jump instruction to boot code */
    char        oem_name[8];        /* 0x03: OEM Name (e.g., "MSWIN4.1") */
    uint16_t    bytes_per_sector;   /* 0x0B: Bytes per sector (usually 512) */
    uint8_t     sectors_per_cluster;/* 0x0D: Sectors per cluster (power of 2) */
    uint16_t    reserved_sectors;   /* 0x0E: Reserved sector count (includes boot) */
    uint8_t     num_fats;           /* 0x10: Number of FATs (usually 2) */
    uint16_t    root_entry_count;   /* 0x11: Root entry count (0 for FAT32) */
    uint16_t    total_sectors_16;   /* 0x13: Total sectors 16-bit (0 for FAT32) */
    uint8_t     media_type;         /* 0x15: Media type (0xF8 for fixed disk) */
    uint16_t    fat_size_16;        /* 0x16: FAT size 16-bit (0 for FAT32) */
    uint16_t    sectors_per_track;  /* 0x18: Sectors per track */
    uint16_t    num_heads;          /* 0x1A: Number of heads */
    uint32_t    hidden_sectors;     /* 0x1C: Hidden sectors before partition */
    uint32_t    total_sectors_32;   /* 0x20: Total sectors 32-bit */

    /* FAT32 Extended BPB (offset 36-89) */
    uint32_t    fat_size_32;        /* 0x24: FAT size in sectors (FAT32) */
    uint16_t    ext_flags;          /* 0x28: Extended flags */
    uint16_t    fs_version;         /* 0x2A: Filesystem version (0.0) */
    uint32_t    root_cluster;       /* 0x2C: Root directory first cluster */
    uint16_t    fs_info_sector;     /* 0x30: FSInfo sector number */
    uint16_t    backup_boot_sector; /* 0x32: Backup boot sector location */
    uint8_t     reserved[12];       /* 0x34: Reserved */
    uint8_t     drive_number;       /* 0x40: Drive number (0x80 for HDD) */
    uint8_t     reserved1;          /* 0x41: Reserved */
    uint8_t     boot_signature;     /* 0x42: Extended boot signature (0x29) */
    uint32_t    volume_id;          /* 0x43: Volume serial number */
    char        volume_label[11];   /* 0x47: Volume label */
    char        fs_type[8];         /* 0x52: Filesystem type ("FAT32   ") */

    /* Boot code (offset 90-509) */
    uint8_t     boot_code[420];     /* 0x5A: Boot code */

    /* Boot sector signature (offset 510-511) */
    uint16_t    boot_sector_sig;    /* 0x1FE: Boot signature (0xAA55) */
} fat32_bpb_t;

/**
 * FAT32 FSInfo Structure
 * Located at the FSInfo sector (usually sector 1)
 */
typedef struct PACKED {
    uint32_t    lead_sig;           /* 0x00: Lead signature (0x41615252) */
    uint8_t     reserved1[480];     /* 0x04: Reserved */
    uint32_t    struc_sig;          /* 0x1E4: Structure signature (0x61417272) */
    uint32_t    free_count;         /* 0x1E8: Last known free cluster count */
    uint32_t    next_free;          /* 0x1EC: Hint for next free cluster */
    uint8_t     reserved2[12];      /* 0x1F0: Reserved */
    uint32_t    trail_sig;          /* 0x1FC: Trail signature (0xAA550000) */
} fat32_fsinfo_t;

/**
 * FAT32 Directory Entry (32 bytes)
 * Short filename format (8.3)
 */
typedef struct PACKED {
    char        name[8];            /* 0x00: Short name (padded with spaces) */
    char        ext[3];             /* 0x08: Extension (padded with spaces) */
    uint8_t     attr;               /* 0x0B: File attributes */
    uint8_t     nt_reserved;        /* 0x0C: Reserved for Windows NT */
    uint8_t     create_time_tenth;  /* 0x0D: Creation time (tenths of second) */
    uint16_t    create_time;        /* 0x0E: Creation time */
    uint16_t    create_date;        /* 0x10: Creation date */
    uint16_t    access_date;        /* 0x12: Last access date */
    uint16_t    cluster_high;       /* 0x14: High 16 bits of first cluster */
    uint16_t    modify_time;        /* 0x16: Last modification time */
    uint16_t    modify_date;        /* 0x18: Last modification date */
    uint16_t    cluster_low;        /* 0x1A: Low 16 bits of first cluster */
    uint32_t    file_size;          /* 0x1C: File size in bytes */
} fat32_dir_entry_t;

/**
 * FAT32 Long Filename Entry (32 bytes)
 */
typedef struct PACKED {
    uint8_t     order;              /* 0x00: Sequence number (1-20) | 0x40 for last */
    uint16_t    name1[5];           /* 0x01: Characters 1-5 (Unicode) */
    uint8_t     attr;               /* 0x0B: Attributes (always 0x0F) */
    uint8_t     type;               /* 0x0C: Type (always 0x00) */
    uint8_t     checksum;           /* 0x0D: Checksum of short name */
    uint16_t    name2[6];           /* 0x0E: Characters 6-11 (Unicode) */
    uint16_t    first_cluster;      /* 0x1A: First cluster (always 0x0000) */
    uint16_t    name3[2];           /* 0x1C: Characters 12-13 (Unicode) */
} fat32_lfn_entry_t;

/*============================================================================
 * FAT32 Runtime Structures
 *============================================================================*/

/**
 * Block device operations interface
 * Abstraction layer for reading/writing sectors to storage
 */
typedef struct {
    /**
     * Read sectors from device
     * @param device Device-specific data
     * @param lba Logical Block Address (sector number)
     * @param count Number of sectors to read
     * @param buffer Buffer to read into
     * @return 0 on success, negative error code on failure
     */
    int (*read_sectors)(void *device, uint64_t lba, uint32_t count, void *buffer);

    /**
     * Write sectors to device
     * @param device Device-specific data
     * @param lba Logical Block Address (sector number)
     * @param count Number of sectors to write
     * @param buffer Buffer to write from
     * @return 0 on success, negative error code on failure
     */
    int (*write_sectors)(void *device, uint64_t lba, uint32_t count, const void *buffer);

    /**
     * Flush pending writes to device
     * @param device Device-specific data
     * @return 0 on success, negative error code on failure
     */
    int (*flush)(void *device);
} fat32_block_ops_t;

/**
 * FAT cache entry
 */
typedef struct {
    uint32_t    sector;             /* Cached FAT sector number */
    bool        dirty;              /* Cache entry has been modified */
    bool        valid;              /* Cache entry contains valid data */
    uint8_t     data[FAT32_SECTOR_SIZE]; /* Cached sector data */
} fat32_fat_cache_entry_t;

/**
 * FAT32 Filesystem State
 * Main structure holding all mount-related information
 */
typedef struct {
    /* Block device interface */
    void                    *device;        /* Device-specific data */
    fat32_block_ops_t       *block_ops;     /* Block device operations */

    /* BPB information (cached from boot sector) */
    fat32_bpb_t             bpb;            /* Copy of BPB */

    /* Calculated filesystem geometry */
    uint32_t                bytes_per_cluster;
    uint32_t                fat_start_sector;   /* First FAT sector */
    uint32_t                fat_sectors;        /* Size of one FAT in sectors */
    uint32_t                data_start_sector;  /* First data sector */
    uint32_t                total_clusters;     /* Total data clusters */
    uint32_t                root_cluster;       /* Root directory first cluster */

    /* FSInfo data */
    uint32_t                free_clusters;      /* Free cluster count */
    uint32_t                next_free_cluster;  /* Hint for next free cluster */
    bool                    fsinfo_dirty;       /* FSInfo needs to be written */

    /* FAT cache */
    fat32_fat_cache_entry_t fat_cache[FAT32_FAT_CACHE_SIZE];

    /* Cluster buffer (for reading full clusters) */
    uint8_t                 *cluster_buffer;

    /* Mount state */
    bool                    mounted;
    bool                    readonly;

    /* VFS integration */
    vfs_mount_t             *vfs_mount;         /* Associated VFS mount */
} fat32_fs_t;

/**
 * FAT32 file handle
 * Used internally to track open files
 */
typedef struct {
    fat32_fs_t          *fs;                /* Filesystem reference */
    fat32_dir_entry_t   entry;              /* Copy of directory entry */
    uint32_t            first_cluster;      /* First cluster of file */
    uint32_t            current_cluster;    /* Current cluster being accessed */
    uint32_t            cluster_offset;     /* Offset within cluster chain */
    uint64_t            position;           /* Current file position */
    char                path[VFS_PATH_MAX]; /* Full path to file */
    bool                dirty;              /* File has been modified */
    bool                is_dir;             /* Is a directory */
} fat32_file_t;

/*============================================================================
 * FAT32 Core Functions
 *============================================================================*/

/**
 * Initialize the FAT32 driver
 * Registers FAT32 with the VFS
 * @return 0 on success, negative error code on failure
 */
int fat32_init(void);

/**
 * Mount a FAT32 volume
 * @param device Device-specific data
 * @param block_ops Block device operations
 * @return Pointer to filesystem state, or NULL on failure
 */
fat32_fs_t* fat32_mount(void *device, fat32_block_ops_t *block_ops);

/**
 * Unmount a FAT32 volume
 * @param fs Filesystem state from fat32_mount
 * @return 0 on success, negative error code on failure
 */
int fat32_unmount(fat32_fs_t *fs);

/**
 * Read a cluster from the filesystem
 * @param fs Filesystem state
 * @param cluster Cluster number to read
 * @param buf Buffer to read into (must be cluster_size bytes)
 * @return 0 on success, negative error code on failure
 */
int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buf);

/**
 * Write a cluster to the filesystem
 * @param fs Filesystem state
 * @param cluster Cluster number to write
 * @param buf Buffer to write from (must be cluster_size bytes)
 * @return 0 on success, negative error code on failure
 */
int fat32_write_cluster(fat32_fs_t *fs, uint32_t cluster, const void *buf);

/**
 * Get the next cluster in a chain
 * @param fs Filesystem state
 * @param cluster Current cluster number
 * @return Next cluster number, or FAT32_CLUSTER_EOF/FAT32_CLUSTER_BAD
 */
uint32_t fat32_next_cluster(fat32_fs_t *fs, uint32_t cluster);

/**
 * Set the next cluster in a chain (modify FAT)
 * @param fs Filesystem state
 * @param cluster Cluster to modify
 * @param value New value for the FAT entry
 * @return 0 on success, negative error code on failure
 */
int fat32_set_cluster(fat32_fs_t *fs, uint32_t cluster, uint32_t value);

/**
 * Allocate a free cluster
 * @param fs Filesystem state
 * @return Cluster number, or 0 on failure (no free clusters)
 */
uint32_t fat32_alloc_cluster(fat32_fs_t *fs);

/**
 * Free a cluster chain
 * @param fs Filesystem state
 * @param start_cluster First cluster of chain to free
 * @return 0 on success, negative error code on failure
 */
int fat32_free_chain(fat32_fs_t *fs, uint32_t start_cluster);

/*============================================================================
 * FAT32 Directory Functions
 *============================================================================*/

/**
 * Find a file or directory entry by path
 * @param fs Filesystem state
 * @param path Path to search for (e.g., "/dir/file.txt")
 * @param out Output directory entry
 * @return 0 on success, negative error code on failure
 */
int fat32_find_entry(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *out);

/**
 * Find a file or directory and return its first cluster
 * @param fs Filesystem state
 * @param path Path to search for
 * @param out_entry Output directory entry (can be NULL)
 * @param out_cluster Output first cluster number
 * @return 0 on success, negative error code on failure
 */
int fat32_lookup(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *out_entry,
                 uint32_t *out_cluster);

/**
 * List directory contents
 * @param fs Filesystem state
 * @param path Directory path
 * @return Array of directory entries (NULL-terminated), or NULL on failure
 *         Caller must free the returned array
 */
fat32_dir_entry_t* fat32_list_dir(fat32_fs_t *fs, const char *path);

/**
 * Read a directory entry by index within a directory
 * @param fs Filesystem state
 * @param dir_cluster First cluster of directory
 * @param index Entry index (0-based)
 * @param out Output directory entry
 * @param out_name Output long filename (if available)
 * @return 0 on success, VFS_ERR_NOENT if no more entries, negative on error
 */
int fat32_read_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster, uint32_t index,
                         fat32_dir_entry_t *out, char *out_name);

/**
 * Create a new directory entry
 * @param fs Filesystem state
 * @param parent_cluster First cluster of parent directory
 * @param name Entry name
 * @param attr Attributes
 * @param out_entry Output created entry
 * @return 0 on success, negative error code on failure
 */
int fat32_create_entry(fat32_fs_t *fs, uint32_t parent_cluster, const char *name,
                       uint8_t attr, fat32_dir_entry_t *out_entry);

/**
 * Delete a directory entry
 * @param fs Filesystem state
 * @param parent_cluster First cluster of parent directory
 * @param name Entry name to delete
 * @return 0 on success, negative error code on failure
 */
int fat32_delete_entry(fat32_fs_t *fs, uint32_t parent_cluster, const char *name);

/*============================================================================
 * FAT32 File Functions
 *============================================================================*/

/**
 * Read data from a file
 * @param fs Filesystem state
 * @param entry Directory entry of the file
 * @param buf Buffer to read into
 * @param offset Byte offset within file
 * @param len Number of bytes to read
 * @return Number of bytes read, or negative error code on failure
 */
ssize_t fat32_read_file(fat32_fs_t *fs, fat32_dir_entry_t *entry, void *buf,
                        size_t offset, size_t len);

/**
 * Write data to a file
 * @param fs Filesystem state
 * @param entry Directory entry of the file (will be updated)
 * @param parent_cluster Parent directory cluster (to update entry)
 * @param buf Buffer to write from
 * @param offset Byte offset within file
 * @param len Number of bytes to write
 * @return Number of bytes written, or negative error code on failure
 */
ssize_t fat32_write_file(fat32_fs_t *fs, fat32_dir_entry_t *entry,
                         uint32_t parent_cluster, const void *buf,
                         size_t offset, size_t len);

/**
 * Truncate or extend a file
 * @param fs Filesystem state
 * @param entry Directory entry of the file
 * @param parent_cluster Parent directory cluster
 * @param new_size New file size
 * @return 0 on success, negative error code on failure
 */
int fat32_truncate_file(fat32_fs_t *fs, fat32_dir_entry_t *entry,
                        uint32_t parent_cluster, uint32_t new_size);

/*============================================================================
 * FAT32 VFS Integration
 *============================================================================*/

/**
 * VFS open callback
 */
int fat32_vfs_open(vfs_node_t *node, int flags);

/**
 * VFS close callback
 */
int fat32_vfs_close(vfs_node_t *node);

/**
 * VFS read callback
 */
ssize_t fat32_vfs_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);

/**
 * VFS write callback
 */
ssize_t fat32_vfs_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);

/**
 * VFS readdir callback
 */
vfs_dirent_t* fat32_vfs_readdir(vfs_node_t *dir, uint32_t index);

/**
 * VFS finddir callback
 */
vfs_node_t* fat32_vfs_finddir(vfs_node_t *dir, const char *name);

/**
 * VFS stat callback
 */
int fat32_vfs_stat(vfs_node_t *node, vfs_stat_t *stat);

/**
 * VFS mkdir callback
 */
int fat32_vfs_mkdir(vfs_node_t *parent, const char *name, uint32_t permissions);

/**
 * VFS create callback
 */
int fat32_vfs_create(vfs_node_t *parent, const char *name, uint32_t permissions);

/**
 * VFS unlink callback
 */
int fat32_vfs_unlink(vfs_node_t *parent, const char *name);

/**
 * VFS mount callback
 */
int fat32_vfs_mount(vfs_mount_t *mount, void *device);

/**
 * VFS unmount callback
 */
int fat32_vfs_unmount(vfs_mount_t *mount);

/*============================================================================
 * FAT32 Utility Functions
 *============================================================================*/

/**
 * Convert cluster number to first sector number
 * @param fs Filesystem state
 * @param cluster Cluster number
 * @return First sector of the cluster
 */
static inline uint32_t fat32_cluster_to_sector(fat32_fs_t *fs, uint32_t cluster) {
    return fs->data_start_sector +
           ((cluster - FAT32_FIRST_DATA_CLUSTER) * fs->bpb.sectors_per_cluster);
}

/**
 * Check if a cluster number is valid (not EOF, BAD, or out of range)
 * @param fs Filesystem state
 * @param cluster Cluster number to check
 * @return true if valid data cluster
 */
static inline bool fat32_cluster_is_valid(fat32_fs_t *fs, uint32_t cluster) {
    cluster &= FAT32_CLUSTER_MASK;
    return (cluster >= FAT32_FIRST_DATA_CLUSTER &&
            cluster < (FAT32_FIRST_DATA_CLUSTER + fs->total_clusters));
}

/**
 * Check if a FAT entry indicates end of chain
 * @param entry FAT entry value
 * @return true if end of chain
 */
static inline bool fat32_is_eof(uint32_t entry) {
    return (entry & FAT32_CLUSTER_MASK) >= FAT32_CLUSTER_EOF_MIN;
}

/**
 * Get first cluster from directory entry
 * @param entry Directory entry
 * @return First cluster number
 */
static inline uint32_t fat32_entry_cluster(fat32_dir_entry_t *entry) {
    return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

/**
 * Set first cluster in directory entry
 * @param entry Directory entry
 * @param cluster Cluster number
 */
static inline void fat32_entry_set_cluster(fat32_dir_entry_t *entry, uint32_t cluster) {
    entry->cluster_high = (uint16_t)(cluster >> 16);
    entry->cluster_low = (uint16_t)(cluster & 0xFFFF);
}

/**
 * Convert FAT32 8.3 name to readable string
 * @param entry Directory entry
 * @param out Output buffer (at least 13 bytes)
 */
void fat32_format_short_name(fat32_dir_entry_t *entry, char *out);

/**
 * Convert filename to FAT32 8.3 format
 * @param name Input filename
 * @param out Output 11-byte buffer
 * @return 0 on success, negative if name cannot be represented as 8.3
 */
int fat32_to_short_name(const char *name, char *out);

/**
 * Calculate short name checksum (for LFN entries)
 * @param short_name 11-byte short name
 * @return Checksum value
 */
uint8_t fat32_short_name_checksum(const char *short_name);

/**
 * Sync all dirty data to disk
 * @param fs Filesystem state
 * @return 0 on success, negative error code on failure
 */
int fat32_sync(fat32_fs_t *fs);

/**
 * Get filesystem information
 * @param fs Filesystem state
 * @param total_bytes Output total size in bytes
 * @param free_bytes Output free space in bytes
 * @return 0 on success, negative error code on failure
 */
int fat32_statfs(fat32_fs_t *fs, uint64_t *total_bytes, uint64_t *free_bytes);

#endif /* _AAAOS_FAT32_H */
