#define _POSIX_C_SOURCE 200809L
#include "flat_date.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FlatDateRunState a = {0};
    FlatDateRunState b = {0};

    if (setenv("FLAT_DATE_RUN_OFFSET", "+03:00", 1) != 0 ||
        setenv("FLAT_DATE_NOW_SEC", "100", 1) != 0) {
        return 1;
    }
    flat_date_run_begin(&a);

    if (setenv("FLAT_DATE_RUN_OFFSET", "-05:30", 1) != 0 ||
        setenv("FLAT_DATE_NOW_SEC", "200", 1) != 0) {
        return 2;
    }
    flat_date_run_begin(&b);

    if (a.run_offset_min != 180 || !a.has_run_offset || a.now_override_sec != 100) {
        return 3;
    }
    if (b.run_offset_min != -330 || !b.has_run_offset || b.now_override_sec != 200) {
        return 4;
    }

    /* Updating one VM's date configuration must not mutate the other. */
    a.run_offset_min = 0;
    a.has_run_offset = 0;
    a.now_override_sec = 300;
    if (b.run_offset_min != -330 || !b.has_run_offset || b.now_override_sec != 200) {
        return 5;
    }

    puts("date-run-state-isolation-ok");
    return 0;
}
