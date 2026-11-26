#include "flat_rng.h"

#include <stdio.h>

int main(void) {
    FlatRngState a = {0};
    FlatRngState b = {0};
    FlatRngState expected_a = {0};
    FlatRngState expected_b = {0};
    uint64_t a_first;
    uint64_t a_second;
    uint64_t b_first;
    uint64_t b_second;

    flat_rng_seed_u64(&a, 42u);
    flat_rng_seed_u64(&b, 99u);
    flat_rng_seed_u64(&expected_a, 42u);
    flat_rng_seed_u64(&expected_b, 99u);

    a_first = flat_rng_u64(&a);
    b_first = flat_rng_u64(&b);
    a_second = flat_rng_u64(&a);
    b_second = flat_rng_u64(&b);

    if (a_first != flat_rng_u64(&expected_a) ||
        a_second != flat_rng_u64(&expected_a)) {
        return 1;
    }
    if (b_first != flat_rng_u64(&expected_b) ||
        b_second != flat_rng_u64(&expected_b)) {
        return 2;
    }

    /* Reseeding one VM must not change the other VM's next value. */
    flat_rng_seed_u64(&a, 123u);
    if (flat_rng_u64(&b) != flat_rng_u64(&expected_b)) {
        return 3;
    }

    puts("rng-context-isolation-ok");
    return 0;
}
