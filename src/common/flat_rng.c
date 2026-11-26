#include "flat_rng.h"

#include "flat_time.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#define FLAT_RNG_DEFAULT_STATE UINT64_C(0x853c49e6748fea9b)

/* splitmix64 advancing state — fast, good enough for non-crypto FlatL RNG. */
static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

void flat_rng_seed_u64(FlatRngState *rng, uint64_t seed) {
    if (!rng) {
        return;
    }
    if (seed == 0) {
        seed = UINT64_C(0x9E3779B97F4A7C15);
    }
    rng->state = seed;
    (void)splitmix64_next(&rng->state);
    (void)splitmix64_next(&rng->state);
    rng->seeded = 1;
}

void flat_rng_seed_auto(FlatRngState *rng) {
    uint64_t s = (uint64_t)time(NULL);
    double ms = flat_time_monotonic_ms();
    uint64_t mix = 0;
    if (!rng) {
        return;
    }
    memcpy(&mix, &ms, sizeof mix > sizeof ms ? sizeof ms : sizeof mix);
    s ^= mix;
    s ^= (uint64_t)(uintptr_t)rng;
    flat_rng_seed_u64(rng, s);
}

uint64_t flat_rng_u64(FlatRngState *rng) {
    if (!rng) {
        return 0;
    }
    if (!rng->seeded) {
        rng->state = FLAT_RNG_DEFAULT_STATE;
        flat_rng_seed_auto(rng);
    }
    return splitmix64_next(&rng->state);
}

double flat_rng_f64_unit(FlatRngState *rng) {
    /* Top 53 bits → [0, 1). */
    return (double)(flat_rng_u64(rng) >> 11) * (1.0 / 9007199254740992.0);
}

void flat_rng_fill(FlatRngState *rng, void *buf, size_t n) {
    unsigned char *p = (unsigned char *)buf;
    size_t i = 0;
    while (p && i < n) {
        uint64_t r = flat_rng_u64(rng);
        size_t k;
        for (k = 0; k < 8 && i < n; k++, i++) {
            p[i] = (unsigned char)(r & 0xffu);
            r >>= 8;
        }
    }
}

uint64_t flat_rng_u64_range(FlatRngState *rng, uint64_t lo, uint64_t hi) {
    uint64_t span;
    uint64_t threshold;
    uint64_t r;
    if (lo >= hi) {
        return lo;
    }
    span = hi - lo; /* hi-lo fits in u64 when hi > lo */
    /* Want values in [0, span] inclusive → span+1 possibilities. */
    if (span == UINT64_MAX) {
        return flat_rng_u64(rng);
    }
    span += 1u;
    threshold = (UINT64_MAX / span) * span;
    do {
        r = flat_rng_u64(rng);
    } while (r >= threshold);
    return lo + (r % span);
}
