/**
 * AAAos FAT32 Filesystem Driver Implementation
 *
 * This file implements the FAT32 filesystem driver for AAAos.
 */

#include "fat32.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/pmm.h"

/*============================================================================
 * Private Helper Functions - Forward Declarations
 *============================================================================*/

static int fat32_read_boot_sector(fat32_fs_t *fs);
static int fat32_read_fsinfo(fat32_fs_t *fs);
static int fat32_write_fsinfo(fat32_fs_t *fs);
static int fat32_flush_fat_cache(fat32_fs_t *fs);
static uint32_t fat32_read_fat_entry(fat32_fs_t *fs, uint32_t cluster);
static int fat32_write_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value);
static int fat32_find_entry_in_dir(fat32_fs_t *fs, uint32_t dir_cluster,
                                   const char *name, fat32_dir_entry_t *out,
                                   uint32_t *out_entry_index);
static char* fat32_path_next_component(const char *path, char *component, size_t max_len);
static int fat32_strcmp_83(const char *name, fat32_dir_entry_t *entry);
static void fat32_memset(void *dest, uint8_t val, size_t n);
static void fat32_memcpy(void *dest, const void *src, size_t n);
static size_t fat32_strlen(const char *s);
static int fat32_strncmp(const char *s1, const char *s2, size_t n);
static char fat32_toupper(char c);

/*============================================================================
 * VFS Operations Table
 *============================================================================*/

static vfs_ops_t fat32_vfs_ops = {
    .open       = fat32_vfs_open,
    .close      = fat32_vfs_close,
    .read       = fat32_vfs_read,
    .write      = fat32_vfs_write,
    .truncate   = NULL,     /* TODO: Implement */
    .sync       = NULL,     /* TODO: Implement */
    .readdir    = fat32_vfs_readdir,
    .finddir    = fat32_vfs_finddir,
    .mkdir      = fat32_vfs_mkdir,
    .rmdir      = NULL,     /* TODO: Implement */
    .create     = fat32_vfs_create,
    .unlink     = fat32_vfs_unlink,
    .rename     = NULL,     /* TODO: Implement */
    .stat       = fat32_vfs_stat,
    .chmod      = NULL,     /* FAT32 doesn't support Unix permissions */
    .chown      = NULL,     /* FAT32 doesn't support ownership */
    .mount      = fat32_vfs_mount,
    .unmount    = fat32_vfs_unmount,
    .sync_fs    = NULL,     /* TODO: Implement */
    .statfs     = NULL      /* TODO: Implement */
};

/*============================================================================
 * String/Memory Utility Functions
 *============================================================================*/

static void fat32_memset(void *dest, uint8_t val, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    while (n--) {
        *d++ = val;
    }
}

static void fat32_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
}

static size_t fat32_strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static int fat32_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static char fat32_toupper(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    }
    return c;
}

/*============================================================================
 * FAT32 Core Initialization
 *============================================================================*/

int fat32_init(void) {
    kprintf("[FAT32] Initializing FAT32 filesystem driver\n");

    /* Register with VFS */
    int result = vfs_register_fs("fat32", &fat32_vfs_ops);
    if (result != VFS_OK) {
        kprintf("[FAT32] Failed to register with VFS: %d\n", result);
        return result;
    }

    kprintf("[FAT32] FAT32 driver registered successfully\n");
    return 0;
}

/*============================================================================
 * Mount/Unmount Operations
 *============================================================================*/

fat32_fs_t* fat32_mount(void *device, fat32_block_ops_t *block_ops) {
    kprintf("[FAT32] Mounting FAT32 volume\n");

    if (!device || !block_ops || !block_ops->read_sectors) {
        kprintf("[FAT32] Invalid device or block operations\n");
        return NULL;
    }

    /* Allocate filesystem state */
    physaddr_t fs_phys = pmm_alloc_page();
    if (!fs_phys) {
        kprintf("[FAT32] Failed to allocate filesystem state\n");
        return NULL;
    }
    fat32_fs_t *fs = (fat32_fs_t *)(fs_phys + VMM_KERNEL_PHYS_MAP);
    fat32_memset(fs, 0, sizeof(fat32_fs_t));

    fs->device = device;
    fs->block_ops = block_ops;

    /* Read and validate boot sector */
    if (fat32_read_boot_sector(fs) != 0) {
        kprintf("[FAT32] Failed to read boot sector\n");
        pmm_free_page(fs_phys);
        return NULL;
    }

    /* Calculate filesystem geometry */
    fat32_bpb_t *bpb = &fs->bpb;

    fs->bytes_per_cluster = bpb->bytes_per_sector * bpb->sectors_per_cluster;
    fs->fat_start_sector = bpb->reserved_sectors;
    fs->fat_sectors = bpb->fat_size_32;
    fs->data_start_sector = bpb->reserved_sectors +
                            (bpb->num_fats * bpb->fat_size_32);
    fs->root_cluster = bpb->root_cluster;

    /* Calculate total data clusters */
    uint32_t total_sectors = bpb->total_sectors_32 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    uint32_t data_sectors = total_sectors - fs->data_start_sector;
    fs->total_clusters = data_sectors / bpb->sectors_per_cluster;

    kprintf("[FAT32] Volume info:\n");
    kprintf("[FAT32]   Bytes per sector: %u\n", bpb->bytes_per_sector);
    kprintf("[FAT32]   Sectors per cluster: %u\n", bpb->sectors_per_cluster);
    kprintf("[FAT32]   Bytes per cluster: %u\n", fs->bytes_per_cluster);
    kprintf("[FAT32]   Reserved sectors: %u\n", bpb->reserved_sectors);
    kprintf("[FAT32]   Number of FATs: %u\n", bpb->num_fats);
    kprintf("[FAT32]   FAT size (sectors): %u\n", fs->fat_sectors);
    kprintf("[FAT32]   Data start sector: %u\n", fs->data_start_sector);
    kprintf("[FAT32]   Total clusters: %u\n", fs->total_clusters);
    kprintf("[FAT32]   Root cluster: %u\n", fs->root_cluster);

    /* Read FSInfo sector */
    if (fat32_read_fsinfo(fs) != 0) {
        kprintf("[FAT32] Warning: Failed to read FSInfo sector\n");
        fs->free_clusters = 0xFFFFFFFF;  /* Unknown */
        fs->next_free_cluster = FAT32_FIRST_DATA_CLUSTER;
    }

    /* Allocate cluster buffer */
    size_t cluster_pages = (fs->bytes_per_cluster + PAGE_SIZE - 1) / PAGE_SIZE;
    physaddr_t cluster_buf_phys = pmm_alloc_pages(cluster_pages);
    if (!cluster_buf_phys) {
        kprintf("[FAT32] Failed to allocate cluster buffer\n");
        pmm_free_page(fs_phys);
        return NULL;
    }
    fs->cluster_buffer = (uint8_t *)(cluster_buf_phys + VMM_KERNEL_PHYS_MAP);

    /* Initialize FAT cache */
    for (int i = 0; i < FAT32_FAT_CACHE_SIZE; i++) {
        fs->fat_cache[i].valid = false;
        fs->fat_cache[i].dirty = false;
    }

    fs->mounted = true;
    fs->readonly = false;

    kprintf("[FAT32] Volume mounted successfully\n");
    return fs;
}

int fat32_unmount(fat32_fs_t *fs) {
    if (!fs || !fs->mounted) {
        return VFS_ERR_INVAL;
    }

    kprintf("[FAT32] Unmounting volume\n");

    /* Flush all dirty data */
    fat32_sync(fs);

    /* Free cluster buffer */
    if (fs->cluster_buffer) {
        size_t cluster_pages = (fs->bytes_per_cluster + PAGE_SIZE - 1) / PAGE_SIZE;
        physaddr_t cluster_buf_phys = (physaddr_t)fs->cluster_buffer - VMM_KERNEL_PHYS_MAP;
        pmm_free_pages(cluster_buf_phys, cluster_pages);
    }

    /* Free filesystem state */
    physaddr_t fs_phys = (physaddr_t)fs - VMM_KERNEL_PHYS_MAP;
    fs->mounted = false;
    pmm_free_page(fs_phys);

    kprintf("[FAT32] Volume unmounted successfully\n");
    return 0;
}

/*============================================================================
 * Boot Sector and FSInfo Operations
 *============================================================================*/

static int fat32_read_boot_sector(fat32_fs_t *fs) {
    uint8_t sector[FAT32_SECTOR_SIZE];

    /* Read boot sector (sector 0) */
    int result = fs->block_ops->read_sectors(fs->device, 0, 1, sector);
    if (result != 0) {
        kprintf("[FAT32] Failed to read boot sector: %d\n", result);
        return VFS_ERR_IO;
    }

    /* Copy BPB to filesystem state */
    fat32_memcpy(&fs->bpb, sector, sizeof(fat32_bpb_t));

    /* Validate boot signature */
    if (fs->bpb.boot_sector_sig != FAT32_BOOT_SIGNATURE) {
        kprintf("[FAT32] Invalid boot signature: 0x%04X (expected 0x%04X)\n",
                fs->bpb.boot_sector_sig, FAT32_BOOT_SIGNATURE);
        return VFS_ERR_INVAL;
    }

    /* Validate bytes per sector */
    if (fs->bpb.bytes_per_sector != 512 &&
        fs->bpb.bytes_per_sector != 1024 &&
        fs->bpb.bytes_per_sector != 2048 &&
        fs->bpb.bytes_per_sector != 4096) {
        kprintf("[FAT32] Invalid bytes per sector: %u\n", fs->bpb.bytes_per_sector);
        return VFS_ERR_INVAL;
    }

    /* Validate sectors per cluster (must be power of 2) */
    uint8_t spc = fs->bpb.sectors_per_cluster;
    if (spc == 0 || (spc & (spc - 1)) != 0) {
        kprintf("[FAT32] Invalid sectors per cluster: %u\n", spc);
        return VFS_ERR_INVAL;
    }

    /* Check for FAT32 (FAT size 16 should be 0 for FAT32) */
    if (fs->bpb.fat_size_16 != 0) {
        kprintf("[FAT32] This appears to be FAT12/FAT16, not FAT32\n");
        return VFS_ERR_INVAL;
    }

    /* Check filesystem type string */
    if (fat32_strncmp(fs->bpb.fs_type, "FAT32", 5) != 0) {
        kprintf("[FAT32] Warning: Filesystem type string is not 'FAT32'\n");
        /* Continue anyway, as this field is not always reliable */
    }

    return 0;
}

static int fat32_read_fsinfo(fat32_fs_t *fs) {
    if (fs->bpb.fs_info_sector == 0 || fs->bpb.fs_info_sector == 0xFFFF) {
        return VFS_ERR_INVAL;
    }

    uint8_t sector[FAT32_SECTOR_SIZE];
    int result = fs->block_ops->read_sectors(fs->device, fs->bpb.fs_info_sector, 1, sector);
    if (result != 0) {
        return VFS_ERR_IO;
    }

    fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)sector;

    /* Validate signatures */
    if (fsinfo->lead_sig != FAT32_FSINFO_LEAD_SIG ||
        fsinfo->struc_sig != FAT32_FSINFO_STRUC_SIG ||
        fsinfo->trail_sig != FAT32_FSINFO_TRAIL_SIG) {
        kprintf("[FAT32] Invalid FSInfo signatures\n");
        return VFS_ERR_INVAL;
    }

    fs->free_clusters = fsinfo->free_count;
    fs->next_free_cluster = fsinfo->next_free;

    kprintf("[FAT32] FSInfo: %u free clusters, next free hint: %u\n",
            fs->free_clusters, fs->next_free_cluster);

    return 0;
}

static int fat32_write_fsinfo(fat32_fs_t *fs) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    if (fs->bpb.fs_info_sector == 0 || fs->bpb.fs_info_sector == 0xFFFF) {
        return 0;  /* No FSInfo sector */
    }

    uint8_t sector[FAT32_SECTOR_SIZE];
    int result = fs->block_ops->read_sectors(fs->device, fs->bpb.fs_info_sector, 1, sector);
    if (result != 0) {
        return VFS_ERR_IO;
    }

    fat32_fsinfo_t *fsinfo = (fat32_fsinfo_t *)sector;
    fsinfo->free_count = fs->free_clusters;
    fsinfo->next_free = fs->next_free_cluster;

    result = fs->block_ops->write_sectors(fs->device, fs->bpb.fs_info_sector, 1, sector);
    if (result != 0) {
        return VFS_ERR_IO;
    }

    fs->fsinfo_dirty = false;
    return 0;
}

/*============================================================================
 * FAT Table Operations
 *============================================================================*/

static uint32_t fat32_read_fat_entry(fat32_fs_t *fs, uint32_t cluster) {
    /* Calculate which FAT sector contains this entry */
    uint32_t fat_offset = cluster * 4;  /* 4 bytes per FAT32 entry */
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bpb.bytes_per_sector;

    /* Check cache */
    int cache_idx = -1;
    int oldest_idx = 0;

    for (int i = 0; i < FAT32_FAT_CACHE_SIZE; i++) {
        if (fs->fat_cache[i].valid && fs->fat_cache[i].sector == fat_sector) {
            cache_idx = i;
            break;
        }
        if (!fs->fat_cache[i].valid) {
            oldest_idx = i;
        }
    }

    /* Cache miss - load sector */
    if (cache_idx < 0) {
        cache_idx = oldest_idx;

        /* Flush if dirty */
        if (fs->fat_cache[cache_idx].dirty) {
            uint32_t old_sector = fs->fat_cache[cache_idx].sector;
            fs->block_ops->write_sectors(fs->device, old_sector, 1,
                                         fs->fat_cache[cache_idx].data);
            fs->fat_cache[cache_idx].dirty = false;
        }

        /* Read new sector */
        int result = fs->block_ops->read_sectors(fs->device, fat_sector, 1,
                                                 fs->fat_cache[cache_idx].data);
        if (result != 0) {
            kprintf("[FAT32] Failed to read FAT sector %u\n", fat_sector);
            return FAT32_CLUSTER_BAD;
        }

        fs->fat_cache[cache_idx].sector = fat_sector;
        fs->fat_cache[cache_idx].valid = true;
    }

    /* Read entry from cache */
    uint32_t *entry = (uint32_t *)&fs->fat_cache[cache_idx].data[entry_offset];
    return (*entry) & FAT32_CLUSTER_MASK;
}

static int fat32_write_fat_entry(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->bpb.bytes_per_sector;

    /* Find or load the sector in cache */
    int cache_idx = -1;
    int oldest_idx = 0;

    for (int i = 0; i < FAT32_FAT_CACHE_SIZE; i++) {
        if (fs->fat_cache[i].valid && fs->fat_cache[i].sector == fat_sector) {
            cache_idx = i;
            break;
        }
        if (!fs->fat_cache[i].valid) {
            oldest_idx = i;
        }
    }

    if (cache_idx < 0) {
        cache_idx = oldest_idx;

        /* Flush if dirty */
        if (fs->fat_cache[cache_idx].dirty) {
            uint32_t old_sector = fs->fat_cache[cache_idx].sector;
            int result = fs->block_ops->write_sectors(fs->device, old_sector, 1,
                                                      fs->fat_cache[cache_idx].data);
            if (result != 0) {
                return VFS_ERR_IO;
            }
            fs->fat_cache[cache_idx].dirty = false;
        }

        /* Read new sector */
        int result = fs->block_ops->read_sectors(fs->device, fat_sector, 1,
                                                 fs->fat_cache[cache_idx].data);
        if (result != 0) {
            return VFS_ERR_IO;
        }

        fs->fat_cache[cache_idx].sector = fat_sector;
        fs->fat_cache[cache_idx].valid = true;
    }

    /* Modify entry in cache */
    uint32_t *entry = (uint32_t *)&fs->fat_cache[cache_idx].data[entry_offset];
    *entry = (*entry & 0xF0000000) | (value & FAT32_CLUSTER_MASK);  /* Preserve high 4 bits */
    fs->fat_cache[cache_idx].dirty = true;

    return 0;
}

static int fat32_flush_fat_cache(fat32_fs_t *fs) {
    if (fs->readonly) {
        return 0;
    }

    for (int i = 0; i < FAT32_FAT_CACHE_SIZE; i++) {
        if (fs->fat_cache[i].valid && fs->fat_cache[i].dirty) {
            int result = fs->block_ops->write_sectors(fs->device,
                                                      fs->fat_cache[i].sector, 1,
                                                      fs->fat_cache[i].data);
            if (result != 0) {
                kprintf("[FAT32] Failed to flush FAT cache entry %d\n", i);
                return VFS_ERR_IO;
            }

            /* Write to backup FAT if present */
            if (fs->bpb.num_fats > 1) {
                uint32_t backup_sector = fs->fat_cache[i].sector + fs->fat_sectors;
                fs->block_ops->write_sectors(fs->device, backup_sector, 1,
                                             fs->fat_cache[i].data);
            }

            fs->fat_cache[i].dirty = false;
        }
    }

    return 0;
}

/*============================================================================
 * Cluster Chain Operations
 *============================================================================*/

uint32_t fat32_next_cluster(fat32_fs_t *fs, uint32_t cluster) {
    if (!fat32_cluster_is_valid(fs, cluster)) {
        return FAT32_CLUSTER_EOF;
    }

    uint32_t next = fat32_read_fat_entry(fs, cluster);
    return next;
}

int fat32_set_cluster(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    return fat32_write_fat_entry(fs, cluster, value);
}

uint32_t fat32_alloc_cluster(fat32_fs_t *fs) {
    if (fs->readonly) {
        return 0;
    }

    uint32_t start = fs->next_free_cluster;
    if (start < FAT32_FIRST_DATA_CLUSTER) {
        start = FAT32_FIRST_DATA_CLUSTER;
    }

    uint32_t cluster = start;
    uint32_t max_cluster = FAT32_FIRST_DATA_CLUSTER + fs->total_clusters;

    /* Search for free cluster */
    do {
        uint32_t entry = fat32_read_fat_entry(fs, cluster);
        if (entry == FAT32_CLUSTER_FREE) {
            /* Found free cluster - mark as EOF */
            if (fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_EOF) != 0) {
                return 0;
            }

            /* Update free cluster hints */
            if (fs->free_clusters != 0xFFFFFFFF && fs->free_clusters > 0) {
                fs->free_clusters--;
            }
            fs->next_free_cluster = cluster + 1;
            fs->fsinfo_dirty = true;

            return cluster;
        }

        cluster++;
        if (cluster >= max_cluster) {
            cluster = FAT32_FIRST_DATA_CLUSTER;  /* Wrap around */
        }
    } while (cluster != start);

    kprintf("[FAT32] No free clusters available\n");
    return 0;  /* No free clusters */
}

int fat32_free_chain(fat32_fs_t *fs, uint32_t start_cluster) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    uint32_t cluster = start_cluster;
    uint32_t count = 0;

    while (fat32_cluster_is_valid(fs, cluster)) {
        uint32_t next = fat32_read_fat_entry(fs, cluster);

        /* Mark cluster as free */
        int result = fat32_write_fat_entry(fs, cluster, FAT32_CLUSTER_FREE);
        if (result != 0) {
            return result;
        }

        count++;

        if (fat32_is_eof(next)) {
            break;
        }
        cluster = next;
    }

    /* Update free cluster count */
    if (fs->free_clusters != 0xFFFFFFFF) {
        fs->free_clusters += count;
    }
    fs->fsinfo_dirty = true;

    return 0;
}

/*============================================================================
 * Cluster Read/Write Operations
 *============================================================================*/

int fat32_read_cluster(fat32_fs_t *fs, uint32_t cluster, void *buf) {
    if (!fat32_cluster_is_valid(fs, cluster)) {
        kprintf("[FAT32] Invalid cluster number: %u\n", cluster);
        return VFS_ERR_INVAL;
    }

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    int result = fs->block_ops->read_sectors(fs->device, sector,
                                             fs->bpb.sectors_per_cluster, buf);
    if (result != 0) {
        kprintf("[FAT32] Failed to read cluster %u (sector %u)\n", cluster, sector);
        return VFS_ERR_IO;
    }

    return 0;
}

int fat32_write_cluster(fat32_fs_t *fs, uint32_t cluster, const void *buf) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    if (!fat32_cluster_is_valid(fs, cluster)) {
        kprintf("[FAT32] Invalid cluster number: %u\n", cluster);
        return VFS_ERR_INVAL;
    }

    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    int result = fs->block_ops->write_sectors(fs->device, sector,
                                              fs->bpb.sectors_per_cluster, buf);
    if (result != 0) {
        kprintf("[FAT32] Failed to write cluster %u (sector %u)\n", cluster, sector);
        return VFS_ERR_IO;
    }

    return 0;
}

/*============================================================================
 * Path Parsing
 *============================================================================*/

/**
 * Extract the next path component from a path string
 * @param path Input path (e.g., "/dir/file.txt")
 * @param component Output buffer for component
 * @param max_len Maximum component length
 * @return Pointer to rest of path, or NULL if no more components
 */
static char* fat32_path_next_component(const char *path, char *component, size_t max_len) {
    /* Skip leading slashes */
    while (*path == '/' || *path == '\\') {
        path++;
    }

    if (*path == '\0') {
        return NULL;  /* No more components */
    }

    /* Copy component */
    size_t i = 0;
    while (*path && *path != '/' && *path != '\\' && i < max_len - 1) {
        component[i++] = *path++;
    }
    component[i] = '\0';

    return (char *)path;
}

/*============================================================================
 * Short Name (8.3) Operations
 *============================================================================*/

/**
 * Compare a name with a directory entry's short name (case-insensitive)
 */
static int fat32_strcmp_83(const char *name, fat32_dir_entry_t *entry) {
    char entry_name[13];
    fat32_format_short_name(entry, entry_name);

    /* Case-insensitive comparison */
    const char *a = name;
    char *b = entry_name;

    while (*a && *b) {
        char ca = fat32_toupper(*a);
        char cb = fat32_toupper(*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }

    return fat32_toupper(*a) - fat32_toupper(*b);
}

void fat32_format_short_name(fat32_dir_entry_t *entry, char *out) {
    int i, j = 0;

    /* Copy name part (8 chars), trimming trailing spaces */
    for (i = 7; i >= 0 && entry->name[i] == ' '; i--);
    for (int k = 0; k <= i; k++) {
        out[j++] = entry->name[k];
    }

    /* Add dot and extension if present */
    for (i = 2; i >= 0 && entry->ext[i] == ' '; i--);
    if (i >= 0) {
        out[j++] = '.';
        for (int k = 0; k <= i; k++) {
            out[j++] = entry->ext[k];
        }
    }

    out[j] = '\0';
}

int fat32_to_short_name(const char *name, char *out) {
    fat32_memset(out, ' ', 11);

    int i = 0, j = 0;
    bool has_dot = false;

    /* Find the last dot */
    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
        }
    }

    /* Copy name part (up to 8 chars before dot or end) */
    const char *end = dot ? dot : name + fat32_strlen(name);
    for (const char *p = name; p < end && j < 8; p++) {
        char c = fat32_toupper(*p);
        /* Valid characters for short names */
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
            c == '\'' || c == '(' || c == ')' || c == '-' || c == '@' ||
            c == '^' || c == '_' || c == '`' || c == '{' || c == '}' || c == '~') {
            out[j++] = c;
        }
    }

    /* Copy extension part (up to 3 chars after dot) */
    if (dot) {
        j = 8;
        for (const char *p = dot + 1; *p && j < 11; p++) {
            char c = fat32_toupper(*p);
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '!' || c == '#' || c == '$' || c == '%' || c == '&' ||
                c == '\'' || c == '(' || c == ')' || c == '-' || c == '@' ||
                c == '^' || c == '_' || c == '`' || c == '{' || c == '}' || c == '~') {
                out[j++] = c;
            }
        }
    }

    return 0;
}

uint8_t fat32_short_name_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

/*============================================================================
 * Directory Operations
 *============================================================================*/

static int fat32_find_entry_in_dir(fat32_fs_t *fs, uint32_t dir_cluster,
                                   const char *name, fat32_dir_entry_t *out,
                                   uint32_t *out_entry_index) {
    uint32_t cluster = dir_cluster;
    uint32_t entry_index = 0;

    while (fat32_cluster_is_valid(fs, cluster)) {
        /* Read cluster */
        int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            return result;
        }

        /* Scan directory entries */
        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t *entry = &entries[i];

            /* End of directory */
            if (entry->name[0] == FAT32_DIRENT_END) {
                return VFS_ERR_NOENT;
            }

            /* Skip free entries */
            if ((uint8_t)entry->name[0] == FAT32_DIRENT_FREE) {
                entry_index++;
                continue;
            }

            /* Skip LFN entries and volume labels */
            if ((entry->attr & FAT32_ATTR_LONG_NAME_MASK) == FAT32_ATTR_LONG_NAME ||
                (entry->attr & FAT32_ATTR_VOLUME_ID)) {
                entry_index++;
                continue;
            }

            /* Compare names */
            if (fat32_strcmp_83(name, entry) == 0) {
                fat32_memcpy(out, entry, sizeof(fat32_dir_entry_t));
                if (out_entry_index) {
                    *out_entry_index = entry_index;
                }
                return 0;
            }

            entry_index++;
        }

        /* Move to next cluster */
        cluster = fat32_next_cluster(fs, cluster);
    }

    return VFS_ERR_NOENT;
}

int fat32_find_entry(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *out) {
    uint32_t cluster;
    return fat32_lookup(fs, path, out, &cluster);
}

int fat32_lookup(fat32_fs_t *fs, const char *path, fat32_dir_entry_t *out_entry,
                 uint32_t *out_cluster) {
    if (!fs || !path) {
        return VFS_ERR_INVAL;
    }

    /* Handle root directory */
    if (path[0] == '/' || path[0] == '\\') {
        if (path[1] == '\0') {
            /* Root directory itself */
            if (out_entry) {
                fat32_memset(out_entry, 0, sizeof(fat32_dir_entry_t));
                out_entry->attr = FAT32_ATTR_DIRECTORY;
                fat32_entry_set_cluster(out_entry, fs->root_cluster);
            }
            if (out_cluster) {
                *out_cluster = fs->root_cluster;
            }
            return 0;
        }
    }

    uint32_t current_cluster = fs->root_cluster;
    fat32_dir_entry_t entry;
    char component[FAT32_MAX_NAME + 1];
    const char *remaining = path;

    while ((remaining = fat32_path_next_component(remaining, component, sizeof(component))) != NULL ||
           component[0] != '\0') {

        /* Search for component in current directory */
        int result = fat32_find_entry_in_dir(fs, current_cluster, component, &entry, NULL);
        if (result != 0) {
            return result;
        }

        current_cluster = fat32_entry_cluster(&entry);

        /* Check if this is the last component */
        const char *next = remaining;
        while (next && (*next == '/' || *next == '\\')) next++;
        if (!next || *next == '\0') {
            break;  /* Found the target */
        }

        /* Not the last component - must be a directory */
        if (!(entry.attr & FAT32_ATTR_DIRECTORY)) {
            return VFS_ERR_NOTDIR;
        }

        remaining = next;
        component[0] = '\0';  /* Reset for loop condition */
    }

    if (out_entry) {
        fat32_memcpy(out_entry, &entry, sizeof(fat32_dir_entry_t));
    }
    if (out_cluster) {
        *out_cluster = current_cluster;
    }

    return 0;
}

int fat32_read_dir_entry(fat32_fs_t *fs, uint32_t dir_cluster, uint32_t index,
                         fat32_dir_entry_t *out, char *out_name) {
    uint32_t cluster = dir_cluster;
    uint32_t current_index = 0;
    uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);

    /* Find the cluster containing the requested index */
    while (current_index + entries_per_cluster <= index) {
        if (!fat32_cluster_is_valid(fs, cluster)) {
            return VFS_ERR_NOENT;
        }
        cluster = fat32_next_cluster(fs, cluster);
        current_index += entries_per_cluster;
    }

    /* Read the cluster */
    int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
    if (result != 0) {
        return result;
    }

    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;
    uint32_t offset = index - current_index;

    /* Scan from this position */
    while (offset < entries_per_cluster) {
        fat32_dir_entry_t *entry = &entries[offset];

        /* End of directory */
        if (entry->name[0] == FAT32_DIRENT_END) {
            return VFS_ERR_NOENT;
        }

        /* Skip free entries */
        if ((uint8_t)entry->name[0] == FAT32_DIRENT_FREE) {
            offset++;
            continue;
        }

        /* Skip LFN entries and volume labels */
        if ((entry->attr & FAT32_ATTR_LONG_NAME_MASK) == FAT32_ATTR_LONG_NAME ||
            (entry->attr & FAT32_ATTR_VOLUME_ID)) {
            offset++;
            continue;
        }

        /* Found a valid entry */
        fat32_memcpy(out, entry, sizeof(fat32_dir_entry_t));
        if (out_name) {
            fat32_format_short_name(entry, out_name);
        }
        return 0;
    }

    /* Need to read next cluster */
    cluster = fat32_next_cluster(fs, cluster);
    if (!fat32_cluster_is_valid(fs, cluster)) {
        return VFS_ERR_NOENT;
    }

    return fat32_read_dir_entry(fs, cluster, 0, out, out_name);
}

fat32_dir_entry_t* fat32_list_dir(fat32_fs_t *fs, const char *path) {
    /* Find the directory */
    fat32_dir_entry_t dir_entry;
    uint32_t dir_cluster;

    int result = fat32_lookup(fs, path, &dir_entry, &dir_cluster);
    if (result != 0) {
        kprintf("[FAT32] Directory not found: %s\n", path);
        return NULL;
    }

    if (!(dir_entry.attr & FAT32_ATTR_DIRECTORY)) {
        kprintf("[FAT32] Not a directory: %s\n", path);
        return NULL;
    }

    /* Count entries first */
    uint32_t count = 0;
    uint32_t cluster = dir_cluster;

    while (fat32_cluster_is_valid(fs, cluster)) {
        if (fat32_read_cluster(fs, cluster, fs->cluster_buffer) != 0) {
            return NULL;
        }

        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == FAT32_DIRENT_END) {
                goto count_done;
            }
            if ((uint8_t)entries[i].name[0] != FAT32_DIRENT_FREE &&
                (entries[i].attr & FAT32_ATTR_LONG_NAME_MASK) != FAT32_ATTR_LONG_NAME &&
                !(entries[i].attr & FAT32_ATTR_VOLUME_ID)) {
                count++;
            }
        }

        cluster = fat32_next_cluster(fs, cluster);
    }

count_done:
    /* Allocate array for entries (+1 for terminator) */
    size_t array_size = (count + 1) * sizeof(fat32_dir_entry_t);
    size_t pages_needed = (array_size + PAGE_SIZE - 1) / PAGE_SIZE;
    physaddr_t array_phys = pmm_alloc_pages(pages_needed);
    if (!array_phys) {
        return NULL;
    }

    fat32_dir_entry_t *array = (fat32_dir_entry_t *)(array_phys + VMM_KERNEL_PHYS_MAP);
    fat32_memset(array, 0, array_size);

    /* Copy entries */
    uint32_t idx = 0;
    cluster = dir_cluster;

    while (fat32_cluster_is_valid(fs, cluster) && idx < count) {
        if (fat32_read_cluster(fs, cluster, fs->cluster_buffer) != 0) {
            pmm_free_pages(array_phys, pages_needed);
            return NULL;
        }

        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

        for (uint32_t i = 0; i < entries_per_cluster && idx < count; i++) {
            if (entries[i].name[0] == FAT32_DIRENT_END) {
                goto copy_done;
            }
            if ((uint8_t)entries[i].name[0] != FAT32_DIRENT_FREE &&
                (entries[i].attr & FAT32_ATTR_LONG_NAME_MASK) != FAT32_ATTR_LONG_NAME &&
                !(entries[i].attr & FAT32_ATTR_VOLUME_ID)) {
                fat32_memcpy(&array[idx++], &entries[i], sizeof(fat32_dir_entry_t));
            }
        }

        cluster = fat32_next_cluster(fs, cluster);
    }

copy_done:
    /* Mark end with zero name */
    array[idx].name[0] = FAT32_DIRENT_END;

    return array;
}

int fat32_create_entry(fat32_fs_t *fs, uint32_t parent_cluster, const char *name,
                       uint8_t attr, fat32_dir_entry_t *out_entry) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    /* Convert name to 8.3 format */
    char short_name[11];
    fat32_to_short_name(name, short_name);

    /* Find a free entry in the directory */
    uint32_t cluster = parent_cluster;
    uint32_t prev_cluster = 0;

    while (fat32_cluster_is_valid(fs, cluster)) {
        int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            return result;
        }

        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == FAT32_DIRENT_END ||
                (uint8_t)entries[i].name[0] == FAT32_DIRENT_FREE) {

                /* Found a free slot - create entry */
                fat32_memset(&entries[i], 0, sizeof(fat32_dir_entry_t));
                fat32_memcpy(entries[i].name, short_name, 8);
                fat32_memcpy(entries[i].ext, short_name + 8, 3);
                entries[i].attr = attr;

                /* Allocate first cluster if this is a directory */
                if (attr & FAT32_ATTR_DIRECTORY) {
                    uint32_t new_cluster = fat32_alloc_cluster(fs);
                    if (!new_cluster) {
                        return VFS_ERR_NOSPC;
                    }
                    fat32_entry_set_cluster(&entries[i], new_cluster);

                    /* Initialize directory with . and .. entries */
                    fat32_memset(fs->cluster_buffer + fs->bytes_per_cluster, 0,
                                 fs->bytes_per_cluster);
                    /* We need another buffer, so just write the current cluster first */
                    result = fat32_write_cluster(fs, cluster, fs->cluster_buffer);
                    if (result != 0) {
                        return result;
                    }

                    /* Create . and .. entries */
                    uint8_t *dir_buf = fs->cluster_buffer;
                    fat32_memset(dir_buf, 0, fs->bytes_per_cluster);

                    fat32_dir_entry_t *dot = (fat32_dir_entry_t *)dir_buf;
                    fat32_memset(dot->name, ' ', 11);
                    dot->name[0] = '.';
                    dot->attr = FAT32_ATTR_DIRECTORY;
                    fat32_entry_set_cluster(dot, new_cluster);

                    fat32_dir_entry_t *dotdot = (fat32_dir_entry_t *)(dir_buf + 32);
                    fat32_memset(dotdot->name, ' ', 11);
                    dotdot->name[0] = '.';
                    dotdot->name[1] = '.';
                    dotdot->attr = FAT32_ATTR_DIRECTORY;
                    fat32_entry_set_cluster(dotdot, parent_cluster);

                    result = fat32_write_cluster(fs, new_cluster, dir_buf);
                    if (result != 0) {
                        return result;
                    }

                    /* Re-read parent cluster for output */
                    fat32_read_cluster(fs, cluster, fs->cluster_buffer);
                } else {
                    /* Write back modified cluster */
                    result = fat32_write_cluster(fs, cluster, fs->cluster_buffer);
                    if (result != 0) {
                        return result;
                    }
                }

                if (out_entry) {
                    fat32_memcpy(out_entry, &entries[i], sizeof(fat32_dir_entry_t));
                }
                return 0;
            }
        }

        prev_cluster = cluster;
        cluster = fat32_next_cluster(fs, cluster);
    }

    /* Need to allocate a new cluster for the directory */
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (!new_cluster) {
        return VFS_ERR_NOSPC;
    }

    /* Link new cluster to chain */
    int result = fat32_set_cluster(fs, prev_cluster, new_cluster);
    if (result != 0) {
        return result;
    }

    /* Initialize new cluster and add entry */
    fat32_memset(fs->cluster_buffer, 0, fs->bytes_per_cluster);
    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

    fat32_memcpy(entries[0].name, short_name, 8);
    fat32_memcpy(entries[0].ext, short_name + 8, 3);
    entries[0].attr = attr;

    if (attr & FAT32_ATTR_DIRECTORY) {
        uint32_t dir_cluster = fat32_alloc_cluster(fs);
        if (!dir_cluster) {
            return VFS_ERR_NOSPC;
        }
        fat32_entry_set_cluster(&entries[0], dir_cluster);
    }

    result = fat32_write_cluster(fs, new_cluster, fs->cluster_buffer);
    if (result != 0) {
        return result;
    }

    if (out_entry) {
        fat32_memcpy(out_entry, &entries[0], sizeof(fat32_dir_entry_t));
    }

    return 0;
}

int fat32_delete_entry(fat32_fs_t *fs, uint32_t parent_cluster, const char *name) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    uint32_t cluster = parent_cluster;

    while (fat32_cluster_is_valid(fs, cluster)) {
        int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            return result;
        }

        uint32_t entries_per_cluster = fs->bytes_per_cluster / sizeof(fat32_dir_entry_t);
        fat32_dir_entry_t *entries = (fat32_dir_entry_t *)fs->cluster_buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == FAT32_DIRENT_END) {
                return VFS_ERR_NOENT;
            }

            if ((uint8_t)entries[i].name[0] == FAT32_DIRENT_FREE) {
                continue;
            }

            if ((entries[i].attr & FAT32_ATTR_LONG_NAME_MASK) == FAT32_ATTR_LONG_NAME) {
                continue;
            }

            if (fat32_strcmp_83(name, &entries[i]) == 0) {
                /* Free the cluster chain */
                uint32_t file_cluster = fat32_entry_cluster(&entries[i]);
                if (file_cluster >= FAT32_FIRST_DATA_CLUSTER) {
                    fat32_free_chain(fs, file_cluster);
                }

                /* Mark entry as deleted */
                entries[i].name[0] = FAT32_DIRENT_FREE;

                /* Write back */
                return fat32_write_cluster(fs, cluster, fs->cluster_buffer);
            }
        }

        cluster = fat32_next_cluster(fs, cluster);
    }

    return VFS_ERR_NOENT;
}

/*============================================================================
 * File Read/Write Operations
 *============================================================================*/

ssize_t fat32_read_file(fat32_fs_t *fs, fat32_dir_entry_t *entry, void *buf,
                        size_t offset, size_t len) {
    if (!fs || !entry || !buf) {
        return VFS_ERR_INVAL;
    }

    /* Check if it's a directory */
    if (entry->attr & FAT32_ATTR_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }

    /* Bounds check */
    if (offset >= entry->file_size) {
        return 0;  /* EOF */
    }

    if (offset + len > entry->file_size) {
        len = entry->file_size - offset;
    }

    if (len == 0) {
        return 0;
    }

    uint32_t cluster = fat32_entry_cluster(entry);
    uint32_t cluster_size = fs->bytes_per_cluster;
    size_t bytes_read = 0;
    uint8_t *dest = (uint8_t *)buf;

    /* Skip to starting cluster */
    uint32_t clusters_to_skip = offset / cluster_size;
    uint32_t offset_in_cluster = offset % cluster_size;

    while (clusters_to_skip > 0 && fat32_cluster_is_valid(fs, cluster)) {
        cluster = fat32_next_cluster(fs, cluster);
        clusters_to_skip--;
    }

    /* Read data */
    while (len > 0 && fat32_cluster_is_valid(fs, cluster)) {
        int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            return result;
        }

        size_t to_copy = cluster_size - offset_in_cluster;
        if (to_copy > len) {
            to_copy = len;
        }

        fat32_memcpy(dest, fs->cluster_buffer + offset_in_cluster, to_copy);

        dest += to_copy;
        bytes_read += to_copy;
        len -= to_copy;
        offset_in_cluster = 0;

        cluster = fat32_next_cluster(fs, cluster);
    }

    return bytes_read;
}

ssize_t fat32_write_file(fat32_fs_t *fs, fat32_dir_entry_t *entry,
                         uint32_t parent_cluster, const void *buf,
                         size_t offset, size_t len) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    if (!fs || !entry || !buf) {
        return VFS_ERR_INVAL;
    }

    if (entry->attr & FAT32_ATTR_DIRECTORY) {
        return VFS_ERR_ISDIR;
    }

    if (len == 0) {
        return 0;
    }

    uint32_t cluster = fat32_entry_cluster(entry);
    uint32_t cluster_size = fs->bytes_per_cluster;
    size_t bytes_written = 0;
    const uint8_t *src = (const uint8_t *)buf;

    /* Handle file with no clusters yet */
    if (cluster < FAT32_FIRST_DATA_CLUSTER) {
        cluster = fat32_alloc_cluster(fs);
        if (!cluster) {
            return VFS_ERR_NOSPC;
        }
        fat32_entry_set_cluster(entry, cluster);
    }

    /* Skip to starting cluster */
    uint32_t clusters_to_skip = offset / cluster_size;
    uint32_t offset_in_cluster = offset % cluster_size;
    uint32_t prev_cluster = 0;

    while (clusters_to_skip > 0) {
        prev_cluster = cluster;
        uint32_t next = fat32_next_cluster(fs, cluster);

        if (fat32_is_eof(next) || !fat32_cluster_is_valid(fs, next)) {
            /* Need to extend the file */
            uint32_t new_cluster = fat32_alloc_cluster(fs);
            if (!new_cluster) {
                return VFS_ERR_NOSPC;
            }
            fat32_set_cluster(fs, cluster, new_cluster);
            next = new_cluster;

            /* Zero out new cluster */
            fat32_memset(fs->cluster_buffer, 0, cluster_size);
            fat32_write_cluster(fs, new_cluster, fs->cluster_buffer);
        }

        cluster = next;
        clusters_to_skip--;
    }

    /* Write data */
    while (len > 0) {
        /* Read current cluster content for partial writes */
        int result = fat32_read_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            fat32_memset(fs->cluster_buffer, 0, cluster_size);
        }

        size_t to_copy = cluster_size - offset_in_cluster;
        if (to_copy > len) {
            to_copy = len;
        }

        fat32_memcpy(fs->cluster_buffer + offset_in_cluster, src, to_copy);

        result = fat32_write_cluster(fs, cluster, fs->cluster_buffer);
        if (result != 0) {
            return result;
        }

        src += to_copy;
        bytes_written += to_copy;
        len -= to_copy;
        offset_in_cluster = 0;

        if (len > 0) {
            prev_cluster = cluster;
            uint32_t next = fat32_next_cluster(fs, cluster);

            if (fat32_is_eof(next) || !fat32_cluster_is_valid(fs, next)) {
                /* Need more clusters */
                uint32_t new_cluster = fat32_alloc_cluster(fs);
                if (!new_cluster) {
                    break;  /* Return partial write */
                }
                fat32_set_cluster(fs, cluster, new_cluster);
                next = new_cluster;
            }

            cluster = next;
        }
    }

    /* Update file size if necessary */
    size_t new_end = offset + bytes_written;
    if (new_end > entry->file_size) {
        entry->file_size = (uint32_t)new_end;
    }

    /* Note: Caller is responsible for updating the directory entry on disk */

    return bytes_written;
}

int fat32_truncate_file(fat32_fs_t *fs, fat32_dir_entry_t *entry,
                        uint32_t parent_cluster, uint32_t new_size) {
    if (fs->readonly) {
        return VFS_ERR_ROFS;
    }

    uint32_t old_size = entry->file_size;
    uint32_t cluster_size = fs->bytes_per_cluster;

    if (new_size == old_size) {
        return 0;
    }

    uint32_t cluster = fat32_entry_cluster(entry);

    if (new_size == 0) {
        /* Free all clusters */
        if (cluster >= FAT32_FIRST_DATA_CLUSTER) {
            fat32_free_chain(fs, cluster);
        }
        fat32_entry_set_cluster(entry, 0);
        entry->file_size = 0;
        return 0;
    }

    if (new_size < old_size) {
        /* Shrinking - free excess clusters */
        uint32_t clusters_needed = (new_size + cluster_size - 1) / cluster_size;
        uint32_t count = 1;
        uint32_t prev_cluster = 0;

        while (count < clusters_needed && fat32_cluster_is_valid(fs, cluster)) {
            prev_cluster = cluster;
            cluster = fat32_next_cluster(fs, cluster);
            count++;
        }

        if (prev_cluster && fat32_cluster_is_valid(fs, cluster)) {
            /* Mark end of chain */
            fat32_set_cluster(fs, prev_cluster, FAT32_CLUSTER_EOF);

            /* Free remaining clusters */
            fat32_free_chain(fs, cluster);
        }
    }
    /* Extending is handled by write operations */

    entry->file_size = new_size;
    return 0;
}

/*============================================================================
 * Sync and Utility Operations
 *============================================================================*/

int fat32_sync(fat32_fs_t *fs) {
    if (!fs || !fs->mounted) {
        return VFS_ERR_INVAL;
    }

    int result = fat32_flush_fat_cache(fs);
    if (result != 0) {
        return result;
    }

    if (fs->fsinfo_dirty) {
        result = fat32_write_fsinfo(fs);
        if (result != 0) {
            return result;
        }
    }

    if (fs->block_ops->flush) {
        fs->block_ops->flush(fs->device);
    }

    return 0;
}

int fat32_statfs(fat32_fs_t *fs, uint64_t *total_bytes, uint64_t *free_bytes) {
    if (!fs || !fs->mounted) {
        return VFS_ERR_INVAL;
    }

    if (total_bytes) {
        *total_bytes = (uint64_t)fs->total_clusters * fs->bytes_per_cluster;
    }

    if (free_bytes) {
        if (fs->free_clusters != 0xFFFFFFFF) {
            *free_bytes = (uint64_t)fs->free_clusters * fs->bytes_per_cluster;
        } else {
            /* Count free clusters manually */
            uint32_t free_count = 0;
            for (uint32_t i = FAT32_FIRST_DATA_CLUSTER;
                 i < FAT32_FIRST_DATA_CLUSTER + fs->total_clusters; i++) {
                if (fat32_read_fat_entry(fs, i) == FAT32_CLUSTER_FREE) {
                    free_count++;
                }
            }
            *free_bytes = (uint64_t)free_count * fs->bytes_per_cluster;
        }
    }

    return 0;
}

/*============================================================================
 * VFS Integration Callbacks
 *============================================================================*/

int fat32_vfs_open(vfs_node_t *node, int flags) {
    if (!node) {
        return VFS_ERR_INVAL;
    }

    /* Check write access on readonly filesystem */
    fat32_fs_t *fs = (fat32_fs_t *)node->mount->fs_data;
    if (fs->readonly && (flags & (VFS_O_WRONLY | VFS_O_RDWR))) {
        return VFS_ERR_ROFS;
    }

    /* Node already has fs_data pointing to fat32_file_t */
    return VFS_OK;
}

int fat32_vfs_close(vfs_node_t *node) {
    if (!node) {
        return VFS_ERR_INVAL;
    }

    /* Sync if dirty */
    if (node->dirty && node->mount) {
        fat32_fs_t *fs = (fat32_fs_t *)node->mount->fs_data;
        fat32_sync(fs);
    }

    return VFS_OK;
}

ssize_t fat32_vfs_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    if (!node || !buf) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *file = (fat32_file_t *)node->fs_data;
    if (!file) {
        return VFS_ERR_INVAL;
    }

    return fat32_read_file(file->fs, &file->entry, buf, (size_t)offset, size);
}

ssize_t fat32_vfs_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    if (!node || !buf) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *file = (fat32_file_t *)node->fs_data;
    if (!file) {
        return VFS_ERR_INVAL;
    }

    /* Get parent cluster from path (simplified - assumes file->path is set) */
    /* In a full implementation, we'd track the parent cluster */
    fat32_fs_t *fs = file->fs;

    ssize_t result = fat32_write_file(fs, &file->entry, fs->root_cluster, buf,
                                      (size_t)offset, size);
    if (result > 0) {
        node->size = file->entry.file_size;
        node->dirty = true;
    }

    return result;
}

vfs_dirent_t* fat32_vfs_readdir(vfs_node_t *dir, uint32_t index) {
    if (!dir || dir->type != VFS_NODE_DIRECTORY) {
        return NULL;
    }

    fat32_file_t *file = (fat32_file_t *)dir->fs_data;
    if (!file) {
        return NULL;
    }

    /* Allocate dirent structure */
    static vfs_dirent_t dirent;  /* Static for simplicity; should be dynamic */

    fat32_dir_entry_t entry;
    char name[FAT32_MAX_NAME + 1];

    /* Skip . and .. entries (index 0 and 1 often) */
    uint32_t actual_index = index;
    int result;

    do {
        result = fat32_read_dir_entry(file->fs, file->first_cluster, actual_index,
                                      &entry, name);
        if (result != 0) {
            return NULL;
        }

        /* Skip . and .. */
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            actual_index++;
            continue;
        }

        break;
    } while (1);

    /* Fill in dirent */
    dirent.d_ino = fat32_entry_cluster(&entry);
    dirent.d_type = (entry.attr & FAT32_ATTR_DIRECTORY) ?
                    VFS_NODE_DIRECTORY : VFS_NODE_FILE;

    /* Copy name */
    size_t name_len = fat32_strlen(name);
    if (name_len > VFS_NAME_MAX) {
        name_len = VFS_NAME_MAX;
    }
    fat32_memcpy(dirent.d_name, name, name_len);
    dirent.d_name[name_len] = '\0';

    return &dirent;
}

vfs_node_t* fat32_vfs_finddir(vfs_node_t *dir, const char *name) {
    if (!dir || !name || dir->type != VFS_NODE_DIRECTORY) {
        return NULL;
    }

    fat32_file_t *dir_file = (fat32_file_t *)dir->fs_data;
    if (!dir_file) {
        return NULL;
    }

    fat32_fs_t *fs = dir_file->fs;
    fat32_dir_entry_t entry;

    int result = fat32_find_entry_in_dir(fs, dir_file->first_cluster, name, &entry, NULL);
    if (result != 0) {
        return NULL;
    }

    /* Allocate VFS node */
    vfs_node_t *node = vfs_alloc_node();
    if (!node) {
        return NULL;
    }

    /* Allocate FAT32 file handle */
    physaddr_t file_phys = pmm_alloc_page();
    if (!file_phys) {
        vfs_free_node(node);
        return NULL;
    }

    fat32_file_t *file = (fat32_file_t *)(file_phys + VMM_KERNEL_PHYS_MAP);
    fat32_memset(file, 0, sizeof(fat32_file_t));
    file->fs = fs;
    fat32_memcpy(&file->entry, &entry, sizeof(fat32_dir_entry_t));
    file->first_cluster = fat32_entry_cluster(&entry);
    file->current_cluster = file->first_cluster;
    file->is_dir = (entry.attr & FAT32_ATTR_DIRECTORY) != 0;

    /* Fill in node */
    char formatted_name[13];
    fat32_format_short_name(&entry, formatted_name);
    size_t name_len = fat32_strlen(formatted_name);
    if (name_len > VFS_NAME_MAX) name_len = VFS_NAME_MAX;
    fat32_memcpy(node->name, formatted_name, name_len);
    node->name[name_len] = '\0';

    node->type = (entry.attr & FAT32_ATTR_DIRECTORY) ?
                 VFS_NODE_DIRECTORY : VFS_NODE_FILE;
    node->size = entry.file_size;
    node->inode = fat32_entry_cluster(&entry);
    node->mount = dir->mount;
    node->parent = dir;
    node->fs_data = file;

    /* Set permissions based on attributes */
    node->permissions = VFS_S_IRUSR | VFS_S_IRGRP | VFS_S_IROTH;
    if (!(entry.attr & FAT32_ATTR_READ_ONLY)) {
        node->permissions |= VFS_S_IWUSR | VFS_S_IWGRP | VFS_S_IWOTH;
    }
    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        node->permissions |= VFS_S_IXUSR | VFS_S_IXGRP | VFS_S_IXOTH;
    }

    return node;
}

int fat32_vfs_stat(vfs_node_t *node, vfs_stat_t *stat) {
    if (!node || !stat) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *file = (fat32_file_t *)node->fs_data;

    stat->st_ino = node->inode;
    stat->st_mode = node->permissions;
    stat->st_nlink = 1;
    stat->st_uid = 0;
    stat->st_gid = 0;
    stat->st_size = node->size;

    if (file && file->fs) {
        stat->st_blksize = file->fs->bytes_per_cluster;
        stat->st_blocks = (node->size + 511) / 512;
    } else {
        stat->st_blksize = 512;
        stat->st_blocks = (node->size + 511) / 512;
    }

    stat->st_type = node->type;
    stat->st_atime = node->atime;
    stat->st_mtime = node->mtime;
    stat->st_ctime = node->ctime;

    return VFS_OK;
}

int fat32_vfs_mkdir(vfs_node_t *parent, const char *name, uint32_t permissions) {
    UNUSED(permissions);  /* FAT32 doesn't support Unix permissions */

    if (!parent || !name || parent->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *parent_file = (fat32_file_t *)parent->fs_data;
    if (!parent_file) {
        return VFS_ERR_INVAL;
    }

    fat32_dir_entry_t entry;
    int result = fat32_create_entry(parent_file->fs, parent_file->first_cluster,
                                    name, FAT32_ATTR_DIRECTORY, &entry);
    if (result != 0) {
        return result;
    }

    kprintf("[FAT32] Created directory: %s\n", name);
    return VFS_OK;
}

int fat32_vfs_create(vfs_node_t *parent, const char *name, uint32_t permissions) {
    UNUSED(permissions);

    if (!parent || !name || parent->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *parent_file = (fat32_file_t *)parent->fs_data;
    if (!parent_file) {
        return VFS_ERR_INVAL;
    }

    fat32_dir_entry_t entry;
    int result = fat32_create_entry(parent_file->fs, parent_file->first_cluster,
                                    name, FAT32_ATTR_ARCHIVE, &entry);
    if (result != 0) {
        return result;
    }

    kprintf("[FAT32] Created file: %s\n", name);
    return VFS_OK;
}

int fat32_vfs_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !name || parent->type != VFS_NODE_DIRECTORY) {
        return VFS_ERR_INVAL;
    }

    fat32_file_t *parent_file = (fat32_file_t *)parent->fs_data;
    if (!parent_file) {
        return VFS_ERR_INVAL;
    }

    int result = fat32_delete_entry(parent_file->fs, parent_file->first_cluster, name);
    if (result != 0) {
        return result;
    }

    kprintf("[FAT32] Deleted: %s\n", name);
    return VFS_OK;
}

int fat32_vfs_mount(vfs_mount_t *mount, void *device) {
    if (!mount || !device) {
        return VFS_ERR_INVAL;
    }

    kprintf("[FAT32] VFS mount request for %s\n", mount->path);

    /* Device should provide block_ops - for now, cast it */
    fat32_block_ops_t *block_ops = (fat32_block_ops_t *)device;

    fat32_fs_t *fs = fat32_mount(device, block_ops);
    if (!fs) {
        return VFS_ERR_IO;
    }

    /* Store filesystem state */
    mount->fs_data = fs;
    fs->vfs_mount = mount;

    /* Create root node */
    vfs_node_t *root = vfs_alloc_node();
    if (!root) {
        fat32_unmount(fs);
        return VFS_ERR_NOMEM;
    }

    /* Allocate file handle for root */
    physaddr_t file_phys = pmm_alloc_page();
    if (!file_phys) {
        vfs_free_node(root);
        fat32_unmount(fs);
        return VFS_ERR_NOMEM;
    }

    fat32_file_t *root_file = (fat32_file_t *)(file_phys + VMM_KERNEL_PHYS_MAP);
    fat32_memset(root_file, 0, sizeof(fat32_file_t));
    root_file->fs = fs;
    root_file->first_cluster = fs->root_cluster;
    root_file->current_cluster = fs->root_cluster;
    root_file->is_dir = true;
    root_file->entry.attr = FAT32_ATTR_DIRECTORY;
    fat32_entry_set_cluster(&root_file->entry, fs->root_cluster);

    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = VFS_NODE_DIRECTORY;
    root->permissions = VFS_S_IRUSR | VFS_S_IWUSR | VFS_S_IXUSR |
                        VFS_S_IRGRP | VFS_S_IXGRP |
                        VFS_S_IROTH | VFS_S_IXOTH;
    root->mount = mount;
    root->fs_data = root_file;
    root->inode = fs->root_cluster;

    mount->root = root;

    kprintf("[FAT32] VFS mount complete\n");
    return VFS_OK;
}

int fat32_vfs_unmount(vfs_mount_t *mount) {
    if (!mount || !mount->fs_data) {
        return VFS_ERR_INVAL;
    }

    fat32_fs_t *fs = (fat32_fs_t *)mount->fs_data;

    /* Free root node file handle */
    if (mount->root && mount->root->fs_data) {
        physaddr_t file_phys = (physaddr_t)mount->root->fs_data - VMM_KERNEL_PHYS_MAP;
        pmm_free_page(file_phys);
    }

    /* Free root node */
    if (mount->root) {
        vfs_free_node(mount->root);
        mount->root = NULL;
    }

    /* Unmount filesystem */
    int result = fat32_unmount(fs);
    mount->fs_data = NULL;

    return result;
}
