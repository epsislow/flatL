#include "repo.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int has_repo_markers(const char *dir) {
    char path[512];

    snprintf(path, sizeof path, "%s/Makefile", dir);
    if (access(path, F_OK) != 0) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/include/flat.h", dir);
    return access(path, F_OK) == 0;
}

int flat_find_repo_root(char *out, int out_size) {
    char cwd[512];

    if (!out || out_size < 2) {
        return -1;
    }
    if (!getcwd(cwd, sizeof cwd)) {
        return -1;
    }

    for (;;) {
        if (has_repo_markers(cwd)) {
            snprintf(out, (size_t)out_size, "%s", cwd);
            return 0;
        }
        if (strcmp(cwd, "/") == 0) {
            break;
        }
        {
            char *slash = strrchr(cwd, '/');
            if (!slash) {
                break;
            }
            if (slash == cwd) {
                cwd[1] = '\0';
            } else {
                *slash = '\0';
            }
        }
    }
    return -1;
}
