/**
 * AAAos Security - SHA-256 Hash Implementation
 *
 * Implements the SHA-256 cryptographic hash function as defined in
 * FIPS 180-4 (Secure Hash Standard).
 *
 * SHA-256 produces a 256-bit (32-byte) message digest and is widely
 * used for data integrity verification, digital signatures, and
 * password hashing.
 */

#include "crypto.h"
#include "../../kernel/include/serial.h"
#include "../../lib/libc/string.h"

/*============================================================================
 * SHA-256 Constants
 *============================================================================*/

/**
 * Initial hash values (H0-H7)
 *
 * These are the first 32 bits of the fractional parts of the square roots
 * of the first 8 prime numbers (2, 3, 5, 7, 11, 13, 17, 19).
 */
static const uint32_t sha256_h0[8] = {
    0x6a09e667,     /* H0 - sqrt(2)  */
    0xbb67ae85,     /* H1 - sqrt(3)  */
    0x3c6ef372,     /* H2 - sqrt(5)  */
    0xa54ff53a,     /* H3 - sqrt(7)  */
    0x510e527f,     /* H4 - sqrt(11) */
    0x9b05688c,     /* H5 - sqrt(13) */
    0x1f83d9ab,     /* H6 - sqrt(17) */
    0x5be0cd19      /* H7 - sqrt(19) */
};

/**
 * Round constants (K0-K63)
 *
 * These are the first 32 bits of the fractional parts of the cube roots
 * of the first 64 prime numbers (2-311).
 */
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

/*============================================================================
 * SHA-256 Helper Macros
 *============================================================================*/

/* Right rotate (circular right shift) */
#define ROTR(x, n)      (((x) >> (n)) | ((x) << (32 - (n))))

/* Right shift */
#define SHR(x, n)       ((x) >> (n))

/* SHA-256 logical functions */
#define CH(x, y, z)     (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)    (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* SHA-256 sigma functions */
#define SIGMA0(x)       (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x)       (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x)       (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define sigma1(x)       (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

/*============================================================================
 * SHA-256 Internal Functions
 *============================================================================*/

/**
 * Process a single 512-bit (64-byte) block
 *
 * @param ctx SHA-256 context
 * @param block Pointer to 64-byte block to process
 */
static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];     /* Message schedule */
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    int i;

    /* Prepare message schedule */
    /* First 16 words are directly from the input block (big-endian) */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4 + 0] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }

    /* Remaining 48 words are derived from earlier words */
    for (i = 16; i < 64; i++) {
        w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
    }

    /* Initialize working variables with current hash value */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    /* Main compression loop - 64 rounds */
    for (i = 0; i < 64; i++) {
        t1 = h + SIGMA1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        t2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    /* Add compressed chunk to current hash value */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

/*============================================================================
 * SHA-256 Public API
 *============================================================================*/

/**
 * Initialize SHA-256 context
 */
void sha256_init(sha256_ctx_t *ctx)
{
    if (!ctx) {
        kprintf("[SHA256] Error: NULL context\n");
        return;
    }

    /* Set initial hash values */
    ctx->state[0] = sha256_h0[0];
    ctx->state[1] = sha256_h0[1];
    ctx->state[2] = sha256_h0[2];
    ctx->state[3] = sha256_h0[3];
    ctx->state[4] = sha256_h0[4];
    ctx->state[5] = sha256_h0[5];
    ctx->state[6] = sha256_h0[6];
    ctx->state[7] = sha256_h0[7];

    /* Reset byte count and buffer */
    ctx->count = 0;
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);

    kprintf("[SHA256] Context initialized\n");
}

/**
 * Add data to SHA-256 hash computation
 */
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t buffer_used;
    size_t buffer_free;

    if (!ctx || !data || len == 0) {
        return;
    }

    /* Calculate how much of the buffer is already used */
    buffer_used = ctx->count % SHA256_BLOCK_SIZE;
    buffer_free = SHA256_BLOCK_SIZE - buffer_used;

    /* Update total byte count */
    ctx->count += len;

    /* If we have buffered data and new data fills the buffer, process it */
    if (buffer_used > 0 && len >= buffer_free) {
        memcpy(ctx->buffer + buffer_used, p, buffer_free);
        sha256_transform(ctx, ctx->buffer);
        p += buffer_free;
        len -= buffer_free;
        buffer_used = 0;
    }

    /* Process complete blocks directly from input */
    while (len >= SHA256_BLOCK_SIZE) {
        sha256_transform(ctx, p);
        p += SHA256_BLOCK_SIZE;
        len -= SHA256_BLOCK_SIZE;
    }

    /* Buffer remaining data */
    if (len > 0) {
        memcpy(ctx->buffer + buffer_used, p, len);
    }
}

/**
 * Finalize SHA-256 hash and retrieve result
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32])
{
    uint64_t total_bits;
    size_t buffer_used;
    size_t padding_len;
    uint8_t padding[128];
    int i;

    if (!ctx || !hash) {
        kprintf("[SHA256] Error: NULL parameter in final\n");
        return;
    }

    /* Calculate total message length in bits */
    total_bits = ctx->count * 8;

    /* Determine padding length */
    buffer_used = ctx->count % SHA256_BLOCK_SIZE;

    /*
     * Padding: append 1 bit (0x80), then zeros, then 64-bit length
     * Total padded message must be multiple of 512 bits (64 bytes)
     *
     * If buffer_used < 56: pad to 56 bytes, then add 8-byte length
     * If buffer_used >= 56: pad to 120 bytes (64 + 56), then add 8-byte length
     */
    if (buffer_used < 56) {
        padding_len = 56 - buffer_used;
    } else {
        padding_len = 120 - buffer_used;
    }

    /* Build padding block */
    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80;  /* Append 1 bit */

    /* Append message length in bits as big-endian 64-bit value */
    padding[padding_len + 0] = (uint8_t)(total_bits >> 56);
    padding[padding_len + 1] = (uint8_t)(total_bits >> 48);
    padding[padding_len + 2] = (uint8_t)(total_bits >> 40);
    padding[padding_len + 3] = (uint8_t)(total_bits >> 32);
    padding[padding_len + 4] = (uint8_t)(total_bits >> 24);
    padding[padding_len + 5] = (uint8_t)(total_bits >> 16);
    padding[padding_len + 6] = (uint8_t)(total_bits >> 8);
    padding[padding_len + 7] = (uint8_t)(total_bits);

    /* Process padding (one or two blocks) */
    sha256_update(ctx, padding, padding_len + 8);

    /* Output hash in big-endian format */
    for (i = 0; i < 8; i++) {
        hash[i * 4 + 0] = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }

    /* Clear sensitive data */
    memset(ctx, 0, sizeof(sha256_ctx_t));

    kprintf("[SHA256] Hash finalized\n");
}

/**
 * Compute SHA-256 hash in one shot
 */
void sha256_hash(const void *data, size_t len, uint8_t hash[32])
{
    sha256_ctx_t ctx;

    if (!data || !hash) {
        kprintf("[SHA256] Error: NULL parameter in one-shot hash\n");
        return;
    }

    kprintf("[SHA256] Computing hash of %u bytes\n", (uint32_t)len);

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash);
}
