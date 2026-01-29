/**
 * AAAos Virtual File System (VFS) Layer Implementation
 *
 * This file implements the abstract file system interface that provides
 * unified access to all filesystem types in the system.
 */

#include "vfs.h"
#include "../../kernel/include/serial.h"

/*============================================================================
 * String utility functions (minimal implementations for kernel use)
 *============================================================================*/

static size_t vfs_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int vfs_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int vfs_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static char* vfs_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char* vfs_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

static void* vfs_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

static void* vfs_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/*============================================================================
 * Global VFS state
 *============================================================================*/

/* Root mount point */
static vfs_mount_t *vfs_root_mount = NULL;

/* Array of all mount points */
static vfs_mount_t vfs_mounts[VFS_MAX_MOUNTS];
static uint32_t vfs_mount_count = 0;

/* Array of open files */
static vfs_file_t vfs_open_files[VFS_MAX_OPEN_FILES];

/* Array of open directories */
#define VFS_MAX_OPEN_DIRS 256
static vfs_dir_t vfs_open_dirs[VFS_MAX_OPEN_DIRS];

/* Registered filesystem types */
static vfs_fstype_t *vfs_fs_types = NULL;

/* Last error code */
static int vfs_last_error = VFS_OK;

/* VFS initialized flag */
static bool vfs_initialized = false;

/* Static directory entry for readdir */
static vfs_dirent_t vfs_dirent_buf;

/*============================================================================
 * Error handling
 *============================================================================*/

static void vfs_set_error(int error) {
    vfs_last_error = error;
}

int vfs_get_error(void) {
    return vfs_last_error;
}

const char* vfs_strerror(int error) {
    switch (error) {
        case VFS_OK:            return "Success";
        case VFS_ERR_NOENT:     return "No such file or directory";
        case VFS_ERR_IO:        return "I/O error";
        case VFS_ERR_NXIO:      return "No such device or address";
        case VFS_ERR_BADF:      return "Bad file descriptor";
        case VFS_ERR_NOMEM:     return "Out of memory";
        case VFS_ERR_ACCES:     return "Permission denied";
        case VFS_ERR_EXIST:     return "File exists";
        case VFS_ERR_NOTDIR:    return "Not a directory";
        case VFS_ERR_ISDIR:     return "Is a directory";
        case VFS_ERR_INVAL:     return "Invalid argument";
        case VFS_ERR_NFILE:     return "Too many open files";
        case VFS_ERR_FBIG:      return "File too large";
        case VFS_ERR_NOSPC:     return "No space left on device";
        case VFS_ERR_ROFS:      return "Read-only filesystem";
        case VFS_ERR_NAMETOOLONG: return "Filename too long";
        case VFS_ERR_NOTEMPTY:  return "Directory not empty";
        case VFS_ERR_NOSYS:     return "Function not implemented";
        case VFS_ERR_BUSY:      return "Device or resource busy";
        default:                return "Unknown error";
    }
}

/*============================================================================
 * Path utilities
 *============================================================================*/

/**
 * Normalize a path by removing . and .. components
 * Returns static buffer - not thread-safe
 */
static char* vfs_normalize_path(const char *path) {
    static char normalized[VFS_PATH_MAX];
    char *components[VFS_PATH_MAX / 2];
    int depth = 0;
    size_t len;
    size_t i, j;

    if (!path || !path[0]) {
        normalized[0] = '/';
        normalized[1] = '\0';
        return normalized;
    }

    len = vfs_strlen(path);
    if (len >= VFS_PATH_MAX) {
        return NULL;
    }

    /* Copy path for tokenization */
    static char temp[VFS_PATH_MAX];
    vfs_strcpy(temp, path);

    /* Parse path components */
    char *token = temp;
    while (*token) {
        /* Skip leading slashes */
        while (*token == '/') token++;
        if (!*token) break;

        /* Find end of component */
        char *end = token;
        while (*end && *end != '/') end++;

        /* Null-terminate component */
        bool more = (*end != '\0');
        *end = '\0';

        /* Process component */
        if (vfs_strcmp(token, ".") == 0) {
            /* Skip . */
        } else if (vfs_strcmp(token, "..") == 0) {
            /* Go up one level */
            if (depth > 0) depth--;
        } else {
            /* Add component */
            components[depth++] = token;
        }

        if (!more) break;
        token = end + 1;
    }

    /* Build normalized path */
    normalized[0] = '/';
    j = 1;
    for (i = 0; i < (size_t)depth; i++) {
        len = vfs_strlen(components[i]);
        if (j + len + 1 >= VFS_PATH_MAX) {
            return NULL;
        }
        vfs_strcpy(&normalized[j], components[i]);
        j += len;
        if (i < (size_t)(depth - 1)) {
            normalized[j++] = '/';
        }
    }
    normalized[j] = '\0';

    return normalized;
}

/**
 * Get parent directory path
 * Returns static buffer - not thread-safe
 */
static char* vfs_parent_path(const char *path) {
    static char parent[VFS_PATH_MAX];
    size_t len;

    if (!path) return NULL;

    len = vfs_strlen(path);
    if (len == 0 || (len == 1 && path[0] == '/')) {
        parent[0] = '/';
        parent[1] = '\0';
        return parent;
    }

    vfs_strcpy(parent, path);

    /* Remove trailing slash */
    if (parent[len - 1] == '/') {
        parent[--len] = '\0';
    }

    /* Find last slash */
    while (len > 0 && parent[len - 1] != '/') {
        len--;
    }

    if (len == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else if (len == 1) {
        parent[1] = '\0';
    } else {
        parent[len - 1] = '\0';
    }

    return parent;
}

/**
 * Get basename (filename) from path
 * Returns pointer into original string
 */
static const char* vfs_basename(const char *path) {
    const char *base;
    size_t len;

    if (!path) return NULL;

    len = vfs_strlen(path);
    if (len == 0) return path;

    base = path + len - 1;

    /* Skip trailing slash */
    if (*base == '/' && base > path) base--;

    /* Find start of basename */
    while (base > path && *(base - 1) != '/') {
        base--;
    }

    return base;
}

/*============================================================================
 * Filesystem type management
 *============================================================================*/

int vfs_register_fs(const char *name, vfs_ops_t *ops) {
    vfs_fstype_t *fstype;
    vfs_fstype_t *current;

    if (!name || !ops) {
        kprintf("[VFS] register_fs: Invalid arguments\n");
        return VFS_ERR_INVAL;
    }

    /* Check if already registered */
    current = vfs_fs_types;
    while (current) {
        if (vfs_strcmp(current->name, name) == 0) {
            kprintf("[VFS] register_fs: Filesystem '%s' already registered\n", name);
            return VFS_ERR_EXIST;
        }
        current = current->next;
    }

    /* Allocate new fstype structure */
    /* Note: In a real kernel, this would use kmalloc */
    static vfs_fstype_t fstype_pool[16];
    static int fstype_count = 0;

    if (fstype_count >= 16) {
        kprintf("[VFS] register_fs: Too many filesystem types\n");
        return VFS_ERR_NOMEM;
    }

    fstype = &fstype_pool[fstype_count++];
    vfs_memset(fstype, 0, sizeof(*fstype));
    vfs_strncpy(fstype->name, name, sizeof(fstype->name) - 1);
    fstype->ops = ops;
    fstype->next = vfs_fs_types;
    vfs_fs_types = fstype;

    kprintf("[VFS] Registered filesystem type: %s\n", name);
    return VFS_OK;
}

int vfs_unregister_fs(const char *name) {
    vfs_fstype_t *current;
    vfs_fstype_t *prev = NULL;

    if (!name) {
        return VFS_ERR_INVAL;
    }

    current = vfs_fs_types;
    while (current) {
        if (vfs_strcmp(current->name, name) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                vfs_fs_types = current->next;
            }
            kprintf("[VFS] Unregistered filesystem type: %s\n", name);
            return VFS_OK;
        }
        prev = current;
        current = current->next;
    }

    kprintf("[VFS] unregister_fs: Filesystem '%s' not found\n", name);
    return VFS_ERR_NOENT;
}

/**
 * Find a registered filesystem type by name
 */
static vfs_fstype_t* vfs_find_fstype(const char *name) {
    vfs_fstype_t *current = vfs_fs_types;

    while (current) {
        if (vfs_strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

/*============================================================================
 * Node management
 *============================================================================*/

/* Node pool for allocation */
#define VFS_NODE_POOL_SIZE 1024
static vfs_node_t vfs_node_pool[VFS_NODE_POOL_SIZE];
static bool vfs_node_used[VFS_NODE_POOL_SIZE];

vfs_node_t* vfs_alloc_node(void) {
    for (int i = 0; i < VFS_NODE_POOL_SIZE; i++) {
        if (!vfs_node_used[i]) {
            vfs_node_used[i] = true;
            vfs_memset(&vfs_node_pool[i], 0, sizeof(vfs_node_t));
            vfs_node_pool[i].ref_count = 1;
            return &vfs_node_pool[i];
        }
    }
    kprintf("[VFS] alloc_node: Out of nodes\n");
    return NULL;
}

void vfs_free_node(vfs_node_t *node) {
    if (!node) return;

    for (int i = 0; i < VFS_NODE_POOL_SIZE; i++) {
        if (&vfs_node_pool[i] == node) {
            vfs_node_used[i] = false;
            return;
        }
    }
}

void vfs_ref_node(vfs_node_t *node) {
    if (node) {
        node->ref_count++;
    }
}

void vfs_unref_node(vfs_node_t *node) {
    if (!node) return;

    if (node->ref_count > 0) {
        node->ref_count--;
    }

    if (node->ref_count == 0) {
        vfs_free_node(node);
    }
}

/*============================================================================
 * Mount management
 *============================================================================*/

vfs_mount_t* vfs_get_mount(const char *path) {
    char *normalized;
    size_t best_len = 0;
    vfs_mount_t *best_mount = NULL;

    if (!path) return NULL;

    normalized = vfs_normalize_path(path);
    if (!normalized) return NULL;

    /* Find the longest matching mount point */
    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        if (!vfs_mounts[i].active) continue;

        size_t mount_len = vfs_strlen(vfs_mounts[i].path);

        /* Check if this mount point is a prefix of the path */
        if (vfs_strncmp(vfs_mounts[i].path, normalized, mount_len) == 0) {
            /* Must be exact match or followed by slash */
            if (normalized[mount_len] == '\0' ||
                normalized[mount_len] == '/' ||
                mount_len == 1) {  /* Root mount */
                if (mount_len > best_len) {
                    best_len = mount_len;
                    best_mount = &vfs_mounts[i];
                }
            }
        }
    }

    return best_mount;
}

int vfs_mount(const char *path, const char *type, void *device) {
    vfs_mount_t *mount;
    vfs_fstype_t *fstype;
    char *normalized;
    int result;

    kprintf("[VFS] Mounting %s filesystem at %s\n", type, path);

    if (!path || !type) {
        kprintf("[VFS] mount: Invalid arguments\n");
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Normalize the path */
    normalized = vfs_normalize_path(path);
    if (!normalized) {
        kprintf("[VFS] mount: Path too long\n");
        vfs_set_error(VFS_ERR_NAMETOOLONG);
        return VFS_ERR_NAMETOOLONG;
    }

    /* Find the filesystem type */
    fstype = vfs_find_fstype(type);
    if (!fstype) {
        kprintf("[VFS] mount: Unknown filesystem type '%s'\n", type);
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    /* Check if already mounted at this path */
    for (uint32_t i = 0; i < vfs_mount_count; i++) {
        if (vfs_mounts[i].active && vfs_strcmp(vfs_mounts[i].path, normalized) == 0) {
            kprintf("[VFS] mount: Already mounted at %s\n", normalized);
            vfs_set_error(VFS_ERR_BUSY);
            return VFS_ERR_BUSY;
        }
    }

    /* Find a free mount slot */
    mount = NULL;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!vfs_mounts[i].active) {
            mount = &vfs_mounts[i];
            break;
        }
    }

    if (!mount) {
        kprintf("[VFS] mount: Too many mounted filesystems\n");
        vfs_set_error(VFS_ERR_NOMEM);
        return VFS_ERR_NOMEM;
    }

    /* Initialize mount structure */
    vfs_memset(mount, 0, sizeof(*mount));
    vfs_strcpy(mount->path, normalized);
    vfs_strncpy(mount->type, type, sizeof(mount->type) - 1);
    mount->ops = fstype->ops;
    mount->device = device;
    mount->active = true;

    /* Call filesystem-specific mount */
    if (mount->ops && mount->ops->mount) {
        result = mount->ops->mount(mount, device);
        if (result != VFS_OK) {
            kprintf("[VFS] mount: Filesystem mount failed: %s\n", vfs_strerror(result));
            mount->active = false;
            vfs_set_error(result);
            return result;
        }
    }

    vfs_mount_count++;

    /* If this is root mount, set it as the root */
    if (vfs_strcmp(normalized, "/") == 0) {
        vfs_root_mount = mount;
        kprintf("[VFS] Root filesystem mounted\n");
    }

    kprintf("[VFS] Successfully mounted %s at %s\n", type, normalized);
    return VFS_OK;
}

int vfs_unmount(const char *path) {
    vfs_mount_t *mount;
    char *normalized;
    int result;

    kprintf("[VFS] Unmounting %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    normalized = vfs_normalize_path(path);
    if (!normalized) {
        vfs_set_error(VFS_ERR_NAMETOOLONG);
        return VFS_ERR_NAMETOOLONG;
    }

    /* Find the mount */
    mount = NULL;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (vfs_mounts[i].active && vfs_strcmp(vfs_mounts[i].path, normalized) == 0) {
            mount = &vfs_mounts[i];
            break;
        }
    }

    if (!mount) {
        kprintf("[VFS] unmount: Not mounted at %s\n", normalized);
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    /* Check if any files are open on this mount */
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i].in_use && vfs_open_files[i].node &&
            vfs_open_files[i].node->mount == mount) {
            kprintf("[VFS] unmount: Filesystem busy (open files)\n");
            vfs_set_error(VFS_ERR_BUSY);
            return VFS_ERR_BUSY;
        }
    }

    /* Call filesystem-specific unmount */
    if (mount->ops && mount->ops->unmount) {
        result = mount->ops->unmount(mount);
        if (result != VFS_OK) {
            kprintf("[VFS] unmount: Filesystem unmount failed: %s\n", vfs_strerror(result));
            vfs_set_error(result);
            return result;
        }
    }

    /* Clear root mount if unmounting root */
    if (mount == vfs_root_mount) {
        vfs_root_mount = NULL;
    }

    /* Mark mount as inactive */
    mount->active = false;
    vfs_mount_count--;

    kprintf("[VFS] Successfully unmounted %s\n", normalized);
    return VFS_OK;
}

/*============================================================================
 * Path resolution
 *============================================================================*/

vfs_node_t* vfs_lookup(const char *path) {
    vfs_mount_t *mount;
    vfs_node_t *node;
    vfs_node_t *next;
    char *normalized;
    char *component;
    char path_copy[VFS_PATH_MAX];

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return NULL;
    }

    normalized = vfs_normalize_path(path);
    if (!normalized) {
        vfs_set_error(VFS_ERR_NAMETOOLONG);
        return NULL;
    }

    /* Find the mount point for this path */
    mount = vfs_get_mount(normalized);
    if (!mount) {
        vfs_set_error(VFS_ERR_NOENT);
        return NULL;
    }

    /* Start from mount root */
    node = mount->root;
    if (!node) {
        vfs_set_error(VFS_ERR_NOENT);
        return NULL;
    }

    /* Handle root path */
    if (vfs_strcmp(normalized, "/") == 0 || vfs_strcmp(normalized, mount->path) == 0) {
        vfs_ref_node(node);
        return node;
    }

    /* Skip the mount point path */
    const char *relative_path = normalized + vfs_strlen(mount->path);
    if (*relative_path == '/') relative_path++;
    if (*relative_path == '\0') {
        vfs_ref_node(node);
        return node;
    }

    /* Copy for tokenization */
    vfs_strcpy(path_copy, relative_path);

    /* Walk the path */
    vfs_ref_node(node);
    component = path_copy;

    while (*component) {
        /* Skip slashes */
        while (*component == '/') component++;
        if (!*component) break;

        /* Find end of component */
        char *end = component;
        while (*end && *end != '/') end++;
        bool more = (*end != '\0');
        *end = '\0';

        /* Must be a directory to traverse */
        if (node->type != VFS_NODE_DIRECTORY) {
            vfs_unref_node(node);
            vfs_set_error(VFS_ERR_NOTDIR);
            return NULL;
        }

        /* Look up the component */
        if (mount->ops && mount->ops->finddir) {
            next = mount->ops->finddir(node, component);
        } else {
            next = NULL;
        }

        vfs_unref_node(node);

        if (!next) {
            vfs_set_error(VFS_ERR_NOENT);
            return NULL;
        }

        /* Check for mount point */
        if (next->type == VFS_NODE_MOUNTPOINT) {
            /* Find mount at this location and switch to its root */
            /* For now, just continue with the node */
        }

        node = next;

        if (!more) break;
        component = end + 1;
    }

    return node;
}

/*============================================================================
 * File operations
 *============================================================================*/

vfs_file_t* vfs_open(const char *path, int flags) {
    vfs_node_t *node;
    vfs_file_t *file;
    vfs_mount_t *mount;
    int result;

    kprintf("[VFS] Opening file: %s (flags=0x%x)\n", path, flags);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return NULL;
    }

    /* Look up the node */
    node = vfs_lookup(path);

    /* Handle creation */
    if (!node && (flags & VFS_O_CREAT)) {
        char *parent = vfs_parent_path(path);
        const char *name = vfs_basename(path);
        vfs_node_t *parent_node;

        parent_node = vfs_lookup(parent);
        if (!parent_node) {
            kprintf("[VFS] open: Parent directory not found\n");
            vfs_set_error(VFS_ERR_NOENT);
            return NULL;
        }

        mount = parent_node->mount;
        if (mount && mount->ops && mount->ops->create) {
            result = mount->ops->create(parent_node, name, VFS_S_IRUSR | VFS_S_IWUSR);
            vfs_unref_node(parent_node);

            if (result != VFS_OK) {
                kprintf("[VFS] open: Failed to create file: %s\n", vfs_strerror(result));
                vfs_set_error(result);
                return NULL;
            }

            /* Look up the newly created node */
            node = vfs_lookup(path);
        } else {
            vfs_unref_node(parent_node);
            kprintf("[VFS] open: Create not supported\n");
            vfs_set_error(VFS_ERR_NOSYS);
            return NULL;
        }
    }

    if (!node) {
        kprintf("[VFS] open: File not found: %s\n", path);
        vfs_set_error(VFS_ERR_NOENT);
        return NULL;
    }

    /* Check if trying to open directory without O_DIRECTORY */
    if (node->type == VFS_NODE_DIRECTORY && !(flags & VFS_O_DIRECTORY)) {
        vfs_unref_node(node);
        kprintf("[VFS] open: Cannot open directory as file\n");
        vfs_set_error(VFS_ERR_ISDIR);
        return NULL;
    }

    /* Check for exclusive create */
    if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
        /* File already exists */
        vfs_unref_node(node);
        kprintf("[VFS] open: File already exists (O_EXCL)\n");
        vfs_set_error(VFS_ERR_EXIST);
        return NULL;
    }

    /* Find a free file slot */
    file = NULL;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!vfs_open_files[i].in_use) {
            file = &vfs_open_files[i];
            break;
        }
    }

    if (!file) {
        vfs_unref_node(node);
        kprintf("[VFS] open: Too many open files\n");
        vfs_set_error(VFS_ERR_NFILE);
        return NULL;
    }

    /* Initialize file structure */
    vfs_memset(file, 0, sizeof(*file));
    file->node = node;
    file->flags = flags;
    file->offset = 0;
    file->ref_count = 1;
    file->in_use = true;

    /* Call filesystem-specific open */
    mount = node->mount;
    if (mount && mount->ops && mount->ops->open) {
        result = mount->ops->open(node, flags);
        if (result != VFS_OK) {
            file->in_use = false;
            vfs_unref_node(node);
            kprintf("[VFS] open: Filesystem open failed: %s\n", vfs_strerror(result));
            vfs_set_error(result);
            return NULL;
        }
    }

    /* Handle truncate */
    if ((flags & VFS_O_TRUNC) && (flags & (VFS_O_WRONLY | VFS_O_RDWR))) {
        if (mount && mount->ops && mount->ops->truncate) {
            mount->ops->truncate(node, 0);
        }
        node->size = 0;
    }

    /* Handle append mode */
    if (flags & VFS_O_APPEND) {
        file->offset = node->size;
    }

    kprintf("[VFS] Opened file: %s (size=%llu)\n", path, node->size);
    return file;
}

int vfs_close(vfs_file_t *file) {
    vfs_mount_t *mount;
    int result = VFS_OK;

    if (!file || !file->in_use) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    kprintf("[VFS] Closing file\n");

    /* Decrement reference count */
    if (file->ref_count > 1) {
        file->ref_count--;
        return VFS_OK;
    }

    /* Call filesystem-specific close */
    if (file->node) {
        mount = file->node->mount;
        if (mount && mount->ops && mount->ops->close) {
            result = mount->ops->close(file->node);
        }
        vfs_unref_node(file->node);
    }

    /* Mark slot as free */
    file->in_use = false;
    file->node = NULL;

    return result;
}

ssize_t vfs_read(vfs_file_t *file, void *buf, size_t size) {
    vfs_mount_t *mount;
    ssize_t bytes_read;

    if (!file || !file->in_use || !buf) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    if (!file->node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    /* Check read permission */
    if ((file->flags & VFS_O_RDWR) == VFS_O_WRONLY) {
        vfs_set_error(VFS_ERR_ACCES);
        return VFS_ERR_ACCES;
    }

    /* Check for directory */
    if (file->node->type == VFS_NODE_DIRECTORY) {
        vfs_set_error(VFS_ERR_ISDIR);
        return VFS_ERR_ISDIR;
    }

    /* Check for EOF */
    if (file->offset >= file->node->size) {
        return 0;
    }

    /* Limit read to remaining bytes */
    if (file->offset + size > file->node->size) {
        size = file->node->size - file->offset;
    }

    /* Call filesystem-specific read */
    mount = file->node->mount;
    if (!mount || !mount->ops || !mount->ops->read) {
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    bytes_read = mount->ops->read(file->node, buf, size, file->offset);

    if (bytes_read > 0) {
        file->offset += bytes_read;
    }

    return bytes_read;
}

ssize_t vfs_write(vfs_file_t *file, const void *buf, size_t size) {
    vfs_mount_t *mount;
    ssize_t bytes_written;

    if (!file || !file->in_use || !buf) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    if (!file->node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    /* Check write permission */
    if ((file->flags & (VFS_O_WRONLY | VFS_O_RDWR)) == 0) {
        vfs_set_error(VFS_ERR_ACCES);
        return VFS_ERR_ACCES;
    }

    /* Check for directory */
    if (file->node->type == VFS_NODE_DIRECTORY) {
        vfs_set_error(VFS_ERR_ISDIR);
        return VFS_ERR_ISDIR;
    }

    /* Check for read-only filesystem */
    mount = file->node->mount;
    if (mount && mount->readonly) {
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    /* Handle append mode */
    if (file->flags & VFS_O_APPEND) {
        file->offset = file->node->size;
    }

    /* Call filesystem-specific write */
    if (!mount || !mount->ops || !mount->ops->write) {
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    bytes_written = mount->ops->write(file->node, buf, size, file->offset);

    if (bytes_written > 0) {
        file->offset += bytes_written;
        /* Update file size if we wrote past the end */
        if (file->offset > file->node->size) {
            file->node->size = file->offset;
        }
        file->node->dirty = true;
    }

    return bytes_written;
}

int64_t vfs_seek(vfs_file_t *file, int64_t offset, int whence) {
    int64_t new_offset;

    if (!file || !file->in_use) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    if (!file->node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    switch (whence) {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;
        case VFS_SEEK_CUR:
            new_offset = (int64_t)file->offset + offset;
            break;
        case VFS_SEEK_END:
            new_offset = (int64_t)file->node->size + offset;
            break;
        default:
            vfs_set_error(VFS_ERR_INVAL);
            return VFS_ERR_INVAL;
    }

    if (new_offset < 0) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    file->offset = (uint64_t)new_offset;
    return (int64_t)file->offset;
}

int64_t vfs_tell(vfs_file_t *file) {
    if (!file || !file->in_use) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }
    return (int64_t)file->offset;
}

int vfs_truncate(vfs_file_t *file, uint64_t size) {
    vfs_mount_t *mount;

    if (!file || !file->in_use || !file->node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    mount = file->node->mount;
    if (mount && mount->readonly) {
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    if (!mount || !mount->ops || !mount->ops->truncate) {
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    return mount->ops->truncate(file->node, size);
}

int vfs_sync(vfs_file_t *file) {
    vfs_mount_t *mount;

    if (!file || !file->in_use || !file->node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    mount = file->node->mount;
    if (!mount || !mount->ops || !mount->ops->sync) {
        return VFS_OK;  /* No sync operation is not an error */
    }

    return mount->ops->sync(file->node);
}

/*============================================================================
 * Directory operations
 *============================================================================*/

vfs_dir_t* vfs_opendir(const char *path) {
    vfs_node_t *node;
    vfs_dir_t *dir;

    kprintf("[VFS] Opening directory: %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return NULL;
    }

    /* Look up the node */
    node = vfs_lookup(path);
    if (!node) {
        kprintf("[VFS] opendir: Directory not found: %s\n", path);
        return NULL;
    }

    /* Check if it's a directory */
    if (node->type != VFS_NODE_DIRECTORY) {
        vfs_unref_node(node);
        kprintf("[VFS] opendir: Not a directory: %s\n", path);
        vfs_set_error(VFS_ERR_NOTDIR);
        return NULL;
    }

    /* Find a free directory slot */
    dir = NULL;
    for (int i = 0; i < VFS_MAX_OPEN_DIRS; i++) {
        if (!vfs_open_dirs[i].in_use) {
            dir = &vfs_open_dirs[i];
            break;
        }
    }

    if (!dir) {
        vfs_unref_node(node);
        kprintf("[VFS] opendir: Too many open directories\n");
        vfs_set_error(VFS_ERR_NFILE);
        return NULL;
    }

    /* Initialize directory handle */
    dir->node = node;
    dir->position = 0;
    dir->in_use = true;

    return dir;
}

vfs_dirent_t* vfs_readdir(vfs_dir_t *dir) {
    vfs_mount_t *mount;
    vfs_dirent_t *entry;

    if (!dir || !dir->in_use || !dir->node) {
        vfs_set_error(VFS_ERR_BADF);
        return NULL;
    }

    mount = dir->node->mount;
    if (!mount || !mount->ops || !mount->ops->readdir) {
        vfs_set_error(VFS_ERR_NOSYS);
        return NULL;
    }

    entry = mount->ops->readdir(dir->node, dir->position);
    if (entry) {
        /* Copy to static buffer */
        vfs_memcpy(&vfs_dirent_buf, entry, sizeof(vfs_dirent_t));
        dir->position++;
        return &vfs_dirent_buf;
    }

    return NULL;  /* End of directory */
}

int vfs_closedir(vfs_dir_t *dir) {
    if (!dir || !dir->in_use) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    kprintf("[VFS] Closing directory\n");

    if (dir->node) {
        vfs_unref_node(dir->node);
    }

    dir->in_use = false;
    dir->node = NULL;

    return VFS_OK;
}

int vfs_mkdir(const char *path, uint32_t mode) {
    vfs_node_t *parent;
    vfs_mount_t *mount;
    char *parent_path;
    const char *name;
    int result;

    kprintf("[VFS] Creating directory: %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Check if already exists */
    if (vfs_lookup(path)) {
        kprintf("[VFS] mkdir: Already exists: %s\n", path);
        vfs_set_error(VFS_ERR_EXIST);
        return VFS_ERR_EXIST;
    }

    /* Get parent directory */
    parent_path = vfs_parent_path(path);
    name = vfs_basename(path);

    parent = vfs_lookup(parent_path);
    if (!parent) {
        kprintf("[VFS] mkdir: Parent not found: %s\n", parent_path);
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    if (parent->type != VFS_NODE_DIRECTORY) {
        vfs_unref_node(parent);
        kprintf("[VFS] mkdir: Parent is not a directory\n");
        vfs_set_error(VFS_ERR_NOTDIR);
        return VFS_ERR_NOTDIR;
    }

    mount = parent->mount;
    if (!mount || !mount->ops || !mount->ops->mkdir) {
        vfs_unref_node(parent);
        kprintf("[VFS] mkdir: Not supported\n");
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    if (mount->readonly) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    result = mount->ops->mkdir(parent, name, mode);
    vfs_unref_node(parent);

    if (result == VFS_OK) {
        kprintf("[VFS] Created directory: %s\n", path);
    } else {
        kprintf("[VFS] mkdir failed: %s\n", vfs_strerror(result));
    }

    return result;
}

int vfs_rmdir(const char *path) {
    vfs_node_t *parent;
    vfs_mount_t *mount;
    char *parent_path;
    const char *name;
    int result;

    kprintf("[VFS] Removing directory: %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Cannot remove root */
    if (vfs_strcmp(path, "/") == 0) {
        vfs_set_error(VFS_ERR_BUSY);
        return VFS_ERR_BUSY;
    }

    /* Get parent directory */
    parent_path = vfs_parent_path(path);
    name = vfs_basename(path);

    parent = vfs_lookup(parent_path);
    if (!parent) {
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    mount = parent->mount;
    if (!mount || !mount->ops || !mount->ops->rmdir) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    if (mount->readonly) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    result = mount->ops->rmdir(parent, name);
    vfs_unref_node(parent);

    return result;
}

/*============================================================================
 * File/path operations
 *============================================================================*/

int vfs_create(const char *path, uint32_t mode) {
    vfs_node_t *parent;
    vfs_mount_t *mount;
    char *parent_path;
    const char *name;
    int result;

    kprintf("[VFS] Creating file: %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Check if already exists */
    if (vfs_lookup(path)) {
        vfs_set_error(VFS_ERR_EXIST);
        return VFS_ERR_EXIST;
    }

    /* Get parent directory */
    parent_path = vfs_parent_path(path);
    name = vfs_basename(path);

    parent = vfs_lookup(parent_path);
    if (!parent) {
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    if (parent->type != VFS_NODE_DIRECTORY) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_NOTDIR);
        return VFS_ERR_NOTDIR;
    }

    mount = parent->mount;
    if (!mount || !mount->ops || !mount->ops->create) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    if (mount->readonly) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    result = mount->ops->create(parent, name, mode);
    vfs_unref_node(parent);

    return result;
}

int vfs_unlink(const char *path) {
    vfs_node_t *parent;
    vfs_node_t *node;
    vfs_mount_t *mount;
    char *parent_path;
    const char *name;
    int result;

    kprintf("[VFS] Unlinking: %s\n", path);

    if (!path) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Check if exists */
    node = vfs_lookup(path);
    if (!node) {
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    /* Cannot unlink directory */
    if (node->type == VFS_NODE_DIRECTORY) {
        vfs_unref_node(node);
        vfs_set_error(VFS_ERR_ISDIR);
        return VFS_ERR_ISDIR;
    }

    vfs_unref_node(node);

    /* Get parent directory */
    parent_path = vfs_parent_path(path);
    name = vfs_basename(path);

    parent = vfs_lookup(parent_path);
    if (!parent) {
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    mount = parent->mount;
    if (!mount || !mount->ops || !mount->ops->unlink) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    if (mount->readonly) {
        vfs_unref_node(parent);
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    result = mount->ops->unlink(parent, name);
    vfs_unref_node(parent);

    return result;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    vfs_node_t *old_parent;
    vfs_node_t *new_parent;
    vfs_mount_t *mount;
    char *old_parent_path;
    char *new_parent_path;
    const char *old_name;
    const char *new_name;
    int result;

    kprintf("[VFS] Renaming: %s -> %s\n", oldpath, newpath);

    if (!oldpath || !newpath) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    /* Get parent directories */
    old_parent_path = vfs_parent_path(oldpath);
    old_parent = vfs_lookup(old_parent_path);
    if (!old_parent) {
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    new_parent_path = vfs_parent_path(newpath);
    new_parent = vfs_lookup(new_parent_path);
    if (!new_parent) {
        vfs_unref_node(old_parent);
        vfs_set_error(VFS_ERR_NOENT);
        return VFS_ERR_NOENT;
    }

    /* Both must be on same filesystem */
    if (old_parent->mount != new_parent->mount) {
        vfs_unref_node(old_parent);
        vfs_unref_node(new_parent);
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    old_name = vfs_basename(oldpath);
    new_name = vfs_basename(newpath);

    mount = old_parent->mount;
    if (!mount || !mount->ops || !mount->ops->rename) {
        vfs_unref_node(old_parent);
        vfs_unref_node(new_parent);
        vfs_set_error(VFS_ERR_NOSYS);
        return VFS_ERR_NOSYS;
    }

    if (mount->readonly) {
        vfs_unref_node(old_parent);
        vfs_unref_node(new_parent);
        vfs_set_error(VFS_ERR_ROFS);
        return VFS_ERR_ROFS;
    }

    result = mount->ops->rename(old_parent, old_name, new_parent, new_name);
    vfs_unref_node(old_parent);
    vfs_unref_node(new_parent);

    return result;
}

int vfs_stat(const char *path, vfs_stat_t *stat) {
    vfs_node_t *node;
    vfs_mount_t *mount;
    int result;

    if (!path || !stat) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    node = vfs_lookup(path);
    if (!node) {
        return VFS_ERR_NOENT;
    }

    mount = node->mount;
    if (mount && mount->ops && mount->ops->stat) {
        result = mount->ops->stat(node, stat);
    } else {
        /* Fill from node if no stat operation */
        vfs_memset(stat, 0, sizeof(*stat));
        stat->st_ino = node->inode;
        stat->st_mode = node->permissions;
        stat->st_nlink = node->nlink;
        stat->st_uid = node->uid;
        stat->st_gid = node->gid;
        stat->st_size = node->size;
        stat->st_atime = node->atime;
        stat->st_mtime = node->mtime;
        stat->st_ctime = node->ctime;
        stat->st_type = node->type;
        result = VFS_OK;
    }

    vfs_unref_node(node);
    return result;
}

int vfs_fstat(vfs_file_t *file, vfs_stat_t *stat) {
    vfs_mount_t *mount;
    vfs_node_t *node;

    if (!file || !file->in_use || !stat) {
        vfs_set_error(VFS_ERR_INVAL);
        return VFS_ERR_INVAL;
    }

    node = file->node;
    if (!node) {
        vfs_set_error(VFS_ERR_BADF);
        return VFS_ERR_BADF;
    }

    mount = node->mount;
    if (mount && mount->ops && mount->ops->stat) {
        return mount->ops->stat(node, stat);
    }

    /* Fill from node */
    vfs_memset(stat, 0, sizeof(*stat));
    stat->st_ino = node->inode;
    stat->st_mode = node->permissions;
    stat->st_nlink = node->nlink;
    stat->st_uid = node->uid;
    stat->st_gid = node->gid;
    stat->st_size = node->size;
    stat->st_atime = node->atime;
    stat->st_mtime = node->mtime;
    stat->st_ctime = node->ctime;
    stat->st_type = node->type;

    return VFS_OK;
}

bool vfs_exists(const char *path) {
    vfs_node_t *node = vfs_lookup(path);
    if (node) {
        vfs_unref_node(node);
        return true;
    }
    return false;
}

bool vfs_is_directory(const char *path) {
    vfs_node_t *node = vfs_lookup(path);
    if (node) {
        bool is_dir = (node->type == VFS_NODE_DIRECTORY);
        vfs_unref_node(node);
        return is_dir;
    }
    return false;
}

bool vfs_is_file(const char *path) {
    vfs_node_t *node = vfs_lookup(path);
    if (node) {
        bool is_file = (node->type == VFS_NODE_FILE);
        vfs_unref_node(node);
        return is_file;
    }
    return false;
}

/*============================================================================
 * VFS Initialization
 *============================================================================*/

int vfs_init(void) {
    kprintf("[VFS] Initializing Virtual File System...\n");

    /* Initialize mount table */
    vfs_memset(vfs_mounts, 0, sizeof(vfs_mounts));
    vfs_mount_count = 0;
    vfs_root_mount = NULL;

    /* Initialize open files table */
    vfs_memset(vfs_open_files, 0, sizeof(vfs_open_files));

    /* Initialize open directories table */
    vfs_memset(vfs_open_dirs, 0, sizeof(vfs_open_dirs));

    /* Initialize node pool */
    vfs_memset(vfs_node_pool, 0, sizeof(vfs_node_pool));
    vfs_memset(vfs_node_used, 0, sizeof(vfs_node_used));

    /* Initialize filesystem types list */
    vfs_fs_types = NULL;

    /* Clear error state */
    vfs_last_error = VFS_OK;

    vfs_initialized = true;

    kprintf("[VFS] VFS initialized successfully\n");
    kprintf("[VFS]   Max open files: %d\n", VFS_MAX_OPEN_FILES);
    kprintf("[VFS]   Max mounts: %d\n", VFS_MAX_MOUNTS);
    kprintf("[VFS]   Max path length: %d\n", VFS_PATH_MAX);

    return VFS_OK;
}
