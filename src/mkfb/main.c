#include "fb_format.h"

#include <stdio.h>
#include <string.h>

static int usage(const char *prog) {
    fprintf(stderr, "Usage: %s --min-ok -o out.fb\n", prog);
    return 2;
}

int main(int argc, char **argv) {
    const char *out = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--min-ok") == 0) {
            continue;
        }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
            continue;
        }
        return usage(argv[0]);
    }
    if (!out) {
        return usage(argv[0]);
    }
    return flat_fb_write_min_ok(out) == 0 ? 0 : 1;
}
