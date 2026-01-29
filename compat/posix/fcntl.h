/**
 * AAAos POSIX Compatibility Layer - File Control Header
 *
 * This header provides POSIX-compatible file control operations
 * and flags for opening files.
 */

#ifndef _AAAOS_POSIX_FCNTL_H
#define _AAAOS_POSIX_FCNTL_H

#include "../../kernel/include/types.h"
#include "unistd.h"
#include "errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * File Access Modes (mutually exclusive)
 * ============================================================================ */

#define O_RDONLY        0x0000  /* Open for reading only */
#define O_WRONLY        0x0001  /* Open for writing only */
#define O_RDWR          0x0002  /* Open for reading and writing */
#define O_ACCMODE       0x0003  /* Mask for file access modes */

/* ============================================================================
 * File Creation and Status Flags
 * ============================================================================ */

#define O_CREAT         0x0040  /* Create file if it doesn't exist */
#define O_EXCL          0x0080  /* Fail if file exists with O_CREAT */
#define O_NOCTTY        0x0100  /* Don't make the file the controlling terminal */
#define O_TRUNC         0x0200  /* Truncate file to zero length */
#define O_APPEND        0x0400  /* Append on each write */
#define O_NONBLOCK      0x0800  /* Non-blocking mode */
#define O_NDELAY        O_NONBLOCK /* Alias for O_NONBLOCK */
#define O_SYNC          0x1000  /* Synchronous writes */
#define O_DSYNC         0x2000  /* Synchronous data writes */
#define O_RSYNC         0x4000  /* Synchronous reads */
#define O_DIRECTORY     0x10000 /* Fail if not a directory */
#define O_NOFOLLOW      0x20000 /* Don't follow symbolic links */
#define O_CLOEXEC       0x80000 /* Close on exec */
#define O_ASYNC         0x2000  /* Enable signal-driven I/O */
#define O_DIRECT        0x4000  /* Direct disk access */
#define O_LARGEFILE     0x8000  /* Large file support */
#define O_NOATIME       0x100000 /* Do not update access time */
#define O_PATH          0x200000 /* Obtain a file descriptor for path operations */
#define O_TMPFILE       0x400000 /* Create an unnamed temporary file */

/* ============================================================================
 * fcntl() Commands
 * ============================================================================ */

#define F_DUPFD         0       /* Duplicate file descriptor */
#define F_GETFD         1       /* Get file descriptor flags */
#define F_SETFD         2       /* Set file descriptor flags */
#define F_GETFL         3       /* Get file status flags */
#define F_SETFL         4       /* Set file status flags */
#define F_GETLK         5       /* Get record locking info */
#define F_SETLK         6       /* Set record locking info (non-blocking) */
#define F_SETLKW        7       /* Set record locking info (blocking) */
#define F_SETOWN        8       /* Set owner for SIGIO */
#define F_GETOWN        9       /* Get owner for SIGIO */
#define F_SETSIG        10      /* Set signal for F_SETOWN */
#define F_GETSIG        11      /* Get signal for F_SETOWN */
#define F_DUPFD_CLOEXEC 1030    /* Duplicate fd with close-on-exec set */

/* ============================================================================
 * File Descriptor Flags (for F_GETFD/F_SETFD)
 * ============================================================================ */

#define FD_CLOEXEC      1       /* Close on exec flag */

/* ============================================================================
 * Record Locking Types (for flock structure)
 * ============================================================================ */

#define F_RDLCK         0       /* Read (shared) lock */
#define F_WRLCK         1       /* Write (exclusive) lock */
#define F_UNLCK         2       /* Remove lock */

/* ============================================================================
 * File Lock Structure
 * ============================================================================ */

struct flock {
    int16_t l_type;     /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
    int16_t l_whence;   /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
    off_t   l_start;    /* Starting offset for lock */
    off_t   l_len;      /* Number of bytes to lock (0 means to EOF) */
    pid_t   l_pid;      /* PID of process blocking our lock (F_GETLK only) */
};

/* ============================================================================
 * Advisory Locking (flock-style)
 * ============================================================================ */

#define LOCK_SH         1       /* Shared lock */
#define LOCK_EX         2       /* Exclusive lock */
#define LOCK_NB         4       /* Non-blocking lock */
#define LOCK_UN         8       /* Unlock */

/* ============================================================================
 * openat() Flags
 * ============================================================================ */

#define AT_FDCWD        (-100)  /* Use current working directory */
#define AT_SYMLINK_NOFOLLOW 0x100 /* Do not follow symbolic links */
#define AT_REMOVEDIR    0x200   /* Remove directory instead of file */
#define AT_SYMLINK_FOLLOW 0x400 /* Follow symbolic links */
#define AT_EACCESS      0x200   /* Check access using effective IDs */
#define AT_EMPTY_PATH   0x1000  /* Allow empty relative pathname */

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/**
 * Open a file
 * @param pathname Path to the file
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 * @param ... Optional mode argument (required if O_CREAT is set)
 * @return File descriptor on success, -1 on error (errno set)
 */
int open(const char *pathname, int flags, ...);

/**
 * Open a file relative to a directory file descriptor
 * @param dirfd Directory file descriptor (or AT_FDCWD)
 * @param pathname Path to the file
 * @param flags Open flags
 * @param ... Optional mode argument
 * @return File descriptor on success, -1 on error (errno set)
 */
int openat(int dirfd, const char *pathname, int flags, ...);

/**
 * Create and open a file
 * @param pathname Path to the file
 * @param mode File permissions
 * @return File descriptor on success, -1 on error (errno set)
 *
 * Equivalent to open(pathname, O_CREAT|O_WRONLY|O_TRUNC, mode)
 */
int creat(const char *pathname, mode_t mode);

/**
 * Manipulate file descriptor
 * @param fd File descriptor
 * @param cmd Command (F_DUPFD, F_GETFD, F_SETFD, etc.)
 * @param ... Optional argument (depends on cmd)
 * @return Depends on cmd, -1 on error (errno set)
 */
int fcntl(int fd, int cmd, ...);

/**
 * Apply or remove an advisory lock on an open file
 * @param fd File descriptor
 * @param operation Lock operation (LOCK_SH, LOCK_EX, LOCK_UN with optional LOCK_NB)
 * @return 0 on success, -1 on error (errno set)
 */
int flock(int fd, int operation);

/* ============================================================================
 * POSIX Advisory Locking Functions
 * ============================================================================ */

/**
 * Test or set file lock on a section of a file
 * @param fd File descriptor
 * @param cmd F_SETLK, F_SETLKW, or F_GETLK
 * @param lock Pointer to flock structure
 * @return 0 on success, -1 on error (errno set)
 */
int posix_lock(int fd, int cmd, struct flock *lock);

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Convert POSIX open flags to AAAos VFS flags
 * @param posix_flags POSIX O_* flags
 * @return AAAos VFS_O_* flags
 */
int posix_to_vfs_flags(int posix_flags);

/**
 * Convert AAAos VFS flags to POSIX open flags
 * @param vfs_flags AAAos VFS_O_* flags
 * @return POSIX O_* flags
 */
int vfs_to_posix_flags(int vfs_flags);

#ifdef __cplusplus
}
#endif

#endif /* _AAAOS_POSIX_FCNTL_H */
