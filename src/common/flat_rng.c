#include "flat_rng.h"

#include "flat_time.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

/* splitmix64 advancing state — fast, good enough for non-crypto FlatL RNG. */
static uint64_t g_state = 0x853c49e6748fea9bULL;
static int g_seeded;

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void flat_rng_seed_u64(uint64_t seed) {
    if (seed == 0) {
        seed = 0x9E3779B97F4A7C15ULL;
    }
    g_state = seed;
    (void)splitmix64_next(&g_state);
    (void)splitmix64_next(&g_state);
    g_seeded = 1;
}

void flat_rng_seed_auto(void) {
    uint64_t s = (uint64_t)time(NULL);
    double ms = flat_time_monotonic_ms();
    uint64_t mix = 0;
    memcpy(&mix, &ms, sizeof mix > sizeof ms ? sizeof ms : sizeof mix);
    s ^= mix;
    s ^= (uint64_t)(uintptr_t)&s;
    flat_rng_seed_u64(s);
}

uint64_t flat_rng_u64(void) {
    if (!g_seeded) {
        flat_rng_seed_auto();
    }
    return splitmix64_next(&g_state);
}

double flat_rng_f64_unit(void) {
    /* Top 53 bits → [0, 1). */
    return (double)(flat_rng_u64() >> 11) * (1.0 / 9007199254740992.0);
}

void flat_rng_fill(void *buf, size_t n) {
    unsigned char *p = (unsigned char *)buf;
    size_t i = 0;
    while (i < n) {
        uint64_t r = flat_rng_u64();
        size_t k;
        for (k = 0; k < 8 && i < n; k++, i++) {
            p[i] = (unsigned char)(r & 0xffu);
            r >>= 8;
        }
    }
}

uint64_t flat_rng_u64_range(uint64_t lo, uint64_t hi) {
    uint64_t span;
    uint64_t threshold;
    uint64_t r;
    if (lo >= hi) {
        return lo;
    }
    span = hi - lo; /* hi-lo fits in u64 when hi > lo */
    /* Want values in [0, span] inclusive → span+1 possibilities. */
    if (span == UINT64_MAX) {
        return flat_rng_u64();
    }
    span += 1u;
    threshold = (UINT64_MAX / span) * span;
    do {
        r = flat_rng_u64();
    } while (r >= threshold);
    return lo + (r % span);
}
