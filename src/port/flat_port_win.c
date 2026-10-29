#include "flat_port.h"

#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#ifndef F_OK
#define F_OK 0
#endif

static int dir_writable(const char *dir) {
    return dir && dir[0] != '\0' && _access(dir, 0) == 0 && _access(dir, 2) == 0;
}

int flat_port_temp_parent(char *parent, size_t parent_sz, const char *repo_root) {
    const char *env;
    char derived[FLAT_PORT_PATH_MAX];
    char win_tmp[FLAT_PORT_PATH_MAX];

    env = getenv("TMPDIR");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    env = getenv("TEMP");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    env = getenv("TMP");
    if (dir_writable(env)) {
        snprintf(parent, parent_sz, "%s", env);
        return 0;
    }
    if (GetTempPathA((DWORD)sizeof win_tmp, win_tmp) > 0 && dir_writable(win_tmp)) {
        snprintf(parent, parent_sz, "%s", win_tmp);
        return 0;
    }
    if (repo_root && repo_root[0] != '\0') {
        snprintf(derived, sizeof derived, "%s\\.flatl_run_tmp", repo_root);
        if (_mkdir(derived) != 0 && errno != EEXIST) {
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
    return path && _access(path, F_OK) == 0;
}

int flat_port_dir_writable(const char *dir) {
    return dir_writable(dir);
}

int flat_port_getcwd(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return -1;
    }
    return _getcwd(buf, (int)buf_size) ? 0 : -1;
}

int flat_port_path_is_root(const char *path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
    if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0) {
        return 1;
    }
    if (strlen(path) == 3 && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    return 0;
}

int flat_port_mkdir(const char *path, int mode) {
    (void)mode;
    if (!path) {
        return -1;
    }
    if (_mkdir(path) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int flat_port_make_temp_dir(char *out, size_t out_size, const char *repo_root) {
    char parent[FLAT_PORT_PATH_MAX];
    unsigned long pid;
    unsigned tick;

    if (!out || out_size == 0) {
        return -1;
    }
    if (flat_port_temp_parent(parent, sizeof parent, repo_root) != 0) {
        return -1;
    }
    pid = (unsigned long)GetCurrentProcessId();
    tick = (unsigned)GetTickCount();
    if (snprintf(out, out_size, "%s\\flatl_%lu_%u", parent, pid, tick) >= (int)out_size) {
        return -1;
    }
    if (_mkdir(out) != 0 && errno != EEXIST) {
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
    snprintf(shell_cmd, sizeof shell_cmd, "cmd /c \"%s\" 2>&1", cmd);
    fp = _popen(shell_cmd, "rt");
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
        int st = _pclose(fp);
        if (st == -1) {
            return -1;
        }
        *exit_code = st;
    }
    return 0;
}

int flat_port_scan_dir(const char *dir_path, FlatPortDirFn fn, void *ctx) {
    char pattern[FLAT_PORT_PATH_MAX];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    if (!dir_path || !fn) {
        return -1;
    }
    if (snprintf(pattern, sizeof pattern, "%s\\*", dir_path) >= (int)sizeof pattern) {
        return -1;
    }
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return -1;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        if (fn(fd.cFileName, ctx) != 0) {
            FindClose(h);
            return 0;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
