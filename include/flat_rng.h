#ifndef FLAT_RNG_H
#define FLAT_RNG_H

#include <stddef.h>
#include <stdint.h>

/*
 * Shared process RNG (math.random / hash.randomBytes).
 * Not cryptographic. Auto-seeded at VM start; math.seed overrides.
 */

void flat_rng_seed_u64(uint64_t seed);
void flat_rng_seed_auto(void);

uint64_t flat_rng_u64(void);
/* Uniform in [0, 1). */
double flat_rng_f64_unit(void);
/* Fill buf with random bytes. */
void flat_rng_fill(void *buf, size_t n);

/*
 * Uniform integer in [lo, hi] inclusive (requires lo <= hi).
 * Uses rejection sampling to avoid modulo bias.
 */
uint64_t flat_rng_u64_range(uint64_t lo, uint64_t hi);

#endif
