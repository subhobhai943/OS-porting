/**
 * AAAos Security - Pseudo-Random Number Generator
 *
 * Implements a fast, high-quality PRNG using the xorshift128+ algorithm.
 *
 * xorshift128+ features:
 * - Period: 2^128 - 1
 * - Fast: only 3 XOR and 3 shift operations per 64-bit output
 * - Passes BigCrush statistical tests
 * - Simple state: two 64-bit values
 *
 * Note: This is NOT cryptographically secure for security-critical
 * applications. For cryptographic randomness, use a dedicated CSPRNG.
 */

#include "crypto.h"
#include "../../kernel/include/serial.h"
#include "../../drivers/timer/pit.h"

/*============================================================================
 * PRNG State
 *============================================================================*/

/* Global PRNG state */
static prng_state_t prng_state = {
    .s = { 0x0123456789abcdef, 0xfedcba9876543210 }  /* Default seed */
};

/* Flag to track initialization */
static bool prng_initialized = false;

/*============================================================================
 * xorshift128+ Algorithm
 *============================================================================*/

/**
 * Generate next 64-bit random value using xorshift128+
 *
 * The algorithm:
 * 1. s1 ^= s1 << 23
 * 2. s1 ^= s1 >> 17
 * 3. s1 ^= s0 ^ (s0 >> 26)
 * 4. swap s0 and s1
 * 5. return s0 + s1
 *
 * @return 64-bit pseudo-random value
 */
static uint64_t xorshift128plus(void)
{
    uint64_t s0 = prng_state.s[0];
    uint64_t s1 = prng_state.s[1];
    uint64_t result = s0 + s1;

    /* Compute new s1 */
    s1 ^= s1 << 23;
    s1 ^= s1 >> 17;
    s1 ^= s0 ^ (s0 >> 26);

    /* Update state */
    prng_state.s[0] = prng_state.s[1];
    prng_state.s[1] = s1;

    return result;
}

/**
 * Mix seed using splitmix64 algorithm
 *
 * This provides good initial state distribution from a single 64-bit seed.
 * Used to expand the seed into the two-part xorshift128+ state.
 *
 * @param x Current state value (modified in place)
 * @return Next value in sequence
 */
static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

/*============================================================================
 * PRNG Public API
 *============================================================================*/

/**
 * Initialize PRNG with a seed
 */
void random_init(uint64_t seed)
{
    kprintf("[RANDOM] Initializing PRNG with seed: 0x%016llx\n", seed);

    /* Use splitmix64 to expand single seed into two-part state */
    /* This ensures good state distribution even from poor seeds */
    prng_state.s[0] = splitmix64(&seed);
    prng_state.s[1] = splitmix64(&seed);

    /* Ensure state is not all zeros (would produce all zeros output) */
    if (prng_state.s[0] == 0 && prng_state.s[1] == 0) {
        prng_state.s[0] = 0x0123456789abcdef;
        prng_state.s[1] = 0xfedcba9876543210;
        kprintf("[RANDOM] Warning: Zero state detected, using default\n");
    }

    prng_initialized = true;

    /* Warm up the generator by discarding first few values */
    /* This helps mix the state if the seed had low entropy */
    for (int i = 0; i < 16; i++) {
        xorshift128plus();
    }

    kprintf("[RANDOM] PRNG initialized successfully\n");
    kprintf("[RANDOM] State: s0=0x%016llx, s1=0x%016llx\n",
            prng_state.s[0], prng_state.s[1]);
}

/**
 * Seed PRNG from system time
 */
void random_seed_from_time(void)
{
    uint64_t seed;
    uint64_t ticks;
    uint64_t uptime;

    kprintf("[RANDOM] Seeding from system time...\n");

    /* Get timing information */
    ticks = pit_get_ticks();
    uptime = pit_get_uptime_ms();

    /* Combine timing sources for entropy */
    /* Mix ticks and uptime to create seed */
    seed = ticks;
    seed ^= uptime << 17;
    seed ^= ticks >> 7;
    seed ^= uptime << 43;

    /* Add some additional mixing */
    seed = (seed ^ (seed >> 33)) * 0xff51afd7ed558ccd;
    seed = (seed ^ (seed >> 33)) * 0xc4ceb9fe1a85ec53;
    seed ^= seed >> 33;

    kprintf("[RANDOM] Time-based seed: ticks=%llu, uptime=%llu ms\n",
            ticks, uptime);

    random_init(seed);
}

/**
 * Fill buffer with random bytes
 */
void random_get_bytes(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    uint64_t value;
    size_t i;

    if (!buf || len == 0) {
        kprintf("[RANDOM] Error: Invalid parameters to random_get_bytes\n");
        return;
    }

    if (!prng_initialized) {
        kprintf("[RANDOM] Warning: PRNG not seeded, using default state\n");
    }

    /* Generate random bytes */
    while (len >= 8) {
        value = xorshift128plus();
        for (i = 0; i < 8; i++) {
            *p++ = (uint8_t)(value >> (i * 8));
        }
        len -= 8;
    }

    /* Handle remaining bytes */
    if (len > 0) {
        value = xorshift128plus();
        for (i = 0; i < len; i++) {
            *p++ = (uint8_t)(value >> (i * 8));
        }
    }
}

/**
 * Get a random 32-bit unsigned integer
 */
uint32_t random_uint32(void)
{
    if (!prng_initialized) {
        kprintf("[RANDOM] Warning: PRNG not seeded, using default state\n");
    }

    /* Return lower 32 bits of 64-bit output */
    return (uint32_t)xorshift128plus();
}

/**
 * Get a random 64-bit unsigned integer
 */
uint64_t random_uint64(void)
{
    if (!prng_initialized) {
        kprintf("[RANDOM] Warning: PRNG not seeded, using default state\n");
    }

    return xorshift128plus();
}

/**
 * Get a random number in a specified range
 *
 * Uses rejection sampling to ensure uniform distribution.
 * This avoids bias that would occur with simple modulo.
 */
uint32_t random_range(uint32_t min, uint32_t max)
{
    uint32_t range;
    uint32_t limit;
    uint32_t value;

    if (!prng_initialized) {
        kprintf("[RANDOM] Warning: PRNG not seeded, using default state\n");
    }

    /* Handle edge cases */
    if (min >= max) {
        return min;
    }

    /* Calculate range size */
    range = max - min + 1;

    /* Handle full range case */
    if (range == 0) {
        /* Overflow: range is 2^32 */
        return random_uint32();
    }

    /*
     * Rejection sampling for uniform distribution:
     *
     * Simple modulo (random % range) produces biased results when
     * UINT32_MAX+1 is not evenly divisible by range.
     *
     * Example: range=3, values 0-4 give 0,1,2,0,1 - uneven!
     *
     * Solution: reject values >= (UINT32_MAX - UINT32_MAX % range)
     * This discards the "partial bucket" at the end.
     */
    limit = UINT32_MAX - (UINT32_MAX % range);

    do {
        value = random_uint32();
    } while (value >= limit);

    return min + (value % range);
}

/**
 * Get the current PRNG state (for debugging/testing)
 */
const prng_state_t *random_get_state(void)
{
    return &prng_state;
}
