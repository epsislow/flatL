#ifndef FLAT_DISCOVER_H
#define FLAT_DISCOVER_H

#include "casefile.h"

int flat_discover_cases(const char *repo_root, FlatCase **out_cases, int *out_count);
void flat_discover_free(FlatCase *cases);

#endif
