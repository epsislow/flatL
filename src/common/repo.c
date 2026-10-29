#include "flat_port.h"
#include "repo.h"

#include <stdio.h>
#include <string.h>

static int has_repo_markers(const char *dir) {
    char path[512];

    snprintf(path, sizeof path, "%s/Makefile", dir);
    if (!flat_port_path_exists(path)) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/include/flat.h", dir);
    return flat_port_path_exists(path);
}

int flat_find_repo_root(char *out, int out_size) {
    char cwd[512];

    if (!out || out_size < 2) {
        return -1;
    }
    if (flat_port_getcwd(cwd, sizeof cwd) != 0) {
        return -1;
    }

    for (;;) {
        if (has_repo_markers(cwd)) {
            snprintf(out, (size_t)out_size, "%s", cwd);
            return 0;
        }
        if (flat_port_path_is_root(cwd)) {
            break;
        }
        {
            char *slash = strrchr(cwd, '/');
            char *bslash = strrchr(cwd, '\\');
            char *sep = slash;
            if (bslash && (!slash || bslash > slash)) {
                sep = bslash;
            }
            if (!sep) {
                break;
            }
            if (sep == cwd && sep[1] == '\0') {
                break;
            }
            if (sep == cwd && (sep[0] == '/' || sep[0] == '\\')) {
                cwd[1] = '\0';
            } else {
                *sep = '\0';
            }
        }
    }
    return -1;
}
