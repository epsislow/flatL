#define _POSIX_C_SOURCE 200809L

#include "flat_port.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TEMP_SUFFIX "flatl_caseXXXXXX"

static int dir_writable(const char *dir) {
    return dir && dir[0] != '\0' && access(dir, W_OK) == 0;
}

int flat_port_temp_parent(char *parent, size_t parent_sz, const char *repo_root) {
    const char *env;
    char derived[FLAT_PORT_PATH_MAX];

    env = getenv("TMPDIR");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    env = getenv("TMP");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    env = getenv("TEMP");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    env = getenv("PREFIX");
    if (env && env[0] != '\0') {
        snprintf(derived, sizeof derived, "%s/tmp", env);
        if (dir_writable(derived)) {
            snprintf(parent, parent_sz, "%s", derived);
            return 0;
        }
    }
    env = getenv("HOME");
    if (env && env[0] != '\0') {
        snprintf(derived, sizeof derived, "%s/tmp", env);
        if (dir_writable(derived)) {
            snprintf(parent, parent_sz, "%s", derived);
            return 0;
        }
    }
    if (dir_writable("/tmp")) {
        snprintf(parent, parent_sz, "/tmp");
        return 0;
    }
    if (repo_root && repo_root[0] != '\0') {
        snprintf(derived, sizeof derived, "%s/.flatl_run_tmp", repo_root);
        if (mkdir(derived, 0700) != 0 && errno != EEXIST) {
            return -1;
        }
        if (dir_writable(derived)) {
            snprintf(parent, parent_sz, "%s", derived);
            return 0;
        }
    }
    return -1;
}

int flat_port_path_exists(const char *path) {
    return path && access(path, F_OK) == 0;
}

int flat_port_dir_writable(const char *dir) {
    return dir_writable(dir);
}

int flat_port_getcwd(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return -1;
    }
    return getcwd(buf, buf_size) ? 0 : -1;
}

int flat_port_path_is_root(const char *path) {
    return path && (strcmp(path, "/") == 0);
}

int flat_port_mkdir(const char *path, int mode) {
    if (!path) {
        return -1;
    }
    if (mkdir(path, (mode_t)mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int flat_port_make_temp_dir(char *out, size_t out_size, const char *repo_root) {
    char parent[FLAT_PORT_PATH_MAX];
    size_t need;

    if (!out || out_size == 0) {
        return -1;
    }
    if (flat_port_temp_parent(parent, sizeof parent, repo_root) != 0) {
        return -1;
    }
    need = strlen(parent) + 1 + strlen(TEMP_SUFFIX) + 1;
    if (need > out_size) {
        return -1;
    }
    if (snprintf(out, out_size, "%s/" TEMP_SUFFIX, parent) >= (int)out_size) {
        return -1;
    }
    if (!mkdtemp(out)) {
        return -1;
    }
    return 0;
}

int flat_port_make_temp_fb_path(char *out, size_t out_size, const char *repo_root) {
    char parent[FLAT_PORT_PATH_MAX];

    if (!out || out_size == 0) {
        return -1;
    }
    if (flat_port_temp_parent(parent, sizeof parent, repo_root) != 0) {
        return -1;
    }
    if (snprintf(out, out_size, "%s/flatl_%ld_%ld.fb", parent, (long)getpid(),
                 (long)time(NULL)) >= (int)out_size) {
        return -1;
    }
    return 0;
}

int flat_port_shell_capture(const char *cmd, int *exit_code, char *out_buf, int out_buf_size) {
    char shell_cmd[4096];
    FILE *fp;

    if (!cmd || !exit_code) {
        return -1;
    }
    snprintf(shell_cmd, sizeof shell_cmd, "%s 2>&1", cmd);
    fp = popen(shell_cmd, "r");
    if (!fp) {
        return -1;
    }
    if (out_buf && out_buf_size > 0) {
        int n = 0;
        int c;
        out_buf[0] = '\0';
        while (n < out_buf_size - 1 && (c = fgetc(fp)) != EOF) {
            out_buf[n++] = (char)c;
        }
        out_buf[n] = '\0';
    } else {
        while (fgetc(fp) != EOF) {
        }
    }
    {
        int st = pclose(fp);
        if (st == -1) {
            return -1;
        }
        if (WIFEXITED(st)) {
            *exit_code = WEXITSTATUS(st);
        } else {
            *exit_code = 1;
        }
    }
    return 0;
}

void flat_port_path_to_native(char *path) {
    (void)path;
}

void flat_port_prepare_shell_cmd(char *cmd, size_t cmd_cap) {
    (void)cmd;
    (void)cmd_cap;
}

int flat_port_scan_dir(const char *dir_path, FlatPortDirFn fn, void *ctx) {
    DIR *d;
    struct dirent *de;

    if (!dir_path || !fn) {
        return -1;
    }
    d = opendir(dir_path);
    if (!d) {
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' || (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
            continue;
        }
        if (fn(de->d_name, ctx) != 0) {
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return 0;
}
