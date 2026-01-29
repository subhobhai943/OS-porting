/**
 * AAAos Virtual File System (VFS) Layer
 *
 * This header defines the abstract file system interface that all
 * filesystem implementations (FAT32, ext4, NTFS, etc.) must implement.
 * The VFS provides a unified API for file and directory operations.
 */

#ifndef _AAAOS_VFS_H
#define _AAAOS_VFS_H

#include "../../kernel/include/types.h"

/* Maximum path length */
#define VFS_PATH_MAX        4096
#define VFS_NAME_MAX        255

/* Maximum number of open files system-wide */
#define VFS_MAX_OPEN_FILES  1024

/* Maximum number of mounted filesystems */
#define VFS_MAX_MOUNTS      64

/* File types */
typedef enum {
    VFS_NODE_FILE       = 0x01,     /* Regular file */
    VFS_NODE_DIRECTORY  = 0x02,     /* Directory */
    VFS_NODE_CHARDEV    = 0x03,     /* Character device */
    VFS_NODE_BLOCKDEV   = 0x04,     /* Block device */
    VFS_NODE_PIPE       = 0x05,     /* Pipe/FIFO */
    VFS_NODE_SYMLINK    = 0x06,     /* Symbolic link */
    VFS_NODE_SOCKET     = 0x07,     /* Socket */
    VFS_NODE_MOUNTPOINT = 0x08      /* Mount point */
} vfs_node_type_t;

/* File open flags */
#define VFS_O_RDONLY        0x0001  /* Open for reading only */
#define VFS_O_WRONLY        0x0002  /* Open for writing only */
#define VFS_O_RDWR          0x0003  /* Open for reading and writing */
#define VFS_O_APPEND        0x0008  /* Append on each write */
#define VFS_O_CREAT         0x0200  /* Create file if it doesn't exist */
#define VFS_O_TRUNC         0x0400  /* Truncate file to zero length */
#define VFS_O_EXCL          0x0800  /* Error if VFS_O_CREAT and file exists */
#define VFS_O_DIRECTORY     0x10000 /* Must be a directory */

/* Seek whence values */
#define VFS_SEEK_SET        0       /* Seek from beginning of file */
#define VFS_SEEK_CUR        1       /* Seek from current position */
#define VFS_SEEK_END        2       /* Seek from end of file */

/* File permissions (Unix-style) */
#define VFS_S_IRUSR         0400    /* Owner read */
#define VFS_S_IWUSR         0200    /* Owner write */
#define VFS_S_IXUSR         0100    /* Owner execute */
#define VFS_S_IRGRP         0040    /* Group read */
#define VFS_S_IWGRP         0020    /* Group write */
#define VFS_S_IXGRP         0010    /* Group execute */
#define VFS_S_IROTH         0004    /* Others read */
#define VFS_S_IWOTH         0002    /* Others write */
#define VFS_S_IXOTH         0001    /* Others execute */

/* Error codes */
#define VFS_OK              0       /* Success */
#define VFS_ERR_NOENT       (-2)    /* No such file or directory */
#define VFS_ERR_IO          (-5)    /* I/O error */
#define VFS_ERR_NXIO        (-6)    /* No such device or address */
#define VFS_ERR_BADF        (-9)    /* Bad file descriptor */
#define VFS_ERR_NOMEM       (-12)   /* Out of memory */
#define VFS_ERR_ACCES       (-13)   /* Permission denied */
#define VFS_ERR_EXIST       (-17)   /* File exists */
#define VFS_ERR_NOTDIR      (-20)   /* Not a directory */
#define VFS_ERR_ISDIR       (-21)   /* Is a directory */
#define VFS_ERR_INVAL       (-22)   /* Invalid argument */
#define VFS_ERR_NFILE       (-23)   /* Too many open files */
#define VFS_ERR_FBIG        (-27)   /* File too large */
#define VFS_ERR_NOSPC       (-28)   /* No space left on device */
#define VFS_ERR_ROFS        (-30)   /* Read-only filesystem */
#define VFS_ERR_NAMETOOLONG (-36)   /* Filename too long */
#define VFS_ERR_NOTEMPTY    (-39)   /* Directory not empty */
#define VFS_ERR_NOSYS       (-78)   /* Function not implemented */
#define VFS_ERR_BUSY        (-16)   /* Device or resource busy */

/* Forward declarations */
struct vfs_node;
struct vfs_mount;
struct vfs_file;
struct vfs_dirent;
struct vfs_stat;
struct vfs_ops;

typedef struct vfs_node vfs_node_t;
typedef struct vfs_mount vfs_mount_t;
typedef struct vfs_file vfs_file_t;
typedef struct vfs_dirent vfs_dirent_t;
typedef struct vfs_stat vfs_stat_t;
typedef struct vfs_ops vfs_ops_t;

/**
 * File/directory statistics
 */
struct vfs_stat {
    uint64_t        st_ino;         /* Inode number */
    uint32_t        st_mode;        /* File mode (type + permissions) */
    uint32_t        st_nlink;       /* Number of hard links */
    uint32_t        st_uid;         /* User ID of owner */
    uint32_t        st_gid;         /* Group ID of owner */
    uint64_t        st_size;        /* Total size in bytes */
    uint64_t        st_blksize;     /* Block size for filesystem I/O */
    uint64_t        st_blocks;      /* Number of 512B blocks allocated */
    uint64_t        st_atime;       /* Time of last access */
    uint64_t        st_mtime;       /* Time of last modification */
    uint64_t        st_ctime;       /* Time of last status change */
    vfs_node_type_t st_type;        /* File type */
};

/**
 * Directory entry
 */
struct vfs_dirent {
    uint64_t        d_ino;                      /* Inode number */
    vfs_node_type_t d_type;                     /* Type of file */
    char            d_name[VFS_NAME_MAX + 1];   /* File name */
};

/**
 * VFS node (inode-like structure)
 * Represents a file or directory in the filesystem
 */
struct vfs_node {
    char            name[VFS_NAME_MAX + 1];     /* Node name */
    vfs_node_type_t type;                       /* Node type */
    uint32_t        permissions;                /* Access permissions */
    uint32_t        uid;                        /* User ID */
    uint32_t        gid;                        /* Group ID */
    uint64_t        inode;                      /* Inode number */
    uint64_t        size;                       /* File size in bytes */
    uint64_t        atime;                      /* Access time */
    uint64_t        mtime;                      /* Modification time */
    uint64_t        ctime;                      /* Creation time */
    uint32_t        nlink;                      /* Link count */
    uint32_t        flags;                      /* Various flags */

    vfs_mount_t     *mount;                     /* Mount this node belongs to */
    vfs_node_t      *parent;                    /* Parent directory */
    void            *fs_data;                   /* Filesystem-specific data */

    uint32_t        ref_count;                  /* Reference count */
    bool            dirty;                      /* Node has been modified */
};

/**
 * VFS operations - function pointers for filesystem implementations
 * Each filesystem type (FAT32, ext4, etc.) provides these operations
 */
struct vfs_ops {
    /* File operations */
    int     (*open)(vfs_node_t *node, int flags);
    int     (*close)(vfs_node_t *node);
    ssize_t (*read)(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
    ssize_t (*write)(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
    int     (*truncate)(vfs_node_t *node, uint64_t size);
    int     (*sync)(vfs_node_t *node);

    /* Directory operations */
    vfs_dirent_t* (*readdir)(vfs_node_t *dir, uint32_t index);
    vfs_node_t*   (*finddir)(vfs_node_t *dir, const char *name);
    int           (*mkdir)(vfs_node_t *parent, const char *name, uint32_t permissions);
    int           (*rmdir)(vfs_node_t *parent, const char *name);

    /* Node operations */
    int     (*create)(vfs_node_t *parent, const char *name, uint32_t permissions);
    int     (*unlink)(vfs_node_t *parent, const char *name);
    int     (*rename)(vfs_node_t *old_parent, const char *old_name,
                      vfs_node_t *new_parent, const char *new_name);
    int     (*stat)(vfs_node_t *node, vfs_stat_t *stat);
    int     (*chmod)(vfs_node_t *node, uint32_t mode);
    int     (*chown)(vfs_node_t *node, uint32_t uid, uint32_t gid);

    /* Filesystem operations */
    int     (*mount)(vfs_mount_t *mount, void *device);
    int     (*unmount)(vfs_mount_t *mount);
    int     (*sync_fs)(vfs_mount_t *mount);
    int     (*statfs)(vfs_mount_t *mount, void *buf);
};

/**
 * Mounted filesystem structure
 */
struct vfs_mount {
    char            path[VFS_PATH_MAX];         /* Mount point path */
    char            type[32];                   /* Filesystem type name */
    vfs_node_t      *root;                      /* Root node of mounted fs */
    vfs_node_t      *mountpoint;                /* Node we're mounted on */
    vfs_ops_t       *ops;                       /* Filesystem operations */
    void            *device;                    /* Device/partition info */
    void            *fs_data;                   /* Filesystem-specific data */
    uint32_t        flags;                      /* Mount flags */
    bool            readonly;                   /* Read-only mount */
    bool            active;                     /* Mount is active */
};

/**
 * Open file structure (file descriptor)
 */
struct vfs_file {
    vfs_node_t      *node;                      /* Associated VFS node */
    uint64_t        offset;                     /* Current file position */
    int             flags;                      /* Open flags */
    uint32_t        ref_count;                  /* Reference count */
    bool            in_use;                     /* Slot is in use */
};

/**
 * Directory handle for readdir operations
 */
typedef struct vfs_dir {
    vfs_node_t      *node;                      /* Directory node */
    uint32_t        position;                   /* Current position */
    bool            in_use;                     /* Handle is in use */
} vfs_dir_t;

/**
 * Filesystem type registration
 */
typedef struct vfs_fstype {
    char            name[32];                   /* Filesystem type name */
    vfs_ops_t       *ops;                       /* Default operations */
    struct vfs_fstype *next;                    /* Next in linked list */
} vfs_fstype_t;

/*============================================================================
 * VFS Public API
 *============================================================================*/

/**
 * Initialize the VFS subsystem
 * @return VFS_OK on success, error code on failure
 */
int vfs_init(void);

/**
 * Register a filesystem type
 * @param name Filesystem type name (e.g., "fat32", "ext4")
 * @param ops Filesystem operations
 * @return VFS_OK on success, error code on failure
 */
int vfs_register_fs(const char *name, vfs_ops_t *ops);

/**
 * Unregister a filesystem type
 * @param name Filesystem type name
 * @return VFS_OK on success, error code on failure
 */
int vfs_unregister_fs(const char *name);

/**
 * Mount a filesystem
 * @param path Mount point path
 * @param type Filesystem type (e.g., "fat32")
 * @param device Device/partition to mount
 * @return VFS_OK on success, error code on failure
 */
int vfs_mount(const char *path, const char *type, void *device);

/**
 * Unmount a filesystem
 * @param path Mount point path
 * @return VFS_OK on success, error code on failure
 */
int vfs_unmount(const char *path);

/**
 * Open a file
 * @param path File path
 * @param flags Open flags (VFS_O_*)
 * @return File handle on success, NULL on failure
 */
vfs_file_t* vfs_open(const char *path, int flags);

/**
 * Close a file
 * @param file File handle
 * @return VFS_OK on success, error code on failure
 */
int vfs_close(vfs_file_t *file);

/**
 * Read from a file
 * @param file File handle
 * @param buf Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read, or negative error code
 */
ssize_t vfs_read(vfs_file_t *file, void *buf, size_t size);

/**
 * Write to a file
 * @param file File handle
 * @param buf Buffer to write from
 * @param size Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t size);

/**
 * Seek within a file
 * @param file File handle
 * @param offset Offset to seek to
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New position, or negative error code
 */
int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence);

/**
 * Get file position
 * @param file File handle
 * @return Current position, or negative error code
 */
int64_t vfs_tell(vfs_file_t *file);

/**
 * Truncate or extend a file
 * @param file File handle
 * @param size New size
 * @return VFS_OK on success, error code on failure
 */
int vfs_truncate(vfs_file_t *file, uint64_t size);

/**
 * Sync file data to storage
 * @param file File handle
 * @return VFS_OK on success, error code on failure
 */
int vfs_sync(vfs_file_t *file);

/**
 * Open a directory for reading
 * @param path Directory path
 * @return Directory handle on success, NULL on failure
 */
vfs_dir_t* vfs_opendir(const char *path);

/**
 * Read next directory entry
 * @param dir Directory handle
 * @return Directory entry, or NULL if end of directory
 */
vfs_dirent_t* vfs_readdir(vfs_dir_t *dir);

/**
 * Close a directory
 * @param dir Directory handle
 * @return VFS_OK on success, error code on failure
 */
int vfs_closedir(vfs_dir_t *dir);

/**
 * Create a directory
 * @param path Directory path
 * @param mode Permissions
 * @return VFS_OK on success, error code on failure
 */
int vfs_mkdir(const char *path, uint32_t mode);

/**
 * Remove a directory
 * @param path Directory path
 * @return VFS_OK on success, error code on failure
 */
int vfs_rmdir(const char *path);

/**
 * Create a file
 * @param path File path
 * @param mode Permissions
 * @return VFS_OK on success, error code on failure
 */
int vfs_create(const char *path, uint32_t mode);

/**
 * Delete a file
 * @param path File path
 * @return VFS_OK on success, error code on failure
 */
int vfs_unlink(const char *path);

/**
 * Rename/move a file or directory
 * @param oldpath Source path
 * @param newpath Destination path
 * @return VFS_OK on success, error code on failure
 */
int vfs_rename(const char *oldpath, const char *newpath);

/**
 * Get file/directory status
 * @param path File path
 * @param stat Status buffer
 * @return VFS_OK on success, error code on failure
 */
int vfs_stat(const char *path, vfs_stat_t *stat);

/**
 * Get status of open file
 * @param file File handle
 * @param stat Status buffer
 * @return VFS_OK on success, error code on failure
 */
int vfs_fstat(vfs_file_t *file, vfs_stat_t *stat);

/**
 * Check if path exists
 * @param path Path to check
 * @return true if exists, false otherwise
 */
bool vfs_exists(const char *path);

/**
 * Check if path is a directory
 * @param path Path to check
 * @return true if directory, false otherwise
 */
bool vfs_is_directory(const char *path);

/**
 * Check if path is a regular file
 * @param path Path to check
 * @return true if regular file, false otherwise
 */
bool vfs_is_file(const char *path);

/*============================================================================
 * Internal VFS functions (used by filesystem implementations)
 *============================================================================*/

/**
 * Resolve a path to a VFS node
 * @param path Path to resolve
 * @return VFS node on success, NULL on failure
 */
vfs_node_t* vfs_lookup(const char *path);

/**
 * Get the mount point for a path
 * @param path Path to check
 * @return Mount structure, or NULL if not mounted
 */
vfs_mount_t* vfs_get_mount(const char *path);

/**
 * Allocate a new VFS node
 * @return New node, or NULL on failure
 */
vfs_node_t* vfs_alloc_node(void);

/**
 * Free a VFS node
 * @param node Node to free
 */
void vfs_free_node(vfs_node_t *node);

/**
 * Increment node reference count
 * @param node Node to reference
 */
void vfs_ref_node(vfs_node_t *node);

/**
 * Decrement node reference count and free if zero
 * @param node Node to unreference
 */
void vfs_unref_node(vfs_node_t *node);

/**
 * Get last error code
 * @return Last VFS error code
 */
int vfs_get_error(void);

/**
 * Convert VFS error code to string
 * @param error Error code
 * @return Error string
 */
const char* vfs_strerror(int error);

#endif /* _AAAOS_VFS_H */
