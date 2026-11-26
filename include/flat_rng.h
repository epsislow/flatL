#ifndef FLAT_RNG_H
#define FLAT_RNG_H

#include <stddef.h>
#include <stdint.h>

/* Per-VM RNG state. Not cryptographic. */
typedef struct {
    uint64_t state;
    int seeded;
} FlatRngState;

void flat_rng_seed_u64(FlatRngState *rng, uint64_t seed);
void flat_rng_seed_auto(FlatRngState *rng);

uint64_t flat_rng_u64(FlatRngState *rng);
/* Uniform in [0, 1). */
double flat_rng_f64_unit(FlatRngState *rng);
/* Fill buf with random bytes. */
void flat_rng_fill(FlatRngState *rng, void *buf, size_t n);

/* Uniform integer in [lo, hi] inclusive (requires lo <= hi).
 * Uses rejection sampling to avoid modulo bias.
 */
uint64_t flat_rng_u64_range(FlatRngState *rng, uint64_t lo, uint64_t hi);

#endif
