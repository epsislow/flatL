#include "flat_http_boundary.h"

#include <stdatomic.h>

/* A request-local boundary still includes its request-slot address. The sequence
 * prevents reuse collisions, and atomic fetch-add makes concurrent VM calls safe. */
static atomic_uint_fast64_t g_http_boundary_sequence = ATOMIC_VAR_INIT(0);

uint64_t flat_http_boundary_next_sequence(void) {
    return atomic_fetch_add_explicit(&g_http_boundary_sequence, 1, memory_order_relaxed) + 1;
}
