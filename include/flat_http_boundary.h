#ifndef FLAT_HTTP_BOUNDARY_H
#define FLAT_HTTP_BOUNDARY_H

#include <stdint.h>

/* Process-wide monotonic sequence used to distinguish concurrent multipart requests. */
uint64_t flat_http_boundary_next_sequence(void);

#endif
