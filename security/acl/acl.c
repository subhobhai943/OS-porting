/**
 * AAAos Access Control List (ACL) System - Implementation
 *
 * This module implements Unix-style file permissions and access control
 * for the AAAos kernel. It provides functions for checking and modifying
 * file access rights based on user/group ownership.
 */

#include "acl.h"
#include "../../kernel/include/serial.h"

/*============================================================================
 * Private State
 *============================================================================*/

static bool acl_initialized = false;

/*============================================================================
 * Private Helper Functions
 *============================================================================*/

/**
 * Extract permission bits for a specific class from mode
 *
 * @param mode  Full permission mode
 * @param shift Shift value (ACL_OWNER_SHIFT, ACL_GROUP_SHIFT, or ACL_OTHER_SHIFT)
 * @return Permission bits (0-7) for the specified class
 */
static inline uint8_t acl_extract_perms(uint32_t mode, int shift)
{
    return (mode >> shift) & 0x7;
}

/**
 * Check if the requested access is granted by the given permission bits
 *
 * @param perms  Permission bits for a class (0-7)
 * @param access Requested access (ACL_READ, ACL_WRITE, ACL_EXEC, or combination)
 * @return true if all requested access types are allowed
 */
static inline bool acl_perms_allow(uint8_t perms, int access)
{
    return (perms & access) == access;
}

/*============================================================================
 * Public API Implementation
 *============================================================================*/

/**
 * Initialize the ACL subsystem
 */
int acl_init(void)
{
    if (acl_initialized) {
        kprintf("[ACL] Warning: ACL subsystem already initialized\n");
        return ACL_OK;
    }

    kprintf("[ACL] Initializing Access Control List subsystem...\n");

    /* No additional initialization needed for basic ACL */
    acl_initialized = true;

    kprintf("[ACL] ACL subsystem initialized successfully\n");
    return ACL_OK;
}

/**
 * Check if a user has the specified access rights to a file
 */
int acl_check_access(vfs_node_t *node, uint32_t uid, uint32_t gid, int access)
{
    uint8_t perms;

    /* Validate input */
    if (node == NULL) {
        kprintf("[ACL] Error: NULL node in acl_check_access\n");
        return ACL_ERR_INVAL;
    }

    /* Validate access parameter */
    if ((access & ~(ACL_READ | ACL_WRITE | ACL_EXEC)) != 0) {
        kprintf("[ACL] Error: Invalid access flags 0x%x\n", access);
        return ACL_ERR_INVAL;
    }

    /* Root (uid 0) has full access to everything */
    if (uid == ACL_ROOT_UID) {
        return ACL_OK;
    }

    /* Get the file's permission mode */
    uint32_t mode = node->permissions;

    /* Determine which permission class applies */
    if (uid == node->uid) {
        /* User is the owner - use owner permissions */
        perms = acl_extract_perms(mode, ACL_OWNER_SHIFT);
    } else if (gid == node->gid) {
        /* User is in the file's group - use group permissions */
        perms = acl_extract_perms(mode, ACL_GROUP_SHIFT);
    } else {
        /* User is neither owner nor in group - use other permissions */
        perms = acl_extract_perms(mode, ACL_OTHER_SHIFT);
    }

    /* Check if the requested access is granted */
    if (acl_perms_allow(perms, access)) {
        return ACL_OK;
    }

    kprintf("[ACL] Access denied: uid=%u, gid=%u, node_uid=%u, node_gid=%u, "
            "mode=0%o, requested=0x%x\n",
            uid, gid, node->uid, node->gid, mode, access);

    return ACL_ERR_DENIED;
}

/**
 * Get the permission mode for a VFS node
 */
uint16_t acl_get_permissions(vfs_node_t *node)
{
    if (node == NULL) {
        kprintf("[ACL] Error: NULL node in acl_get_permissions\n");
        return 0;
    }

    /* Return only the permission bits (lower 12 bits for mode + special) */
    return (uint16_t)(node->permissions & 0xFFF);
}

/**
 * Set the permission mode for a VFS node (chmod equivalent)
 */
int acl_set_permissions(vfs_node_t *node, uint16_t mode)
{
    if (node == NULL) {
        kprintf("[ACL] Error: NULL node in acl_set_permissions\n");
        return ACL_ERR_INVAL;
    }

    /* Mask to valid permission bits only (12 bits: special + rwxrwxrwx) */
    uint16_t valid_mode = mode & 0xFFF;

    kprintf("[ACL] Setting permissions on '%s': 0%o -> 0%o\n",
            node->name, node->permissions & 0xFFF, valid_mode);

    /* Update the permission bits, preserving file type bits if any */
    node->permissions = (node->permissions & ~0xFFF) | valid_mode;

    /* Mark node as dirty so changes are persisted */
    node->dirty = true;

    return ACL_OK;
}

/**
 * Set the owner and group of a VFS node (chown equivalent)
 */
int acl_set_owner(vfs_node_t *node, uint32_t uid, uint32_t gid)
{
    if (node == NULL) {
        kprintf("[ACL] Error: NULL node in acl_set_owner\n");
        return ACL_ERR_INVAL;
    }

    kprintf("[ACL] Changing ownership of '%s': uid %u->%u, gid %u->%u\n",
            node->name, node->uid, uid, node->gid, gid);

    /* Update owner and group */
    node->uid = uid;
    node->gid = gid;

    /* Clear setuid/setgid bits on ownership change (security measure) */
    node->permissions &= ~(ACL_SETUID | ACL_SETGID);

    /* Mark node as dirty so changes are persisted */
    node->dirty = true;

    return ACL_OK;
}

/**
 * Check if a user can read from a file
 */
bool acl_can_read(vfs_node_t *node, uint32_t uid, uint32_t gid)
{
    return acl_check_access(node, uid, gid, ACL_READ) == ACL_OK;
}

/**
 * Check if a user can write to a file
 */
bool acl_can_write(vfs_node_t *node, uint32_t uid, uint32_t gid)
{
    return acl_check_access(node, uid, gid, ACL_WRITE) == ACL_OK;
}

/**
 * Check if a user can execute a file
 */
bool acl_can_exec(vfs_node_t *node, uint32_t uid, uint32_t gid)
{
    return acl_check_access(node, uid, gid, ACL_EXEC) == ACL_OK;
}

/*============================================================================
 * Utility Functions Implementation
 *============================================================================*/

/**
 * Convert a numeric mode to a file_perm_t structure
 */
file_perm_t acl_mode_to_perm(uint16_t mode)
{
    file_perm_t perm;

    /* Clear all bits first */
    perm.reserved = 0;

    /* Owner permissions */
    perm.owner_r = (mode & ACL_S_IRUSR) ? 1 : 0;
    perm.owner_w = (mode & ACL_S_IWUSR) ? 1 : 0;
    perm.owner_x = (mode & ACL_S_IXUSR) ? 1 : 0;

    /* Group permissions */
    perm.group_r = (mode & ACL_S_IRGRP) ? 1 : 0;
    perm.group_w = (mode & ACL_S_IWGRP) ? 1 : 0;
    perm.group_x = (mode & ACL_S_IXGRP) ? 1 : 0;

    /* Other permissions */
    perm.other_r = (mode & ACL_S_IROTH) ? 1 : 0;
    perm.other_w = (mode & ACL_S_IWOTH) ? 1 : 0;
    perm.other_x = (mode & ACL_S_IXOTH) ? 1 : 0;

    /* Special bits */
    perm.setuid = (mode & ACL_SETUID) ? 1 : 0;
    perm.setgid = (mode & ACL_SETGID) ? 1 : 0;
    perm.sticky = (mode & ACL_STICKY) ? 1 : 0;

    return perm;
}

/**
 * Convert a file_perm_t structure to a numeric mode
 */
uint16_t acl_perm_to_mode(file_perm_t perm)
{
    uint16_t mode = 0;

    /* Owner permissions */
    if (perm.owner_r) mode |= ACL_S_IRUSR;
    if (perm.owner_w) mode |= ACL_S_IWUSR;
    if (perm.owner_x) mode |= ACL_S_IXUSR;

    /* Group permissions */
    if (perm.group_r) mode |= ACL_S_IRGRP;
    if (perm.group_w) mode |= ACL_S_IWGRP;
    if (perm.group_x) mode |= ACL_S_IXGRP;

    /* Other permissions */
    if (perm.other_r) mode |= ACL_S_IROTH;
    if (perm.other_w) mode |= ACL_S_IWOTH;
    if (perm.other_x) mode |= ACL_S_IXOTH;

    /* Special bits */
    if (perm.setuid) mode |= ACL_SETUID;
    if (perm.setgid) mode |= ACL_SETGID;
    if (perm.sticky) mode |= ACL_STICKY;

    return mode;
}

/**
 * Create a new ACL entry with the specified parameters
 */
file_acl_t acl_create(uint32_t uid, uint32_t gid, uint16_t mode)
{
    file_acl_t acl;

    acl.owner_uid = uid;
    acl.owner_gid = gid;
    acl.permissions = acl_mode_to_perm(mode);

    return acl;
}

/**
 * Format permission mode as a string (e.g., "rwxr-xr-x")
 */
char* acl_mode_to_string(uint16_t mode, char *buf)
{
    if (buf == NULL) {
        return NULL;
    }

    /* Owner permissions */
    buf[0] = (mode & ACL_S_IRUSR) ? 'r' : '-';
    buf[1] = (mode & ACL_S_IWUSR) ? 'w' : '-';
    if (mode & ACL_SETUID) {
        buf[2] = (mode & ACL_S_IXUSR) ? 's' : 'S';
    } else {
        buf[2] = (mode & ACL_S_IXUSR) ? 'x' : '-';
    }

    /* Group permissions */
    buf[3] = (mode & ACL_S_IRGRP) ? 'r' : '-';
    buf[4] = (mode & ACL_S_IWGRP) ? 'w' : '-';
    if (mode & ACL_SETGID) {
        buf[5] = (mode & ACL_S_IXGRP) ? 's' : 'S';
    } else {
        buf[5] = (mode & ACL_S_IXGRP) ? 'x' : '-';
    }

    /* Other permissions */
    buf[6] = (mode & ACL_S_IROTH) ? 'r' : '-';
    buf[7] = (mode & ACL_S_IWOTH) ? 'w' : '-';
    if (mode & ACL_STICKY) {
        buf[8] = (mode & ACL_S_IXOTH) ? 't' : 'T';
    } else {
        buf[8] = (mode & ACL_S_IXOTH) ? 'x' : '-';
    }

    buf[9] = '\0';

    return buf;
}

/**
 * Check if setuid bit is set
 */
bool acl_has_setuid(vfs_node_t *node)
{
    if (node == NULL) {
        return false;
    }
    return (node->permissions & ACL_SETUID) != 0;
}

/**
 * Check if setgid bit is set
 */
bool acl_has_setgid(vfs_node_t *node)
{
    if (node == NULL) {
        return false;
    }
    return (node->permissions & ACL_SETGID) != 0;
}

/**
 * Check if sticky bit is set
 */
bool acl_has_sticky(vfs_node_t *node)
{
    if (node == NULL) {
        return false;
    }
    return (node->permissions & ACL_STICKY) != 0;
}
