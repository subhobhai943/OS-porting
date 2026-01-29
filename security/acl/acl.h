/**
 * AAAos Access Control List (ACL) System
 *
 * This header defines the file permissions and access control interface
 * for AAAos. It implements Unix-style permissions (rwx for owner/group/other)
 * and provides functions for checking and modifying file access rights.
 */

#ifndef _AAAOS_ACL_H
#define _AAAOS_ACL_H

#include "../../kernel/include/types.h"
#include "../../fs/vfs/vfs.h"

/*============================================================================
 * Permission Constants
 *============================================================================*/

/* Basic permission bits */
#define ACL_READ            0x4     /* Read permission (r) */
#define ACL_WRITE           0x2     /* Write permission (w) */
#define ACL_EXEC            0x1     /* Execute permission (x) */

/* Permission shift values for different classes */
#define ACL_OWNER_SHIFT     6       /* Shift for owner permissions */
#define ACL_GROUP_SHIFT     3       /* Shift for group permissions */
#define ACL_OTHER_SHIFT     0       /* Shift for other permissions */

/* Combined permission masks */
#define ACL_OWNER_MASK      (0x7 << ACL_OWNER_SHIFT)    /* Owner permission mask (0700) */
#define ACL_GROUP_MASK      (0x7 << ACL_GROUP_SHIFT)    /* Group permission mask (0070) */
#define ACL_OTHER_MASK      (0x7 << ACL_OTHER_SHIFT)    /* Other permission mask (0007) */

/* Special permission bits */
#define ACL_SETUID          0x800   /* Set user ID on execution */
#define ACL_SETGID          0x400   /* Set group ID on execution */
#define ACL_STICKY          0x200   /* Sticky bit */

/* Special permission shift */
#define ACL_SPECIAL_SHIFT   9

/* Owner permissions (for mode) */
#define ACL_S_IRUSR         (ACL_READ << ACL_OWNER_SHIFT)   /* 0400 - Owner read */
#define ACL_S_IWUSR         (ACL_WRITE << ACL_OWNER_SHIFT)  /* 0200 - Owner write */
#define ACL_S_IXUSR         (ACL_EXEC << ACL_OWNER_SHIFT)   /* 0100 - Owner execute */

/* Group permissions (for mode) */
#define ACL_S_IRGRP         (ACL_READ << ACL_GROUP_SHIFT)   /* 0040 - Group read */
#define ACL_S_IWGRP         (ACL_WRITE << ACL_GROUP_SHIFT)  /* 0020 - Group write */
#define ACL_S_IXGRP         (ACL_EXEC << ACL_GROUP_SHIFT)   /* 0010 - Group execute */

/* Other permissions (for mode) */
#define ACL_S_IROTH         (ACL_READ << ACL_OTHER_SHIFT)   /* 0004 - Other read */
#define ACL_S_IWOTH         (ACL_WRITE << ACL_OTHER_SHIFT)  /* 0002 - Other write */
#define ACL_S_IXOTH         (ACL_EXEC << ACL_OTHER_SHIFT)   /* 0001 - Other execute */

/* Common permission combinations */
#define ACL_MODE_FULL       0777    /* rwxrwxrwx */
#define ACL_MODE_DIR        0755    /* rwxr-xr-x (typical directory) */
#define ACL_MODE_FILE       0644    /* rw-r--r-- (typical file) */
#define ACL_MODE_EXEC       0755    /* rwxr-xr-x (typical executable) */
#define ACL_MODE_PRIVATE    0600    /* rw------- (private file) */

/* Root user ID */
#define ACL_ROOT_UID        0
#define ACL_ROOT_GID        0

/* Access types for acl_check_access() */
#define ACL_ACCESS_READ     ACL_READ
#define ACL_ACCESS_WRITE    ACL_WRITE
#define ACL_ACCESS_EXEC     ACL_EXEC

/* Error codes */
#define ACL_OK              0       /* Success */
#define ACL_ERR_DENIED      (-1)    /* Permission denied */
#define ACL_ERR_INVAL       (-2)    /* Invalid argument */
#define ACL_ERR_NOENT       (-3)    /* No such entry */

/*============================================================================
 * Data Structures
 *============================================================================*/

/**
 * File permission bits structure
 * Represents Unix-style permission bits for a file or directory
 */
typedef struct file_perm {
    /* Owner permissions */
    uint8_t owner_r : 1;    /* Owner read permission */
    uint8_t owner_w : 1;    /* Owner write permission */
    uint8_t owner_x : 1;    /* Owner execute permission */

    /* Group permissions */
    uint8_t group_r : 1;    /* Group read permission */
    uint8_t group_w : 1;    /* Group write permission */
    uint8_t group_x : 1;    /* Group execute permission */

    /* Other permissions */
    uint8_t other_r : 1;    /* Other read permission */
    uint8_t other_w : 1;    /* Other write permission */
    uint8_t other_x : 1;    /* Other execute permission */

    /* Special bits */
    uint8_t setuid : 1;     /* Set user ID on execution */
    uint8_t setgid : 1;     /* Set group ID on execution */
    uint8_t sticky : 1;     /* Sticky bit (restricted deletion) */

    uint8_t reserved : 4;   /* Reserved for future use */
} PACKED file_perm_t;

/**
 * Full ACL entry structure
 * Contains owner/group IDs and permission bits for a file
 */
typedef struct file_acl {
    uint32_t owner_uid;     /* Owner user ID */
    uint32_t owner_gid;     /* Owner group ID */
    file_perm_t permissions; /* Permission bits */
} file_acl_t;

/*============================================================================
 * Public API Functions
 *============================================================================*/

/**
 * Initialize the ACL subsystem
 * Must be called during kernel initialization before any ACL operations.
 *
 * @return ACL_OK on success, error code on failure
 */
int acl_init(void);

/**
 * Check if a user has the specified access rights to a file
 * This is the main access control function that should be called
 * before any file operation.
 *
 * @param node   VFS node to check access for
 * @param uid    User ID of the requesting process
 * @param gid    Group ID of the requesting process
 * @param access Access type to check (ACL_READ, ACL_WRITE, ACL_EXEC, or combination)
 * @return ACL_OK (0) if access is allowed, ACL_ERR_DENIED if denied
 */
int acl_check_access(vfs_node_t *node, uint32_t uid, uint32_t gid, int access);

/**
 * Get the permission mode for a VFS node
 *
 * @param node VFS node to get permissions for
 * @return Permission mode (Unix-style, e.g., 0755), or 0 on error
 */
uint16_t acl_get_permissions(vfs_node_t *node);

/**
 * Set the permission mode for a VFS node (chmod equivalent)
 * Only the owner or root can change permissions.
 *
 * @param node VFS node to modify
 * @param mode New permission mode (Unix-style, e.g., 0755)
 * @return ACL_OK on success, error code on failure
 */
int acl_set_permissions(vfs_node_t *node, uint16_t mode);

/**
 * Set the owner and group of a VFS node (chown equivalent)
 * Only root can change ownership. Owner can change group to a group they belong to.
 *
 * @param node VFS node to modify
 * @param uid  New owner user ID
 * @param gid  New owner group ID
 * @return ACL_OK on success, error code on failure
 */
int acl_set_owner(vfs_node_t *node, uint32_t uid, uint32_t gid);

/**
 * Check if a user can read from a file
 *
 * @param node VFS node to check
 * @param uid  User ID of the requesting process
 * @param gid  Group ID of the requesting process
 * @return true if read access is allowed, false otherwise
 */
bool acl_can_read(vfs_node_t *node, uint32_t uid, uint32_t gid);

/**
 * Check if a user can write to a file
 *
 * @param node VFS node to check
 * @param uid  User ID of the requesting process
 * @param gid  Group ID of the requesting process
 * @return true if write access is allowed, false otherwise
 */
bool acl_can_write(vfs_node_t *node, uint32_t uid, uint32_t gid);

/**
 * Check if a user can execute a file
 *
 * @param node VFS node to check
 * @param uid  User ID of the requesting process
 * @param gid  Group ID of the requesting process
 * @return true if execute access is allowed, false otherwise
 */
bool acl_can_exec(vfs_node_t *node, uint32_t uid, uint32_t gid);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Convert a numeric mode to a file_perm_t structure
 *
 * @param mode Numeric permission mode (e.g., 0755)
 * @return file_perm_t structure with corresponding bits set
 */
file_perm_t acl_mode_to_perm(uint16_t mode);

/**
 * Convert a file_perm_t structure to a numeric mode
 *
 * @param perm file_perm_t structure
 * @return Numeric permission mode (e.g., 0755)
 */
uint16_t acl_perm_to_mode(file_perm_t perm);

/**
 * Create a new ACL entry with the specified parameters
 *
 * @param uid  Owner user ID
 * @param gid  Owner group ID
 * @param mode Permission mode
 * @return Initialized file_acl_t structure
 */
file_acl_t acl_create(uint32_t uid, uint32_t gid, uint16_t mode);

/**
 * Format permission mode as a string (e.g., "rwxr-xr-x")
 *
 * @param mode Permission mode
 * @param buf  Buffer to store the string (must be at least 10 bytes)
 * @return Pointer to the buffer
 */
char* acl_mode_to_string(uint16_t mode, char *buf);

/**
 * Check if special bits are set
 */
bool acl_has_setuid(vfs_node_t *node);
bool acl_has_setgid(vfs_node_t *node);
bool acl_has_sticky(vfs_node_t *node);

#endif /* _AAAOS_ACL_H */
