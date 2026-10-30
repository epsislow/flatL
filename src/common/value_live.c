#include "flat_mem_stats.h"

#include <string.h>

void flat_mem_stats_init(FlatMemLiveStats *st) {
    if (st) {
        memset(st, 0, sizeof *st);
    }
}

static void live_adjust(FlatMemLiveStats *st, const FlatValue *v, int delta) {
    int *c = NULL;
    if (!st || !v) {
        return;
    }
    switch (v->tag) {
    case VAL_BOOL:
        c = &st->bool_used;
        break;
    case VAL_NUMBER:
        c = &st->num_used;
        break;
    case VAL_STR_INLINE:
        c = &st->str_used;
        break;
    case VAL_NULL:
        c = &st->null_used;
        break;
    default:
        return;
    }
    *c += delta;
    if (*c < 0) {
        *c = 0;
    }
}

void flat_value_live_bind(FlatMemLiveStats *st, const FlatValue *v) {
    live_adjust(st, v, 1);
}

void flat_value_live_unbind(FlatMemLiveStats *st, const FlatValue *v) {
    live_adjust(st, v, -1);
}

void flat_value_live_replace(FlatMemLiveStats *st, const FlatValue *old_v,
                             const FlatValue *new_v) {
    live_adjust(st, old_v, -1);
    live_adjust(st, new_v, 1);
}
