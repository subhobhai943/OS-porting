/**
 * AAAos POSIX Compatibility Layer - Unistd Header
 *
 * This header provides POSIX-compatible system call wrappers
 * that map to AAAos native system calls.
 */

#ifndef _AAAOS_POSIX_UNISTD_H
#define _AAAOS_POSIX_UNISTD_H

#include "../../kernel/include/types.h"
#include "errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Standard File Descriptors
 * ============================================================================ */

#define STDIN_FILENO    0       /* Standard input file descriptor */
#define STDOUT_FILENO   1       /* Standard output file descriptor */
#define STDERR_FILENO   2       /* Standard error file descriptor */

/* ============================================================================
 * Seek Whence Values
 * ============================================================================ */

#ifndef SEEK_SET
#define SEEK_SET        0       /* Seek from beginning of file */
#endif
#ifndef SEEK_CUR
#define SEEK_CUR        1       /* Seek from current position */
#endif
#ifndef SEEK_END
#define SEEK_END        2       /* Seek from end of file */
#endif

/* ============================================================================
 * Access Mode Flags for access()
 * ============================================================================ */

#define F_OK            0       /* Test for existence */
#define X_OK            1       /* Test for execute permission */
#define W_OK            2       /* Test for write permission */
#define R_OK            4       /* Test for read permission */

/* ============================================================================
 * Path Configuration
 * ============================================================================ */

#define PATH_MAX        4096    /* Maximum path length */

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef int32_t pid_t;          /* Process ID type */
typedef uint32_t uid_t;         /* User ID type */
typedef uint32_t gid_t;         /* Group ID type */
typedef int64_t off_t;          /* File offset type */
typedef uint32_t mode_t;        /* File mode type */

/* ============================================================================
 * Process Control Functions
 * ============================================================================ */

/**
 * Create a child process by duplicating the calling process
 * @return Child PID to parent, 0 to child, -1 on error (errno set)
 */
pid_t fork(void);

/**
 * Execute a program
 * @param pathname Path to the executable
 * @param argv Argument vector (NULL-terminated)
 * @param envp Environment vector (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execve(const char *pathname, char *const argv[], char *const envp[]);

/**
 * Execute a program (search PATH)
 * @param file Filename or path to the executable
 * @param argv Argument vector (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execvp(const char *file, char *const argv[]);

/**
 * Execute a program (variadic arguments)
 * @param pathname Path to the executable
 * @param arg0 First argument (program name)
 * @param ... Additional arguments (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execl(const char *pathname, const char *arg0, ...);

/**
 * Execute a program (variadic, search PATH)
 * @param file Filename or path to the executable
 * @param arg0 First argument (program name)
 * @param ... Additional arguments (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execlp(const char *file, const char *arg0, ...);

/**
 * Execute a program (variadic with environment)
 * @param pathname Path to the executable
 * @param arg0 First argument (program name)
 * @param ... Additional arguments and environment (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execle(const char *pathname, const char *arg0, ...);

/**
 * Execute a program (array with environment)
 * @param pathname Path to the executable
 * @param argv Argument vector (NULL-terminated)
 * @param envp Environment vector (NULL-terminated)
 * @return Does not return on success, -1 on error (errno set)
 */
int execv(const char *pathname, char *const argv[]);

/**
 * Terminate the calling process
 * @param status Exit status
 * @note This function does not return
 */
void _exit(int status) NORETURN;

/* ============================================================================
 * Process ID Functions
 * ============================================================================ */

/**
 * Get the process ID of the calling process
 * @return Process ID
 */
pid_t getpid(void);

/**
 * Get the parent process ID of the calling process
 * @return Parent process ID
 */
pid_t getppid(void);

/**
 * Get the process group ID
 * @return Process group ID
 */
pid_t getpgrp(void);

/**
 * Set the process group ID
 * @param pid Process ID (0 for calling process)
 * @param pgid Process group ID (0 for process ID of pid)
 * @return 0 on success, -1 on error (errno set)
 */
int setpgid(pid_t pid, pid_t pgid);

/**
 * Create a session and set the process group ID
 * @return Session ID on success, -1 on error (errno set)
 */
pid_t setsid(void);

/* ============================================================================
 * User/Group ID Functions
 * ============================================================================ */

/**
 * Get the real user ID of the calling process
 * @return User ID
 */
uid_t getuid(void);

/**
 * Get the effective user ID of the calling process
 * @return Effective user ID
 */
uid_t geteuid(void);

/**
 * Get the real group ID of the calling process
 * @return Group ID
 */
gid_t getgid(void);

/**
 * Get the effective group ID of the calling process
 * @return Effective group ID
 */
gid_t getegid(void);

/**
 * Set the real user ID
 * @param uid User ID to set
 * @return 0 on success, -1 on error (errno set)
 */
int setuid(uid_t uid);

/**
 * Set the effective user ID
 * @param uid User ID to set
 * @return 0 on success, -1 on error (errno set)
 */
int seteuid(uid_t uid);

/**
 * Set the real group ID
 * @param gid Group ID to set
 * @return 0 on success, -1 on error (errno set)
 */
int setgid(gid_t gid);

/**
 * Set the effective group ID
 * @param gid Group ID to set
 * @return 0 on success, -1 on error (errno set)
 */
int setegid(gid_t gid);

/* ============================================================================
 * File I/O Functions
 * ============================================================================ */

/**
 * Read from a file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, 0 on EOF, -1 on error (errno set)
 */
ssize_t read(int fd, void *buf, size_t count);

/**
 * Write to a file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, -1 on error (errno set)
 */
ssize_t write(int fd, const void *buf, size_t count);

/**
 * Close a file descriptor
 * @param fd File descriptor to close
 * @return 0 on success, -1 on error (errno set)
 */
int close(int fd);

/**
 * Reposition read/write file offset
 * @param fd File descriptor
 * @param offset Offset in bytes
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New offset on success, -1 on error (errno set)
 */
off_t lseek(int fd, off_t offset, int whence);

/* ============================================================================
 * File Descriptor Duplication
 * ============================================================================ */

/**
 * Duplicate a file descriptor
 * @param oldfd File descriptor to duplicate
 * @return New file descriptor, or -1 on error (errno set)
 */
int dup(int oldfd);

/**
 * Duplicate a file descriptor to a specific number
 * @param oldfd File descriptor to duplicate
 * @param newfd Desired new file descriptor number
 * @return New file descriptor, or -1 on error (errno set)
 */
int dup2(int oldfd, int newfd);

/* ============================================================================
 * Pipe Functions
 * ============================================================================ */

/**
 * Create a pipe
 * @param pipefd Array of two integers to store read/write file descriptors
 *               pipefd[0] = read end, pipefd[1] = write end
 * @return 0 on success, -1 on error (errno set)
 */
int pipe(int pipefd[2]);

/* ============================================================================
 * Directory Operations
 * ============================================================================ */

/**
 * Change the current working directory
 * @param path Path to the new working directory
 * @return 0 on success, -1 on error (errno set)
 */
int chdir(const char *path);

/**
 * Change to directory of file descriptor
 * @param fd File descriptor of directory
 * @return 0 on success, -1 on error (errno set)
 */
int fchdir(int fd);

/**
 * Get the current working directory
 * @param buf Buffer to store the path
 * @param size Size of the buffer
 * @return Pointer to buf on success, NULL on error (errno set)
 */
char *getcwd(char *buf, size_t size);

/**
 * Remove a directory
 * @param pathname Path to the directory
 * @return 0 on success, -1 on error (errno set)
 */
int rmdir(const char *pathname);

/* ============================================================================
 * File Operations
 * ============================================================================ */

/**
 * Delete a name from the filesystem
 * @param pathname Path to the file
 * @return 0 on success, -1 on error (errno set)
 */
int unlink(const char *pathname);

/**
 * Create a symbolic link
 * @param target Target path
 * @param linkpath Link path
 * @return 0 on success, -1 on error (errno set)
 */
int symlink(const char *target, const char *linkpath);

/**
 * Read a symbolic link
 * @param pathname Path to the symbolic link
 * @param buf Buffer to store the target path
 * @param bufsiz Size of the buffer
 * @return Number of bytes placed in buf, -1 on error (errno set)
 */
ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);

/**
 * Check access permissions
 * @param pathname Path to the file
 * @param mode Access mode (F_OK, R_OK, W_OK, X_OK or combination)
 * @return 0 if permitted, -1 on error (errno set)
 */
int access(const char *pathname, int mode);

/**
 * Truncate a file to a specified length
 * @param path Path to the file
 * @param length New length
 * @return 0 on success, -1 on error (errno set)
 */
int truncate(const char *path, off_t length);

/**
 * Truncate a file to a specified length (by fd)
 * @param fd File descriptor
 * @param length New length
 * @return 0 on success, -1 on error (errno set)
 */
int ftruncate(int fd, off_t length);

/* ============================================================================
 * Sleep Functions
 * ============================================================================ */

/**
 * Sleep for a specified number of seconds
 * @param seconds Number of seconds to sleep
 * @return 0 on normal completion, remaining seconds on interrupt
 */
unsigned int sleep(unsigned int seconds);

/**
 * Sleep for a specified number of microseconds
 * @param usec Number of microseconds to sleep
 * @return 0 on success, -1 on error (errno set)
 */
int usleep(uint32_t usec);

/* ============================================================================
 * Miscellaneous Functions
 * ============================================================================ */

/**
 * Synchronize cached writes to persistent storage
 */
void sync(void);

/**
 * Synchronize a file's state with storage device
 * @param fd File descriptor
 * @return 0 on success, -1 on error (errno set)
 */
int fsync(int fd);

/**
 * Get the system hostname
 * @param name Buffer to store hostname
 * @param len Length of buffer
 * @return 0 on success, -1 on error (errno set)
 */
int gethostname(char *name, size_t len);

/**
 * Set the system hostname
 * @param name New hostname
 * @param len Length of hostname
 * @return 0 on success, -1 on error (errno set)
 */
int sethostname(const char *name, size_t len);

/**
 * Check if file descriptor refers to a terminal
 * @param fd File descriptor
 * @return 1 if terminal, 0 otherwise
 */
int isatty(int fd);

/**
 * Get name of terminal
 * @param fd File descriptor
 * @return Pointer to string containing terminal name, NULL on error
 */
char *ttyname(int fd);

/**
 * Allocate memory pages
 * @param addr Address hint (ignored if NULL)
 * @param len Number of bytes
 * @return 0 on success, -1 on error
 */
int brk(void *addr);

/**
 * Increment program data space
 * @param increment Number of bytes to increment (can be negative)
 * @return Previous program break on success, (void*)-1 on error
 */
void *sbrk(intptr_t increment);

#ifdef __cplusplus
}
#endif

#endif /* _AAAOS_POSIX_UNISTD_H */
