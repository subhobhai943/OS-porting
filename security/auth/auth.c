/**
 * AAAos User Authentication System - Implementation
 *
 * Implements user account management, password hashing with SHA-256,
 * login/logout session handling, and multi-user support.
 */

#include "auth.h"
#include "../../kernel/include/serial.h"
#include "../../kernel/mm/heap.h"
#include "../../lib/libc/string.h"

/*============================================================================
 * Internal Data Structures
 *============================================================================*/

/* User database */
static user_t users[AUTH_MAX_USERS];
static uint32_t user_count = 0;
static uint32_t next_uid = 1001;  /* Start auto-assigned UIDs after guest */

/* Session database */
static session_t sessions[AUTH_MAX_SESSIONS];
static uint32_t session_count = 0;
static uint32_t next_sid = 1;

/* Group database */
static user_group_t groups[AUTH_MAX_GROUPS];
static uint32_t group_count = 0;
static uint32_t next_gid = 101;  /* Start auto-assigned GIDs after users group */

/* Current session (per-context, simplified for single-core) */
static session_t *current_session = NULL;

/* Statistics */
static auth_stats_t stats = {0};

/* Auth system initialized flag */
static bool auth_initialized = false;

/* Simple pseudo-random state for salt generation */
static uint64_t random_state = 0x123456789ABCDEF0ULL;

/*============================================================================
 * SHA-256 Implementation
 *============================================================================*/

/* SHA-256 constants (first 32 bits of fractional parts of cube roots of first 64 primes) */
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* SHA-256 context */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} sha256_ctx_t;

/* Rotate right */
static inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

/* SHA-256 choice function */
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

/* SHA-256 majority function */
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

/* SHA-256 sigma0 */
static inline uint32_t sig0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

/* SHA-256 sigma1 */
static inline uint32_t sig1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

/* SHA-256 gamma0 */
static inline uint32_t gam0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

/* SHA-256 gamma1 */
static inline uint32_t gam1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/* Process a 512-bit block */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t *data) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2;
    uint32_t w[64];
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        w[i] = gam1(w[i - 2]) + w[i - 7] + gam0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main loop */
    for (i = 0; i < 64; i++) {
        t1 = h + sig1(e) + ch(e, f, g) + sha256_k[i] + w[i];
        t2 = sig0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Update state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/* Initialize SHA-256 context */
static void sha256_init(sha256_ctx_t *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

/* Update SHA-256 context with data */
static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

/* Finalize SHA-256 and output hash */
static void sha256_final(sha256_ctx_t *ctx, uint8_t *hash) {
    uint32_t i = ctx->datalen;

    /* Pad message */
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) {
            ctx->data[i++] = 0x00;
        }
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    /* Append message length */
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    /* Output hash (big-endian) */
    for (i = 0; i < 8; i++) {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xff;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xff;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xff;
        hash[i * 4 + 3] = ctx->state[i] & 0xff;
    }
}

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * Simple pseudo-random number generator (xorshift64)
 * Not cryptographically secure, but adequate for salt generation
 */
static uint64_t random_next(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

/**
 * Seed the random number generator
 */
static void random_seed(uint64_t seed) {
    random_state = seed ? seed : 0x123456789ABCDEF0ULL;
}

/**
 * Convert a byte to two hex characters
 */
static void byte_to_hex(uint8_t byte, char *out) {
    const char hex_chars[] = "0123456789abcdef";
    out[0] = hex_chars[(byte >> 4) & 0x0f];
    out[1] = hex_chars[byte & 0x0f];
}

/**
 * Convert a hex character to nibble value
 */
static int hex_to_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Convert two hex characters to a byte
 */
static int hex_to_byte(const char *hex) {
    int hi = hex_to_nibble(hex[0]);
    int lo = hex_to_nibble(hex[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

/**
 * Generate random salt
 */
static void generate_salt(uint8_t *salt, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        salt[i] = (uint8_t)(random_next() & 0xff);
    }
}

/**
 * Get a simple timestamp (tick count for now)
 * In a real system, this would use RTC or system time
 */
static uint64_t get_timestamp(void) {
    /* Use random state as a simple incrementing counter */
    static uint64_t tick = 0;
    return ++tick;
}

/**
 * Find a free user slot
 */
static user_t* find_free_user_slot(void) {
    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++) {
        if (!users[i].in_use) {
            return &users[i];
        }
    }
    return NULL;
}

/**
 * Find a free session slot
 */
static session_t* find_free_session_slot(void) {
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (!sessions[i].in_use) {
            return &sessions[i];
        }
    }
    return NULL;
}

/**
 * Find a free group slot
 */
static user_group_t* find_free_group_slot(void) {
    for (uint32_t i = 0; i < AUTH_MAX_GROUPS; i++) {
        if (!groups[i].in_use) {
            return &groups[i];
        }
    }
    return NULL;
}

/*============================================================================
 * Password Hashing
 *============================================================================*/

int auth_hash_password(const char *password, char *hash_out) {
    if (!password || !hash_out) {
        return AUTH_ERR_INVAL;
    }

    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t hash[AUTH_HASH_SIZE];
    uint8_t salted_password[AUTH_PASSWORD_MAX + AUTH_SALT_SIZE];
    sha256_ctx_t ctx;

    /* Generate random salt */
    generate_salt(salt, AUTH_SALT_SIZE);

    /* Combine salt and password */
    memcpy(salted_password, salt, AUTH_SALT_SIZE);
    size_t pwd_len = strlen(password);
    if (pwd_len > AUTH_PASSWORD_MAX - 1) {
        pwd_len = AUTH_PASSWORD_MAX - 1;
    }
    memcpy(salted_password + AUTH_SALT_SIZE, password, pwd_len);

    /* Compute SHA-256 */
    sha256_init(&ctx);
    sha256_update(&ctx, salted_password, AUTH_SALT_SIZE + pwd_len);
    sha256_final(&ctx, hash);

    /* Format as salt$hash in hex */
    char *p = hash_out;
    for (size_t i = 0; i < AUTH_SALT_SIZE; i++) {
        byte_to_hex(salt[i], p);
        p += 2;
    }
    *p++ = '$';
    for (size_t i = 0; i < AUTH_HASH_SIZE; i++) {
        byte_to_hex(hash[i], p);
        p += 2;
    }
    *p = '\0';

    /* Clear sensitive data */
    memset(salted_password, 0, sizeof(salted_password));
    memset(&ctx, 0, sizeof(ctx));

    return AUTH_OK;
}

int auth_check_hash(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) {
        return AUTH_ERR_INVAL;
    }

    /* Parse stored hash: extract salt */
    uint8_t salt[AUTH_SALT_SIZE];
    uint8_t stored_hash_bytes[AUTH_HASH_SIZE];
    const char *p = stored_hash;

    /* Extract salt (hex encoded) */
    for (size_t i = 0; i < AUTH_SALT_SIZE; i++) {
        int byte = hex_to_byte(p);
        if (byte < 0) {
            return AUTH_ERR_INVAL;
        }
        salt[i] = (uint8_t)byte;
        p += 2;
    }

    /* Skip separator */
    if (*p != '$') {
        return AUTH_ERR_INVAL;
    }
    p++;

    /* Extract stored hash (hex encoded) */
    for (size_t i = 0; i < AUTH_HASH_SIZE; i++) {
        int byte = hex_to_byte(p);
        if (byte < 0) {
            return AUTH_ERR_INVAL;
        }
        stored_hash_bytes[i] = (uint8_t)byte;
        p += 2;
    }

    /* Compute hash with extracted salt */
    uint8_t computed_hash[AUTH_HASH_SIZE];
    uint8_t salted_password[AUTH_PASSWORD_MAX + AUTH_SALT_SIZE];
    sha256_ctx_t ctx;

    memcpy(salted_password, salt, AUTH_SALT_SIZE);
    size_t pwd_len = strlen(password);
    if (pwd_len > AUTH_PASSWORD_MAX - 1) {
        pwd_len = AUTH_PASSWORD_MAX - 1;
    }
    memcpy(salted_password + AUTH_SALT_SIZE, password, pwd_len);

    sha256_init(&ctx);
    sha256_update(&ctx, salted_password, AUTH_SALT_SIZE + pwd_len);
    sha256_final(&ctx, computed_hash);

    /* Compare hashes (constant time) */
    int result = 0;
    for (size_t i = 0; i < AUTH_HASH_SIZE; i++) {
        result |= computed_hash[i] ^ stored_hash_bytes[i];
    }

    /* Clear sensitive data */
    memset(salted_password, 0, sizeof(salted_password));
    memset(&ctx, 0, sizeof(ctx));

    return (result == 0) ? AUTH_OK : AUTH_ERR_BADPASS;
}

int auth_verify_password(const char *username, const char *password) {
    if (!username || !password) {
        return AUTH_ERR_INVAL;
    }

    user_t *user = auth_get_user_by_name(username);
    if (!user) {
        kprintf("[AUTH] verify_password: user '%s' not found\n", username);
        return AUTH_ERR_NOENT;
    }

    if (user->flags & AUTH_USER_LOCKED) {
        kprintf("[AUTH] verify_password: user '%s' is locked\n", username);
        return AUTH_ERR_LOCKED;
    }

    int result = auth_check_hash(password, user->password_hash);
    if (result != AUTH_OK) {
        user->failed_logins++;
        stats.failed_logins++;
        kprintf("[AUTH] verify_password: invalid password for user '%s' (attempt %u)\n",
                username, user->failed_logins);
    }

    return result;
}

/*============================================================================
 * User Management
 *============================================================================*/

int auth_create_user(const char *username, const char *password, uint32_t uid) {
    if (!auth_initialized) {
        return AUTH_ERR_INVAL;
    }

    if (!username || !password) {
        return AUTH_ERR_INVAL;
    }

    /* Check username length */
    if (strlen(username) == 0 || strlen(username) >= AUTH_USERNAME_MAX) {
        kprintf("[AUTH] create_user: invalid username length\n");
        return AUTH_ERR_INVAL;
    }

    /* Check if username already exists */
    if (auth_get_user_by_name(username) != NULL) {
        kprintf("[AUTH] create_user: user '%s' already exists\n", username);
        return AUTH_ERR_EXISTS;
    }

    /* Check if UID already exists (if specified) */
    if (uid != AUTH_UID_INVALID && auth_get_user(uid) != NULL) {
        kprintf("[AUTH] create_user: UID %u already exists\n", uid);
        return AUTH_ERR_EXISTS;
    }

    /* Find free slot */
    user_t *user = find_free_user_slot();
    if (!user) {
        kprintf("[AUTH] create_user: maximum users reached\n");
        return AUTH_ERR_MAXUSERS;
    }

    /* Initialize user */
    memset(user, 0, sizeof(user_t));
    user->in_use = true;
    user->uid = (uid != AUTH_UID_INVALID) ? uid : next_uid++;
    user->gid = AUTH_GID_USERS;
    strncpy(user->username, username, AUTH_USERNAME_MAX - 1);
    user->username[AUTH_USERNAME_MAX - 1] = '\0';

    /* Hash password */
    int result = auth_hash_password(password, user->password_hash);
    if (result != AUTH_OK) {
        user->in_use = false;
        kprintf("[AUTH] create_user: failed to hash password\n");
        return result;
    }

    /* Set defaults */
    user->flags = AUTH_USER_ACTIVE;
    user->created_time = get_timestamp();

    /* Set default home directory */
    if (user->uid == AUTH_UID_ROOT) {
        strncpy(user->home_dir, "/root", AUTH_HOME_DIR_MAX - 1);
    } else {
        /* Format: /home/username */
        strcpy(user->home_dir, "/home/");
        strcat(user->home_dir, username);
    }
    user->home_dir[AUTH_HOME_DIR_MAX - 1] = '\0';

    /* Set default shell */
    strncpy(user->shell, "/bin/sh", AUTH_SHELL_MAX - 1);
    user->shell[AUTH_SHELL_MAX - 1] = '\0';

    user_count++;
    stats.total_users++;

    kprintf("[AUTH] create_user: created user '%s' (uid=%u, gid=%u)\n",
            username, user->uid, user->gid);

    return AUTH_OK;
}

int auth_delete_user(uint32_t uid) {
    if (!auth_initialized) {
        return AUTH_ERR_INVAL;
    }

    /* Cannot delete root */
    if (uid == AUTH_UID_ROOT) {
        kprintf("[AUTH] delete_user: cannot delete root user\n");
        return AUTH_ERR_PERM;
    }

    user_t *user = auth_get_user(uid);
    if (!user) {
        kprintf("[AUTH] delete_user: user %u not found\n", uid);
        return AUTH_ERR_NOENT;
    }

    /* Terminate all sessions for this user */
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (sessions[i].in_use && sessions[i].uid == uid) {
            auth_logout(&sessions[i]);
        }
    }

    /* Remove from all groups */
    for (uint32_t i = 0; i < AUTH_MAX_GROUPS; i++) {
        if (groups[i].in_use) {
            auth_remove_from_group(uid, groups[i].gid);
        }
    }

    kprintf("[AUTH] delete_user: deleted user '%s' (uid=%u)\n",
            user->username, uid);

    /* Clear user data */
    memset(user, 0, sizeof(user_t));
    user_count--;

    return AUTH_OK;
}

int auth_set_password(uint32_t uid, const char *password) {
    if (!auth_initialized || !password) {
        return AUTH_ERR_INVAL;
    }

    user_t *user = auth_get_user(uid);
    if (!user) {
        kprintf("[AUTH] set_password: user %u not found\n", uid);
        return AUTH_ERR_NOENT;
    }

    int result = auth_hash_password(password, user->password_hash);
    if (result == AUTH_OK) {
        kprintf("[AUTH] set_password: password changed for user '%s' (uid=%u)\n",
                user->username, uid);
    }

    return result;
}

user_t* auth_get_user(uint32_t uid) {
    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++) {
        if (users[i].in_use && users[i].uid == uid) {
            return &users[i];
        }
    }
    return NULL;
}

user_t* auth_get_user_by_name(const char *username) {
    if (!username) {
        return NULL;
    }

    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++) {
        if (users[i].in_use && strcmp(users[i].username, username) == 0) {
            return &users[i];
        }
    }
    return NULL;
}

user_t* auth_get_current_user(void) {
    if (current_session && current_session->in_use) {
        return auth_get_user(current_session->uid);
    }
    return NULL;
}

int auth_set_user_flags(uint32_t uid, uint32_t flags) {
    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    user->flags = flags;
    kprintf("[AUTH] set_user_flags: flags=0x%x for user '%s'\n",
            flags, user->username);
    return AUTH_OK;
}

int auth_set_home_dir(uint32_t uid, const char *home_dir) {
    if (!home_dir) {
        return AUTH_ERR_INVAL;
    }

    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    strncpy(user->home_dir, home_dir, AUTH_HOME_DIR_MAX - 1);
    user->home_dir[AUTH_HOME_DIR_MAX - 1] = '\0';
    return AUTH_OK;
}

int auth_set_shell(uint32_t uid, const char *shell) {
    if (!shell) {
        return AUTH_ERR_INVAL;
    }

    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    strncpy(user->shell, shell, AUTH_SHELL_MAX - 1);
    user->shell[AUTH_SHELL_MAX - 1] = '\0';
    return AUTH_OK;
}

int auth_lock_user(uint32_t uid) {
    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    user->flags |= AUTH_USER_LOCKED;
    kprintf("[AUTH] lock_user: locked user '%s' (uid=%u)\n",
            user->username, uid);
    return AUTH_OK;
}

int auth_unlock_user(uint32_t uid) {
    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    user->flags &= ~AUTH_USER_LOCKED;
    user->failed_logins = 0;
    kprintf("[AUTH] unlock_user: unlocked user '%s' (uid=%u)\n",
            user->username, uid);
    return AUTH_OK;
}

/*============================================================================
 * Session Management
 *============================================================================*/

int auth_login(const char *username, const char *password, session_t **session_out) {
    if (!auth_initialized) {
        return AUTH_ERR_INVAL;
    }

    if (!username || !password || !session_out) {
        return AUTH_ERR_INVAL;
    }

    *session_out = NULL;

    /* Verify credentials */
    int result = auth_verify_password(username, password);
    if (result != AUTH_OK) {
        return result;
    }

    user_t *user = auth_get_user_by_name(username);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    /* Check if account is active */
    if (!(user->flags & AUTH_USER_ACTIVE)) {
        kprintf("[AUTH] login: user '%s' account is not active\n", username);
        return AUTH_ERR_PERM;
    }

    /* Find free session slot */
    session_t *session = find_free_session_slot();
    if (!session) {
        kprintf("[AUTH] login: maximum sessions reached\n");
        return AUTH_ERR_MAXSESSIONS;
    }

    /* Initialize session */
    memset(session, 0, sizeof(session_t));
    session->in_use = true;
    session->sid = next_sid++;
    session->uid = user->uid;
    session->start_time = get_timestamp();
    session->last_activity = session->start_time;
    session->flags = SESSION_FLAG_ACTIVE;
    strncpy(session->terminal, "tty0", sizeof(session->terminal) - 1);

    /* Update user info */
    user->last_login = session->start_time;
    user->failed_logins = 0;

    session_count++;
    stats.active_sessions++;
    stats.total_logins++;

    *session_out = session;

    kprintf("[AUTH] login: user '%s' (uid=%u) logged in, session %u\n",
            username, user->uid, session->sid);

    return AUTH_OK;
}

int auth_logout(session_t *session) {
    if (!session || !session->in_use) {
        return AUTH_ERR_INVAL;
    }

    user_t *user = auth_get_user(session->uid);
    const char *username = user ? user->username : "unknown";

    kprintf("[AUTH] logout: user '%s' (uid=%u) session %u ended\n",
            username, session->uid, session->sid);

    /* Clear current session if this is it */
    if (current_session == session) {
        current_session = NULL;
    }

    /* Clear session data */
    uint32_t sid = session->sid;
    memset(session, 0, sizeof(session_t));
    session_count--;
    stats.active_sessions--;
    UNUSED(sid);

    return AUTH_OK;
}

session_t* auth_get_session(uint32_t sid) {
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (sessions[i].in_use && sessions[i].sid == sid) {
            return &sessions[i];
        }
    }
    return NULL;
}

uint32_t auth_get_user_sessions(uint32_t uid, session_t **sessions_out, uint32_t max_sessions) {
    if (!sessions_out || max_sessions == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS && count < max_sessions; i++) {
        if (sessions[i].in_use && sessions[i].uid == uid) {
            sessions_out[count++] = &sessions[i];
        }
    }
    return count;
}

void auth_touch_session(session_t *session) {
    if (session && session->in_use) {
        session->last_activity = get_timestamp();
    }
}

void auth_set_current_session(session_t *session) {
    current_session = session;
}

session_t* auth_get_current_session(void) {
    return current_session;
}

/*============================================================================
 * Group Management
 *============================================================================*/

int auth_create_group(const char *name, uint32_t gid) {
    if (!auth_initialized || !name) {
        return AUTH_ERR_INVAL;
    }

    /* Check name length */
    if (strlen(name) == 0 || strlen(name) >= AUTH_GROUP_NAME_MAX) {
        return AUTH_ERR_INVAL;
    }

    /* Check if name already exists */
    if (auth_get_group_by_name(name) != NULL) {
        kprintf("[AUTH] create_group: group '%s' already exists\n", name);
        return AUTH_ERR_EXISTS;
    }

    /* Check if GID already exists */
    if (gid != AUTH_GID_INVALID && auth_get_group(gid) != NULL) {
        kprintf("[AUTH] create_group: GID %u already exists\n", gid);
        return AUTH_ERR_EXISTS;
    }

    /* Find free slot */
    user_group_t *group = find_free_group_slot();
    if (!group) {
        kprintf("[AUTH] create_group: maximum groups reached\n");
        return AUTH_ERR_MAXGROUPS;
    }

    /* Initialize group */
    memset(group, 0, sizeof(user_group_t));
    group->in_use = true;
    group->gid = (gid != AUTH_GID_INVALID) ? gid : next_gid++;
    strncpy(group->name, name, AUTH_GROUP_NAME_MAX - 1);
    group->name[AUTH_GROUP_NAME_MAX - 1] = '\0';

    group_count++;
    stats.total_groups++;

    kprintf("[AUTH] create_group: created group '%s' (gid=%u)\n",
            name, group->gid);

    return AUTH_OK;
}

int auth_delete_group(uint32_t gid) {
    if (!auth_initialized) {
        return AUTH_ERR_INVAL;
    }

    /* Cannot delete root group */
    if (gid == AUTH_GID_ROOT) {
        kprintf("[AUTH] delete_group: cannot delete root group\n");
        return AUTH_ERR_PERM;
    }

    user_group_t *group = auth_get_group(gid);
    if (!group) {
        return AUTH_ERR_NOENT;
    }

    kprintf("[AUTH] delete_group: deleted group '%s' (gid=%u)\n",
            group->name, gid);

    /* Clear group data */
    memset(group, 0, sizeof(user_group_t));
    group_count--;

    return AUTH_OK;
}

int auth_add_to_group(uint32_t uid, uint32_t gid) {
    user_t *user = auth_get_user(uid);
    if (!user) {
        return AUTH_ERR_NOENT;
    }

    user_group_t *group = auth_get_group(gid);
    if (!group) {
        return AUTH_ERR_NOENT;
    }

    /* Check if already a member */
    if (auth_is_member(uid, gid)) {
        return AUTH_OK;  /* Already a member, not an error */
    }

    /* Add to group's member list */
    if (group->member_count >= AUTH_MAX_GROUP_MEMBERS) {
        kprintf("[AUTH] add_to_group: group '%s' is full\n", group->name);
        return AUTH_ERR_MAXUSERS;
    }
    group->members[group->member_count++] = uid;

    /* Add to user's group list */
    if (user->group_count >= AUTH_MAX_GROUPS) {
        group->member_count--;  /* Rollback */
        kprintf("[AUTH] add_to_group: user '%s' has too many groups\n", user->username);
        return AUTH_ERR_MAXGROUPS;
    }
    user->groups[user->group_count++] = gid;

    kprintf("[AUTH] add_to_group: added user '%s' to group '%s'\n",
            user->username, group->name);

    return AUTH_OK;
}

int auth_remove_from_group(uint32_t uid, uint32_t gid) {
    user_t *user = auth_get_user(uid);
    user_group_t *group = auth_get_group(gid);

    if (!user || !group) {
        return AUTH_ERR_NOENT;
    }

    /* Remove from group's member list */
    for (uint32_t i = 0; i < group->member_count; i++) {
        if (group->members[i] == uid) {
            /* Shift remaining members */
            for (uint32_t j = i; j < group->member_count - 1; j++) {
                group->members[j] = group->members[j + 1];
            }
            group->member_count--;
            break;
        }
    }

    /* Remove from user's group list */
    for (uint32_t i = 0; i < user->group_count; i++) {
        if (user->groups[i] == gid) {
            /* Shift remaining groups */
            for (uint32_t j = i; j < user->group_count - 1; j++) {
                user->groups[j] = user->groups[j + 1];
            }
            user->group_count--;
            break;
        }
    }

    return AUTH_OK;
}

bool auth_is_member(uint32_t uid, uint32_t gid) {
    user_t *user = auth_get_user(uid);
    if (!user) {
        return false;
    }

    /* Check primary group */
    if (user->gid == gid) {
        return true;
    }

    /* Check secondary groups */
    for (uint32_t i = 0; i < user->group_count; i++) {
        if (user->groups[i] == gid) {
            return true;
        }
    }

    return false;
}

user_group_t* auth_get_group(uint32_t gid) {
    for (uint32_t i = 0; i < AUTH_MAX_GROUPS; i++) {
        if (groups[i].in_use && groups[i].gid == gid) {
            return &groups[i];
        }
    }
    return NULL;
}

user_group_t* auth_get_group_by_name(const char *name) {
    if (!name) {
        return NULL;
    }

    for (uint32_t i = 0; i < AUTH_MAX_GROUPS; i++) {
        if (groups[i].in_use && strcmp(groups[i].name, name) == 0) {
            return &groups[i];
        }
    }
    return NULL;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

bool auth_is_admin(uint32_t uid) {
    if (uid == AUTH_UID_ROOT) {
        return true;
    }

    user_t *user = auth_get_user(uid);
    if (user && (user->flags & AUTH_USER_ADMIN)) {
        return true;
    }

    return false;
}

bool auth_is_root(uint32_t uid) {
    return (uid == AUTH_UID_ROOT);
}

void auth_get_stats(auth_stats_t *stats_out) {
    if (stats_out) {
        stats_out->total_users = user_count;
        stats_out->active_sessions = session_count;
        stats_out->total_logins = stats.total_logins;
        stats_out->failed_logins = stats.failed_logins;
        stats_out->total_groups = group_count;
    }
}

const char* auth_strerror(int error) {
    switch (error) {
        case AUTH_OK:           return "Success";
        case AUTH_ERR_NOMEM:    return "Out of memory";
        case AUTH_ERR_EXISTS:   return "Already exists";
        case AUTH_ERR_NOENT:    return "Not found";
        case AUTH_ERR_INVAL:    return "Invalid argument";
        case AUTH_ERR_PERM:     return "Permission denied";
        case AUTH_ERR_BADPASS:  return "Invalid password";
        case AUTH_ERR_MAXUSERS: return "Maximum users reached";
        case AUTH_ERR_MAXSESSIONS: return "Maximum sessions reached";
        case AUTH_ERR_MAXGROUPS: return "Maximum groups reached";
        case AUTH_ERR_LOCKED:   return "Account locked";
        case AUTH_ERR_EXPIRED:  return "Password/account expired";
        default:                return "Unknown error";
    }
}

void auth_dump_users(void) {
    kprintf("[AUTH] === User Database ===\n");
    kprintf("[AUTH] Total users: %u\n", user_count);

    for (uint32_t i = 0; i < AUTH_MAX_USERS; i++) {
        if (users[i].in_use) {
            kprintf("[AUTH]   User: %s (uid=%u, gid=%u, flags=0x%x)\n",
                    users[i].username, users[i].uid,
                    users[i].gid, users[i].flags);
            kprintf("[AUTH]         home=%s, shell=%s\n",
                    users[i].home_dir, users[i].shell);
            kprintf("[AUTH]         groups=%u, failed_logins=%u\n",
                    users[i].group_count, users[i].failed_logins);
        }
    }
}

void auth_dump_sessions(void) {
    kprintf("[AUTH] === Active Sessions ===\n");
    kprintf("[AUTH] Total sessions: %u\n", session_count);

    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (sessions[i].in_use) {
            user_t *user = auth_get_user(sessions[i].uid);
            kprintf("[AUTH]   Session %u: user=%s (uid=%u), terminal=%s\n",
                    sessions[i].sid,
                    user ? user->username : "unknown",
                    sessions[i].uid,
                    sessions[i].terminal);
        }
    }
}

void auth_dump_groups(void) {
    kprintf("[AUTH] === Group Database ===\n");
    kprintf("[AUTH] Total groups: %u\n", group_count);

    for (uint32_t i = 0; i < AUTH_MAX_GROUPS; i++) {
        if (groups[i].in_use) {
            kprintf("[AUTH]   Group: %s (gid=%u, members=%u)\n",
                    groups[i].name, groups[i].gid, groups[i].member_count);
        }
    }
}

/*============================================================================
 * Initialization
 *============================================================================*/

int auth_init(void) {
    if (auth_initialized) {
        kprintf("[AUTH] Already initialized\n");
        return AUTH_OK;
    }

    kprintf("[AUTH] Initializing authentication subsystem...\n");

    /* Clear all data structures */
    memset(users, 0, sizeof(users));
    memset(sessions, 0, sizeof(sessions));
    memset(groups, 0, sizeof(groups));
    memset(&stats, 0, sizeof(stats));

    user_count = 0;
    session_count = 0;
    group_count = 0;
    current_session = NULL;

    /* Seed random generator (in real system, use hardware RNG or RTC) */
    random_seed(0x1234567890ABCDEFULL);

    auth_initialized = true;

    /* Create default groups */
    kprintf("[AUTH] Creating default groups...\n");
    auth_create_group("root", AUTH_GID_ROOT);
    auth_create_group("users", AUTH_GID_USERS);

    /* Create default users */
    kprintf("[AUTH] Creating default users...\n");

    /* Create root user */
    int result = auth_create_user("root", "root", AUTH_UID_ROOT);
    if (result == AUTH_OK) {
        user_t *root = auth_get_user(AUTH_UID_ROOT);
        if (root) {
            root->gid = AUTH_GID_ROOT;
            root->flags = AUTH_USER_ACTIVE | AUTH_USER_ADMIN;
            strncpy(root->home_dir, "/root", AUTH_HOME_DIR_MAX - 1);
            auth_add_to_group(AUTH_UID_ROOT, AUTH_GID_ROOT);
        }
    } else {
        kprintf("[AUTH] ERROR: Failed to create root user: %s\n", auth_strerror(result));
    }

    /* Create guest user */
    result = auth_create_user("guest", "guest", AUTH_UID_GUEST);
    if (result == AUTH_OK) {
        user_t *guest = auth_get_user(AUTH_UID_GUEST);
        if (guest) {
            guest->gid = AUTH_GID_USERS;
            guest->flags = AUTH_USER_ACTIVE;
            auth_add_to_group(AUTH_UID_GUEST, AUTH_GID_USERS);
        }
    } else {
        kprintf("[AUTH] ERROR: Failed to create guest user: %s\n", auth_strerror(result));
    }

    kprintf("[AUTH] Authentication subsystem initialized successfully\n");
    kprintf("[AUTH] Default users: root (uid=0), guest (uid=1000)\n");
    kprintf("[AUTH] Default groups: root (gid=0), users (gid=100)\n");

    auth_dump_users();
    auth_dump_groups();

    return AUTH_OK;
}

void auth_shutdown(void) {
    if (!auth_initialized) {
        return;
    }

    kprintf("[AUTH] Shutting down authentication subsystem...\n");

    /* Logout all sessions */
    for (uint32_t i = 0; i < AUTH_MAX_SESSIONS; i++) {
        if (sessions[i].in_use) {
            auth_logout(&sessions[i]);
        }
    }

    /* Clear sensitive data */
    memset(users, 0, sizeof(users));
    memset(sessions, 0, sizeof(sessions));
    memset(groups, 0, sizeof(groups));

    auth_initialized = false;
    kprintf("[AUTH] Authentication subsystem shut down\n");
}
