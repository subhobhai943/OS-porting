/**
 * AAAos Security - Cryptographic Functions
 *
 * This header provides cryptographic primitives for the AAAos kernel:
 * - SHA-256 hash function
 * - Pseudo-random number generator (xorshift128+)
 */

#ifndef _AAAOS_CRYPTO_H
#define _AAAOS_CRYPTO_H

#include "../../kernel/include/types.h"

/*============================================================================
 * SHA-256 Hash Function
 *============================================================================*/

/* SHA-256 produces a 256-bit (32-byte) hash */
#define SHA256_DIGEST_SIZE      32
#define SHA256_BLOCK_SIZE       64

/**
 * SHA-256 context structure
 *
 * Holds the intermediate state during hash computation.
 */
typedef struct {
    uint32_t state[8];      /* Hash state (H0-H7) */
    uint64_t count;         /* Total bytes processed */
    uint8_t buffer[64];     /* Input buffer (one block) */
} sha256_ctx_t;

/**
 * Initialize SHA-256 context
 *
 * Must be called before adding data to the hash.
 *
 * @param ctx Pointer to SHA-256 context structure
 */
void sha256_init(sha256_ctx_t *ctx);

/**
 * Add data to SHA-256 hash computation
 *
 * Can be called multiple times to incrementally hash data.
 *
 * @param ctx Pointer to initialized SHA-256 context
 * @param data Pointer to data to hash
 * @param len Length of data in bytes
 */
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);

/**
 * Finalize SHA-256 hash and retrieve result
 *
 * Completes the hash computation and writes the final hash to the output buffer.
 * The context should not be used after calling this function.
 *
 * @param ctx Pointer to SHA-256 context
 * @param hash Output buffer for 32-byte hash result
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]);

/**
 * Compute SHA-256 hash in one shot
 *
 * Convenience function for hashing data in a single call.
 *
 * @param data Pointer to data to hash
 * @param len Length of data in bytes
 * @param hash Output buffer for 32-byte hash result
 */
void sha256_hash(const void *data, size_t len, uint8_t hash[32]);

/*============================================================================
 * Pseudo-Random Number Generator (xorshift128+)
 *============================================================================*/

/**
 * PRNG state structure
 *
 * Contains the internal state for the xorshift128+ algorithm.
 */
typedef struct {
    uint64_t s[2];          /* Two 64-bit state values */
} prng_state_t;

/**
 * Initialize PRNG with a seed
 *
 * Seeds the random number generator with the provided value.
 * The seed is expanded to fill the internal state.
 *
 * @param seed 64-bit seed value
 */
void random_init(uint64_t seed);

/**
 * Seed PRNG from system time
 *
 * Uses the current system uptime to seed the PRNG.
 * Should be called after the timer is initialized.
 */
void random_seed_from_time(void);

/**
 * Fill buffer with random bytes
 *
 * Generates cryptographically unpredictable random bytes.
 *
 * @param buf Pointer to output buffer
 * @param len Number of bytes to generate
 */
void random_get_bytes(void *buf, size_t len);

/**
 * Get a random 32-bit unsigned integer
 *
 * @return Random value in range [0, UINT32_MAX]
 */
uint32_t random_uint32(void);

/**
 * Get a random 64-bit unsigned integer
 *
 * @return Random value in range [0, UINT64_MAX]
 */
uint64_t random_uint64(void);

/**
 * Get a random number in a specified range
 *
 * Returns a uniformly distributed random value in [min, max].
 *
 * @param min Minimum value (inclusive)
 * @param max Maximum value (inclusive)
 * @return Random value in range [min, max]
 */
uint32_t random_range(uint32_t min, uint32_t max);

/**
 * Get the current PRNG state (for debugging/testing)
 *
 * @return Pointer to current PRNG state
 */
const prng_state_t *random_get_state(void);

#endif /* _AAAOS_CRYPTO_H */
