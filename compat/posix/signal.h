/**
 * AAAos POSIX Compatibility Layer - Signal Handling Header
 *
 * This header provides POSIX-compatible signal handling functions
 * and signal number definitions.
 */

#ifndef _AAAOS_POSIX_SIGNAL_H
#define _AAAOS_POSIX_SIGNAL_H

#include "../../kernel/include/types.h"
#include "unistd.h"
#include "errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Signal Numbers
 * ============================================================================ */

#define SIGHUP          1       /* Hangup (POSIX) */
#define SIGINT          2       /* Interrupt (ANSI) */
#define SIGQUIT         3       /* Quit (POSIX) */
#define SIGILL          4       /* Illegal instruction (ANSI) */
#define SIGTRAP         5       /* Trace trap (POSIX) */
#define SIGABRT         6       /* Abort (ANSI) */
#define SIGIOT          SIGABRT /* IOT trap (alias for SIGABRT) */
#define SIGBUS          7       /* BUS error (4.2 BSD) */
#define SIGFPE          8       /* Floating-point exception (ANSI) */
#define SIGKILL         9       /* Kill, unblockable (POSIX) */
#define SIGUSR1         10      /* User-defined signal 1 (POSIX) */
#define SIGSEGV         11      /* Segmentation violation (ANSI) */
#define SIGUSR2         12      /* User-defined signal 2 (POSIX) */
#define SIGPIPE         13      /* Broken pipe (POSIX) */
#define SIGALRM         14      /* Alarm clock (POSIX) */
#define SIGTERM         15      /* Termination (ANSI) */
#define SIGSTKFLT       16      /* Stack fault */
#define SIGCHLD         17      /* Child status has changed (POSIX) */
#define SIGCLD          SIGCHLD /* Same as SIGCHLD (System V) */
#define SIGCONT         18      /* Continue (POSIX) */
#define SIGSTOP         19      /* Stop, unblockable (POSIX) */
#define SIGTSTP         20      /* Keyboard stop (POSIX) */
#define SIGTTIN         21      /* Background read from tty (POSIX) */
#define SIGTTOU         22      /* Background write to tty (POSIX) */
#define SIGURG          23      /* Urgent condition on socket (4.2 BSD) */
#define SIGXCPU         24      /* CPU limit exceeded (4.2 BSD) */
#define SIGXFSZ         25      /* File size limit exceeded (4.2 BSD) */
#define SIGVTALRM       26      /* Virtual alarm clock (4.2 BSD) */
#define SIGPROF         27      /* Profiling alarm clock (4.2 BSD) */
#define SIGWINCH        28      /* Window size change (4.3 BSD, Sun) */
#define SIGIO           29      /* I/O now possible (4.2 BSD) */
#define SIGPOLL         SIGIO   /* Pollable event occurred (System V) */
#define SIGPWR          30      /* Power failure restart (System V) */
#define SIGSYS          31      /* Bad system call */
#define SIGUNUSED       31      /* Unused signal (same as SIGSYS) */

/* Real-time signals */
#define SIGRTMIN        32      /* First real-time signal */
#define SIGRTMAX        64      /* Last real-time signal */

/* Number of signals */
#define NSIG            65      /* Biggest signal number + 1 */
#define _NSIG           NSIG    /* Alias for NSIG */

/* ============================================================================
 * Special Signal Handler Values
 * ============================================================================ */

#define SIG_DFL         ((sighandler_t)0)   /* Default signal handling */
#define SIG_IGN         ((sighandler_t)1)   /* Ignore signal */
#define SIG_ERR         ((sighandler_t)-1)  /* Error return from signal */
#define SIG_HOLD        ((sighandler_t)2)   /* Add signal to hold mask */

/* ============================================================================
 * Signal Action Flags (for sigaction)
 * ============================================================================ */

#define SA_NOCLDSTOP    0x00000001  /* Don't send SIGCHLD when children stop */
#define SA_NOCLDWAIT    0x00000002  /* Don't create zombie on child death */
#define SA_SIGINFO      0x00000004  /* Invoke signal-catching function with 3 args */
#define SA_ONSTACK      0x08000000  /* Deliver signal on alternate stack */
#define SA_RESTART      0x10000000  /* Restart syscall on signal return */
#define SA_NODEFER      0x40000000  /* Don't block signal while handling it */
#define SA_RESETHAND    0x80000000  /* Reset to SIG_DFL upon entry */
#define SA_NOMASK       SA_NODEFER  /* Alias for SA_NODEFER */
#define SA_ONESHOT      SA_RESETHAND /* Alias for SA_RESETHAND */

/* ============================================================================
 * Signal Sets
 * ============================================================================ */

#define _SIGSET_NWORDS  (1024 / (8 * sizeof(unsigned long)))

typedef struct {
    unsigned long sig[_SIGSET_NWORDS];
} sigset_t;

/* ============================================================================
 * Signal Handler Types
 * ============================================================================ */

/* Simple signal handler function pointer */
typedef void (*sighandler_t)(int);

/* Signal handler with extra info (for SA_SIGINFO) */
typedef void (*sigaction_handler_t)(int, void *, void *);

/* ============================================================================
 * Signal Info Structure (for SA_SIGINFO)
 * ============================================================================ */

typedef struct {
    int      si_signo;      /* Signal number */
    int      si_errno;      /* Error number */
    int      si_code;       /* Signal code */
    pid_t    si_pid;        /* Sending process ID */
    uid_t    si_uid;        /* Real user ID of sending process */
    int      si_status;     /* Exit value or signal */
    void    *si_addr;       /* Address of faulting instruction */
    int      si_band;       /* Band event for SIGPOLL */
    union {
        int  _pad[128 / sizeof(int) - 4];
        struct {
            void    *si_call_addr;
            int      si_syscall;
            unsigned si_arch;
        } _sigsys;
    } _sifields;
} siginfo_t;

/* Signal code values for si_code */
#define SI_USER         0       /* Sent by kill, sigsend, raise */
#define SI_KERNEL       0x80    /* Sent by kernel */
#define SI_QUEUE        -1      /* Sent by sigqueue */
#define SI_TIMER        -2      /* Sent by timer expiration */
#define SI_MESGQ        -3      /* Sent by message queue arrival */
#define SI_ASYNCIO      -4      /* Sent by AIO completion */
#define SI_SIGIO        -5      /* Sent by SIGIO */
#define SI_TKILL        -6      /* Sent by tkill/tgkill */

/* ============================================================================
 * Signal Action Structure
 * ============================================================================ */

struct sigaction {
    union {
        sighandler_t        sa_handler;     /* Signal handler function */
        sigaction_handler_t sa_sigaction;   /* Alternate signal handler */
    };
    sigset_t    sa_mask;        /* Signals blocked during handler */
    int         sa_flags;       /* Signal action flags */
    void      (*sa_restorer)(void);  /* Restore function (not used) */
};

/* ============================================================================
 * Signal Stack Structure
 * ============================================================================ */

typedef struct {
    void   *ss_sp;      /* Stack base or pointer */
    int     ss_flags;   /* Flags */
    size_t  ss_size;    /* Stack size */
} stack_t;

/* Stack flags */
#define SS_ONSTACK      1       /* Process is executing on alternate stack */
#define SS_DISABLE      2       /* Alternate stack is disabled */
#define MINSIGSTKSZ     2048    /* Minimum signal stack size */
#define SIGSTKSZ        8192    /* Default signal stack size */

/* ============================================================================
 * Signal Set Manipulation Functions
 * ============================================================================ */

/**
 * Initialize an empty signal set
 * @param set Signal set to initialize
 * @return 0 on success, -1 on error
 */
int sigemptyset(sigset_t *set);

/**
 * Initialize a full signal set
 * @param set Signal set to initialize
 * @return 0 on success, -1 on error
 */
int sigfillset(sigset_t *set);

/**
 * Add a signal to a signal set
 * @param set Signal set
 * @param signum Signal number to add
 * @return 0 on success, -1 on error
 */
int sigaddset(sigset_t *set, int signum);

/**
 * Remove a signal from a signal set
 * @param set Signal set
 * @param signum Signal number to remove
 * @return 0 on success, -1 on error
 */
int sigdelset(sigset_t *set, int signum);

/**
 * Test whether a signal is in a signal set
 * @param set Signal set
 * @param signum Signal number to test
 * @return 1 if signal is in set, 0 if not, -1 on error
 */
int sigismember(const sigset_t *set, int signum);

/* ============================================================================
 * Signal Handling Functions
 * ============================================================================ */

/**
 * Set signal handler (simplified interface)
 * @param signum Signal number
 * @param handler Signal handler function
 * @return Previous signal handler, SIG_ERR on error (errno set)
 */
sighandler_t signal(int signum, sighandler_t handler);

/**
 * Examine and change a signal action
 * @param signum Signal number
 * @param act New action (NULL to query only)
 * @param oldact Previous action (NULL to not retrieve)
 * @return 0 on success, -1 on error (errno set)
 */
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

/**
 * Change the signal mask
 * @param how SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK
 * @param set New signal mask
 * @param oldset Previous signal mask (NULL to not retrieve)
 * @return 0 on success, -1 on error (errno set)
 */
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/* How values for sigprocmask */
#define SIG_BLOCK       0       /* Block signals in set */
#define SIG_UNBLOCK     1       /* Unblock signals in set */
#define SIG_SETMASK     2       /* Set signal mask to set */

/**
 * Get pending signals
 * @param set Signal set to fill with pending signals
 * @return 0 on success, -1 on error (errno set)
 */
int sigpending(sigset_t *set);

/**
 * Wait for a signal
 * @param mask Temporary signal mask
 * @return -1 always (errno set to EINTR)
 */
int sigsuspend(const sigset_t *mask);

/**
 * Synchronously wait for a signal
 * @param set Set of signals to wait for
 * @param info Signal info structure (filled on return)
 * @return Signal number on success, -1 on error (errno set)
 */
int sigwaitinfo(const sigset_t *set, siginfo_t *info);

/**
 * Synchronously wait for a signal with timeout
 * @param set Set of signals to wait for
 * @param info Signal info structure (filled on return)
 * @param timeout Maximum time to wait (NULL for infinite)
 * @return Signal number on success, -1 on error (errno set)
 */
struct timespec;
int sigtimedwait(const sigset_t *set, siginfo_t *info,
                 const struct timespec *timeout);

/* ============================================================================
 * Signal Sending Functions
 * ============================================================================ */

/**
 * Send signal to a process
 * @param pid Process ID (or special values: 0 = current group, -1 = all)
 * @param sig Signal number (0 to check if process exists)
 * @return 0 on success, -1 on error (errno set)
 */
int kill(pid_t pid, int sig);

/**
 * Send signal to a thread
 * @param tgid Thread group ID (process ID)
 * @param tid Thread ID
 * @param sig Signal number
 * @return 0 on success, -1 on error (errno set)
 */
int tgkill(int tgid, int tid, int sig);

/**
 * Send signal to self
 * @param sig Signal number
 * @return 0 on success, non-zero on error
 */
int raise(int sig);

/**
 * Send signal to a process group
 * @param pgrp Process group ID
 * @param sig Signal number
 * @return 0 on success, -1 on error (errno set)
 */
int killpg(int pgrp, int sig);

/**
 * Abort the process (sends SIGABRT)
 * @note This function does not return
 */
void abort(void) NORETURN;

/**
 * Set an alarm clock for delivery of a signal
 * @param seconds Number of seconds until SIGALRM is delivered
 * @return Remaining seconds from previous alarm, or 0
 */
unsigned int alarm(unsigned int seconds);

/**
 * Suspend execution until a signal is caught
 * @return -1 always (errno set to EINTR)
 */
int pause(void);

/* ============================================================================
 * Signal Stack Functions
 * ============================================================================ */

/**
 * Set and/or get signal stack context
 * @param ss New signal stack (NULL to query only)
 * @param old_ss Previous signal stack (NULL to not retrieve)
 * @return 0 on success, -1 on error (errno set)
 */
int sigaltstack(const stack_t *ss, stack_t *old_ss);

/* ============================================================================
 * Signal Name Functions
 * ============================================================================ */

/**
 * Get signal name from signal number
 * @param sig Signal number
 * @return Pointer to signal name string
 */
const char *strsignal(int sig);

/**
 * Print signal description
 * @param sig Signal number
 * @param msg Message to print before description (NULL for none)
 */
void psignal(int sig, const char *msg);

/**
 * Convert signal name to number
 * @param name Signal name (with or without "SIG" prefix)
 * @return Signal number, or -1 if not found
 */
int sig_name_to_num(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _AAAOS_POSIX_SIGNAL_H */
