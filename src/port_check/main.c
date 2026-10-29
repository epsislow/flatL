#include "flat_str.h"

#include <stdio.h>
#include <string.h>

static int expect_str(const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "flat_str: got '%s' want '%s'\n", got, want);
        return -1;
    }
    return 0;
}

int main(void) {
    char buf1[] = "a,b,c";
    char *p = buf1;
    if (expect_str(flat_str_next(&p, ','), "a") != 0) {
        return 1;
    }
    if (expect_str(flat_str_next(&p, ','), "b") != 0) {
        return 1;
    }
    if (expect_str(flat_str_next(&p, ','), "c") != 0) {
        return 1;
    }
    if (flat_str_next(&p, ',') != NULL) {
        return 1;
    }

    {
        char buf2[] = "  x , rest here  ";
        char *q = buf2;
        char *t = flat_str_next(&q, ',');
        if (expect_str(flat_str_trim(t), "x") != 0) {
            return 1;
        }
        if (expect_str(flat_str_trim(q), "rest here") != 0) {
            return 1;
        }
    }

    {
        char buf3[] = "only";
        char *r = buf3;
        if (expect_str(flat_str_next(&r, ','), "only") != 0) {
            return 1;
        }
        if (*r != '\0') {
            return 1;
        }
    }

    puts("OK");
    return 0;
}
