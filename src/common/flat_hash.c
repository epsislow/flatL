#include "flat_hash.h"

#include "WjCryptLib_Md5.h"
#include "WjCryptLib_Sha1.h"
#include "WjCryptLib_Sha256.h"
#include "WjCryptLib_Sha512.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void set_why(char *why, size_t why_sz, const char *msg) {
    if (why && why_sz > 0) {
        snprintf(why, why_sz, "%s", msg ? msg : "hash");
    }
}

void flat_hash_hex(const unsigned char *digest, size_t digest_len, char *out) {
    static const char H[] = "0123456789abcdef";
    size_t i;
    if (!out) {
        return;
    }
    if (!digest) {
        out[0] = '\0';
        return;
    }
    for (i = 0; i < digest_len; i++) {
        out[i * 2] = H[digest[i] >> 4];
        out[i * 2 + 1] = H[digest[i] & 0xf];
    }
    out[digest_len * 2] = '\0';
}

/* SHA-384 = SHA-512 compress with FIPS 180-4 IVs; output first 48 bytes. */
static void sha384_init(Sha512Context *ctx) {
    ctx->length = 0;
    ctx->curlen = 0;
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
}

static void feed_md5(Md5Context *ctx, const unsigned char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 0xffffffffu) {
            chunk = 0xffffffffu;
        }
        Md5Update(ctx, data + off, (uint32_t)chunk);
        off += chunk;
    }
}

static void feed_sha1(Sha1Context *ctx, const unsigned char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 0xffffffffu) {
            chunk = 0xffffffffu;
        }
        Sha1Update(ctx, data + off, (uint32_t)chunk);
        off += chunk;
    }
}

static void feed_sha256(Sha256Context *ctx, const unsigned char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 0xffffffffu) {
            chunk = 0xffffffffu;
        }
        Sha256Update(ctx, data + off, (uint32_t)chunk);
        off += chunk;
    }
}

static void feed_sha512(Sha512Context *ctx, const unsigned char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 0xffffffffu) {
            chunk = 0xffffffffu;
        }
        Sha512Update(ctx, data + off, (uint32_t)chunk);
        off += chunk;
    }
}

int flat_hash_digest(const char *algo, const unsigned char *data, size_t len, unsigned char *out,
                     size_t *out_len, char *why, size_t why_sz) {
    if (why && why_sz > 0) {
        why[0] = '\0';
    }
    if (!algo || !out || !out_len) {
        set_why(why, why_sz, "hash.digest.algo");
        return -1;
    }
    if (!data && len) {
        set_why(why, why_sz, "hash.digest.args");
        return -1;
    }
    *out_len = 0;

    if (strcmp(algo, "md5") == 0) {
        Md5Context ctx;
        MD5_HASH dig;
        Md5Initialise(&ctx);
        feed_md5(&ctx, data, len);
        Md5Finalise(&ctx, &dig);
        memcpy(out, dig.bytes, MD5_HASH_SIZE);
        *out_len = MD5_HASH_SIZE;
        return 0;
    }
    if (strcmp(algo, "sha1") == 0) {
        Sha1Context ctx;
        SHA1_HASH dig;
        Sha1Initialise(&ctx);
        feed_sha1(&ctx, data, len);
        Sha1Finalise(&ctx, &dig);
        memcpy(out, dig.bytes, SHA1_HASH_SIZE);
        *out_len = SHA1_HASH_SIZE;
        return 0;
    }
    if (strcmp(algo, "sha256") == 0) {
        Sha256Context ctx;
        SHA256_HASH dig;
        Sha256Initialise(&ctx);
        feed_sha256(&ctx, data, len);
        Sha256Finalise(&ctx, &dig);
        memcpy(out, dig.bytes, SHA256_HASH_SIZE);
        *out_len = SHA256_HASH_SIZE;
        return 0;
    }
    if (strcmp(algo, "sha384") == 0) {
        Sha512Context ctx;
        SHA512_HASH dig;
        sha384_init(&ctx);
        feed_sha512(&ctx, data, len);
        Sha512Finalise(&ctx, &dig);
        memcpy(out, dig.bytes, 48);
        *out_len = 48;
        return 0;
    }
    if (strcmp(algo, "sha512") == 0) {
        Sha512Context ctx;
        SHA512_HASH dig;
        Sha512Initialise(&ctx);
        feed_sha512(&ctx, data, len);
        Sha512Finalise(&ctx, &dig);
        memcpy(out, dig.bytes, SHA512_HASH_SIZE);
        *out_len = SHA512_HASH_SIZE;
        return 0;
    }

    set_why(why, why_sz, "hash.digest.algo");
    return -1;
}
