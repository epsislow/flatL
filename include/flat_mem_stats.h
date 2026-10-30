#ifndef FLAT_MEM_STATS_H
#define FLAT_MEM_STATS_H

#include "flat_value.h"

/* Incremental live counters per inline tag (variables + elements nested in LIST/OBJECT).
 * Updated through flat_value_live_bind / unbind / replace whenever a FlatValue is stored in or
 * removed from a variable slot or a collection element. No scanning. */
struct FlatMemLiveStats {
    int bool_used;
    int num_used;
    int str_used; /* STRING inline only; STRING REF is counted by the string pool */
    int null_used;
};

void flat_mem_stats_init(FlatMemLiveStats *st);
void flat_value_live_bind(FlatMemLiveStats *st, const FlatValue *v);
void flat_value_live_unbind(FlatMemLiveStats *st, const FlatValue *v);
void flat_value_live_replace(FlatMemLiveStats *st, const FlatValue *old_v, const FlatValue *new_v);

#endif
