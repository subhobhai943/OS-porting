/**
 * AAAos User Authentication System
 *
 * This header defines the user authentication interface for AAAos.
 * It provides user account management, password hashing with SHA-256,
 * login/logout session handling, and multi-user support.
 */

#ifndef _AAAOS_AUTH_H
#define _AAAOS_AUTH_H

#include "../../kernel/include/types.h"

/*============================================================================
 * Configuration Constants
 *============================================================================*/

#define AUTH_MAX_USERS          64          /* Maximum number of users */
#define AUTH_MAX_SESSIONS       32          /* Maximum concurrent sessions */
#define AUTH_MAX_GROUPS         32          /* Maximum number of groups */
#define AUTH_MAX_GROUP_MEMBERS  64          /* Maximum members per group */

#define AUTH_USERNAME_MAX       32          /* Maximum username length */
#define AUTH_PASSWORD_MAX       128         /* Maximum password length */
#define AUTH_HOME_DIR_MAX       256         /* Maximum home directory path */
#define AUTH_SHELL_MAX          128         /* Maximum shell path length */
#define AUTH_GROUP_NAME_MAX     32          /* Maximum group name length */

#define AUTH_SALT_SIZE          16          /* Salt size in bytes */
#define AUTH_HASH_SIZE          32          /* SHA-256 hash size (256 bits) */
#define AUTH_HASH_STRING_SIZE   (AUTH_SALT_SIZE * 2 + 1 + AUTH_HASH_SIZE * 2 + 1)

/* Special UIDs */
#define AUTH_UID_ROOT           0           /* Root/administrator UID */
#define AUTH_UID_GUEST          1000        /* Guest account UID */
#define AUTH_UID_INVALID        ((uint32_t)-1)

/* Special GIDs */
#define AUTH_GID_ROOT           0           /* Root group GID */
#define AUTH_GID_USERS          100         /* Standard users group GID */
#define AUTH_GID_INVALID        ((uint32_t)-1)

/* Session ID invalid value */
#define AUTH_SID_INVALID        ((uint32_t)-1)

/* Error codes */
#define AUTH_OK                 0           /* Success */
#define AUTH_ERR_NOMEM          (-1)        /* Out of memory */
#define AUTH_ERR_EXISTS         (-2)        /* User/group already exists */
#define AUTH_ERR_NOENT          (-3)        /* User/group not found */
#define AUTH_ERR_INVAL          (-4)        /* Invalid argument */
#define AUTH_ERR_PERM           (-5)        /* Permission denied */
#define AUTH_ERR_BADPASS        (-6)        /* Invalid password */
#define AUTH_ERR_MAXUSERS       (-7)        /* Maximum users reached */
#define AUTH_ERR_MAXSESSIONS    (-8)        /* Maximum sessions reached */
#define AUTH_ERR_MAXGROUPS      (-9)        /* Maximum groups reached */
#define AUTH_ERR_LOCKED         (-10)       /* Account is locked */
#define AUTH_ERR_EXPIRED        (-11)       /* Password/account expired */

/* User flags */
#define AUTH_USER_ACTIVE        BIT(0)      /* Account is active */
#define AUTH_USER_LOCKED        BIT(1)      /* Account is locked */
#define AUTH_USER_SYSTEM        BIT(2)      /* System account (no login) */
#define AUTH_USER_ADMIN         BIT(3)      /* Administrator privileges */

/*============================================================================
 * Data Structures
 *============================================================================*/

/**
 * User account structure
 * Contains all information about a user account
 */
typedef struct user {
    uint32_t uid;                           /* User ID */
    uint32_t gid;                           /* Primary group ID */
    char username[AUTH_USERNAME_MAX];       /* Username */
    char password_hash[AUTH_HASH_STRING_SIZE]; /* Password hash (salt$hash format) */
    char home_dir[AUTH_HOME_DIR_MAX];       /* Home directory path */
    char shell[AUTH_SHELL_MAX];             /* Default shell */
    uint32_t flags;                         /* User flags */
    uint64_t created_time;                  /* Account creation time */
    uint64_t last_login;                    /* Last successful login time */
    uint32_t failed_logins;                 /* Failed login attempt counter */
    uint32_t groups[AUTH_MAX_GROUPS];       /* Secondary group memberships (GIDs) */
    uint32_t group_count;                   /* Number of secondary groups */
    bool in_use;                            /* Slot is in use */
} user_t;

/**
 * Login session structure
 * Tracks an active user login session
 */
typedef struct session {
    uint32_t sid;                           /* Session ID */
    uint32_t uid;                           /* User ID of logged-in user */
    uint64_t start_time;                    /* Session start time */
    uint64_t last_activity;                 /* Last activity timestamp */
    char terminal[32];                      /* Terminal identifier (e.g., "tty1") */
    uint32_t flags;                         /* Session flags */
    #define SESSION_FLAG_ACTIVE     BIT(0)  /* Session is active */
    #define SESSION_FLAG_REMOTE     BIT(1)  /* Remote session (SSH, etc.) */
    bool in_use;                            /* Slot is in use */
} session_t;

/**
 * User group structure
 * Represents a group of users
 */
typedef struct user_group {
    uint32_t gid;                           /* Group ID */
    char name[AUTH_GROUP_NAME_MAX];         /* Group name */
    uint32_t members[AUTH_MAX_GROUP_MEMBERS]; /* Member UIDs */
    uint32_t member_count;                  /* Number of members */
    bool in_use;                            /* Slot is in use */
} user_group_t;

/**
 * Authentication statistics
 */
typedef struct auth_stats {
    uint32_t total_users;                   /* Total registered users */
    uint32_t active_sessions;               /* Currently active sessions */
    uint32_t total_logins;                  /* Total successful logins */
    uint32_t failed_logins;                 /* Total failed login attempts */
    uint32_t total_groups;                  /* Total groups */
} auth_stats_t;

/*============================================================================
 * Initialization
 *============================================================================*/

/**
 * Initialize the authentication subsystem
 * Creates default users (root, guest) and groups.
 *
 * @return AUTH_OK on success, error code on failure
 */
int auth_init(void);

/**
 * Shutdown the authentication subsystem
 * Logs out all sessions and saves user database.
 */
void auth_shutdown(void);

/*============================================================================
 * User Management
 *============================================================================*/

/**
 * Create a new user account
 *
 * @param username  Username (must be unique)
 * @param password  Initial password
 * @param uid       User ID to assign (use AUTH_UID_INVALID for auto-assign)
 * @return AUTH_OK on success, error code on failure
 */
int auth_create_user(const char *username, const char *password, uint32_t uid);

/**
 * Delete a user account
 * Warning: This will terminate any active sessions for this user.
 *
 * @param uid User ID to delete
 * @return AUTH_OK on success, error code on failure
 */
int auth_delete_user(uint32_t uid);

/**
 * Set or change a user's password
 *
 * @param uid      User ID
 * @param password New password
 * @return AUTH_OK on success, error code on failure
 */
int auth_set_password(uint32_t uid, const char *password);

/**
 * Get user information by UID
 *
 * @param uid User ID to look up
 * @return Pointer to user structure, or NULL if not found
 */
user_t* auth_get_user(uint32_t uid);

/**
 * Get user information by username
 *
 * @param username Username to look up
 * @return Pointer to user structure, or NULL if not found
 */
user_t* auth_get_user_by_name(const char *username);

/**
 * Get the currently logged-in user for this context
 *
 * @return Pointer to current user, or NULL if not logged in
 */
user_t* auth_get_current_user(void);

/**
 * Set user flags
 *
 * @param uid   User ID
 * @param flags New flags to set
 * @return AUTH_OK on success, error code on failure
 */
int auth_set_user_flags(uint32_t uid, uint32_t flags);

/**
 * Set user's home directory
 *
 * @param uid      User ID
 * @param home_dir New home directory path
 * @return AUTH_OK on success, error code on failure
 */
int auth_set_home_dir(uint32_t uid, const char *home_dir);

/**
 * Set user's default shell
 *
 * @param uid   User ID
 * @param shell New shell path
 * @return AUTH_OK on success, error code on failure
 */
int auth_set_shell(uint32_t uid, const char *shell);

/**
 * Lock a user account (prevent login)
 *
 * @param uid User ID to lock
 * @return AUTH_OK on success, error code on failure
 */
int auth_lock_user(uint32_t uid);

/**
 * Unlock a user account
 *
 * @param uid User ID to unlock
 * @return AUTH_OK on success, error code on failure
 */
int auth_unlock_user(uint32_t uid);

/*============================================================================
 * Session Management
 *============================================================================*/

/**
 * Authenticate user and create a login session
 *
 * @param username    Username
 * @param password    Password
 * @param session_out Pointer to store session pointer on success
 * @return AUTH_OK on success, error code on failure
 */
int auth_login(const char *username, const char *password, session_t **session_out);

/**
 * Terminate a login session (logout)
 *
 * @param session Session to terminate
 * @return AUTH_OK on success, error code on failure
 */
int auth_logout(session_t *session);

/**
 * Get session by session ID
 *
 * @param sid Session ID
 * @return Pointer to session, or NULL if not found
 */
session_t* auth_get_session(uint32_t sid);

/**
 * Get all active sessions for a user
 *
 * @param uid          User ID
 * @param sessions     Array to store session pointers
 * @param max_sessions Maximum sessions to return
 * @return Number of sessions found
 */
uint32_t auth_get_user_sessions(uint32_t uid, session_t **sessions, uint32_t max_sessions);

/**
 * Update session's last activity timestamp
 *
 * @param session Session to update
 */
void auth_touch_session(session_t *session);

/**
 * Set the current session for this context
 *
 * @param session Session to set as current
 */
void auth_set_current_session(session_t *session);

/**
 * Get the current session
 *
 * @return Current session, or NULL if none
 */
session_t* auth_get_current_session(void);

/*============================================================================
 * Password Operations
 *============================================================================*/

/**
 * Verify a user's password without creating a session
 *
 * @param username Username
 * @param password Password to verify
 * @return AUTH_OK if password matches, AUTH_ERR_BADPASS if not
 */
int auth_verify_password(const char *username, const char *password);

/**
 * Hash a password using SHA-256 with a random salt
 *
 * @param password   Plain text password
 * @param hash_out   Buffer to store result (must be AUTH_HASH_STRING_SIZE bytes)
 * @return AUTH_OK on success, error code on failure
 */
int auth_hash_password(const char *password, char *hash_out);

/**
 * Verify a password against a stored hash
 *
 * @param password Plain text password to check
 * @param hash     Stored hash (salt$hash format)
 * @return AUTH_OK if match, AUTH_ERR_BADPASS if not
 */
int auth_check_hash(const char *password, const char *hash);

/*============================================================================
 * Group Management
 *============================================================================*/

/**
 * Create a new group
 *
 * @param name Group name
 * @param gid  Group ID (use AUTH_GID_INVALID for auto-assign)
 * @return AUTH_OK on success, error code on failure
 */
int auth_create_group(const char *name, uint32_t gid);

/**
 * Delete a group
 *
 * @param gid Group ID to delete
 * @return AUTH_OK on success, error code on failure
 */
int auth_delete_group(uint32_t gid);

/**
 * Add a user to a group
 *
 * @param uid User ID
 * @param gid Group ID
 * @return AUTH_OK on success, error code on failure
 */
int auth_add_to_group(uint32_t uid, uint32_t gid);

/**
 * Remove a user from a group
 *
 * @param uid User ID
 * @param gid Group ID
 * @return AUTH_OK on success, error code on failure
 */
int auth_remove_from_group(uint32_t uid, uint32_t gid);

/**
 * Check if a user is a member of a group
 *
 * @param uid User ID
 * @param gid Group ID
 * @return true if member, false otherwise
 */
bool auth_is_member(uint32_t uid, uint32_t gid);

/**
 * Get group by GID
 *
 * @param gid Group ID
 * @return Pointer to group, or NULL if not found
 */
user_group_t* auth_get_group(uint32_t gid);

/**
 * Get group by name
 *
 * @param name Group name
 * @return Pointer to group, or NULL if not found
 */
user_group_t* auth_get_group_by_name(const char *name);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Check if a user has administrator privileges
 *
 * @param uid User ID
 * @return true if admin, false otherwise
 */
bool auth_is_admin(uint32_t uid);

/**
 * Check if a user is root
 *
 * @param uid User ID
 * @return true if root, false otherwise
 */
bool auth_is_root(uint32_t uid);

/**
 * Get authentication statistics
 *
 * @param stats Pointer to stats structure to fill
 */
void auth_get_stats(auth_stats_t *stats);

/**
 * Convert auth error code to string
 *
 * @param error Error code
 * @return Error description string
 */
const char* auth_strerror(int error);

/**
 * Dump all users to serial console (for debugging)
 */
void auth_dump_users(void);

/**
 * Dump all sessions to serial console (for debugging)
 */
void auth_dump_sessions(void);

/**
 * Dump all groups to serial console (for debugging)
 */
void auth_dump_groups(void);

#endif /* _AAAOS_AUTH_H */
