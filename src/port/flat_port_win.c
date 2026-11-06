#include "flat_port.h"

#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#ifndef CREATE_NO_WINDOW
#define CREATE_NO_WINDOW 0x08000000
#endif

#ifndef F_OK
#define F_OK 0
#endif

void flat_port_stdio_setup(void) {
    (void)_setmode(_fileno(stdin), _O_TEXT);
    (void)_setmode(_fileno(stdout), _O_TEXT);
    (void)_setmode(_fileno(stderr), _O_TEXT);
}

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

int flat_port_make_temp_fb_path(char *out, size_t out_size, const char *repo_root) {
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
    if (snprintf(out, out_size, "%s\\flatl_%lu_%u.fb", parent, pid, tick) >= (int)out_size) {
        return -1;
    }
    return 0;
}

static int sh_exe_path(char *out, size_t out_sz) {
    char *unused;
    /* MSYS2 / Git Bash: sh.exe on PATH. */
    if (SearchPathA(NULL, "sh.exe", NULL, (DWORD)out_sz, out, &unused) > 0) {
        return 0;
    }
    if (SearchPathA(NULL, "sh", ".exe", (DWORD)out_sz, out, &unused) > 0) {
        return 0;
    }
    return -1;
}

/* sh -c '…' with standard single-quote escaping ('\"'\"''). */
static int escape_sh_single_quoted(const char *cmd, char *out, size_t out_sz) {
    const char *s;
    char *d = out;
    size_t left = out_sz > 0 ? out_sz - 1 : 0;

    *d++ = '\'';
    left--;
    for (s = cmd; *s && left > 0; s++) {
        if (*s == '\'') {
            const char ins[] = "'\"'\"'";
            size_t k;
            for (k = 0; k < sizeof ins - 1 && left > 0; k++) {
                *d++ = ins[k];
                left--;
            }
            if (k < sizeof ins - 1) {
                return -1;
            }
        } else {
            *d++ = *s;
            left--;
        }
    }
    if (*s) {
        return -1;
    }
    if (left < 1) {
        return -1;
    }
    *d++ = '\'';
    *d = '\0';
    return 0;
}

static int run_create_process_capture(char *cmdline_mut, char *out_buf, int out_buf_size,
                                      int *exit_code) {
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD nread;
    char buf[4096];
    int ok = -1;

    memset(&pi, 0, sizeof pi);
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        return -1;
    }
    if (!SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
        goto fail;
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;

    if (!CreateProcessA(NULL, cmdline_mut, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        goto fail;
    }

    CloseHandle(wr);
    wr = NULL;

    if (out_buf && out_buf_size > 0) {
        int total = 0;
        out_buf[0] = '\0';
        for (;;) {
            if (!ReadFile(rd, buf, sizeof buf, &nread, NULL)) {
                DWORD e = GetLastError();
                if (e == ERROR_BROKEN_PIPE) {
                    break;
                }
                goto fail;
            }
            if (nread == 0) {
                break;
            }
            {
                int add = (int)nread;
                if (total + add >= out_buf_size) {
                    add = out_buf_size - 1 - total;
                }
                if (add > 0) {
                    memcpy(out_buf + total, buf, (size_t)add);
                    total += add;
                    out_buf[total] = '\0';
                }
            }
        }
    } else {
        for (;;) {
            if (!ReadFile(rd, buf, sizeof buf, &nread, NULL)) {
                DWORD e = GetLastError();
                if (e == ERROR_BROKEN_PIPE) {
                    break;
                }
                goto fail;
            }
            if (nread == 0) {
                break;
            }
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    {
        DWORD ec = 1;
        if (!GetExitCodeProcess(pi.hProcess, &ec)) {
            ec = 1;
        }
        *exit_code = (int)ec;
    }
    ok = 0;

fail:
    if (wr) {
        CloseHandle(wr);
    }
    if (rd) {
        CloseHandle(rd);
    }
    if (pi.hThread) {
        CloseHandle(pi.hThread);
    }
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
    }
    return ok;
}

int flat_port_shell_capture(const char *cmd, int *exit_code, char *out_buf, int out_buf_size) {
    char shpath[FLAT_PORT_PATH_MAX];
    char quoted[16384];
    char *cmdline = NULL;
    size_t cap;
    int rc;

    if (!cmd || !exit_code) {
        return -1;
    }

    if (sh_exe_path(shpath, sizeof shpath) != 0) {
        /* Pipeline run lines require POSIX shell (VAR=value, </dev/null, pipes). */
        return -1;
    }

    if (escape_sh_single_quoted(cmd, quoted, sizeof quoted) != 0) {
        return -1;
    }

    cap = strlen(shpath) + strlen(quoted) + 32;
    cmdline = malloc(cap);
    if (!cmdline) {
        return -1;
    }
    if (snprintf(cmdline, cap, "\"%s\" -c %s", shpath, quoted) >= (int)cap) {
        free(cmdline);
        return -1;
    }

    rc = run_create_process_capture(cmdline, out_buf, out_buf_size, exit_code);
    free(cmdline);
    return rc;
}

void flat_port_path_to_native(char *path) {
    char *p;
    if (!path) {
        return;
    }
    for (p = path; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
}

static void replace_token(char *cmd, size_t cap, const char *from, const char *to) {
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    char *pos;
    if (fl == 0 || tl == 0 || !cmd) {
        return;
    }
    while ((pos = strstr(cmd, from)) != NULL) {
        size_t tail = strlen(pos + fl);
        if ((size_t)(pos - cmd) + tl + tail + 1 > cap) {
            return;
        }
        memmove(pos + tl, pos + fl, tail + 1);
        memcpy(pos, to, tl);
    }
}

void flat_port_prepare_shell_cmd(char *cmd, size_t cmd_cap) {
    char *p;
    if (!cmd || cmd_cap == 0) {
        return;
    }
    /*
     * flat_port_shell_capture runs commands via MSYS2 sh -c '…'. Backslashes in
     * Windows paths are treated as shell escapes (e.g. D:\w\...\bin\flc.exe
     * becomes D:w...binflc.exe and exits 127). Forward slashes work for Win32
     * APIs and our tools, so normalize the command line for the POSIX shell.
     */
    for (p = cmd; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    replace_token(cmd, cmd_cap, "\\bin\\flc ", "\\bin\\flc.exe ");
    replace_token(cmd, cmd_cap, "\\bin\\flvm ", "\\bin\\flvm.exe ");
    replace_token(cmd, cmd_cap, "/bin/flc ", "/bin/flc.exe ");
    replace_token(cmd, cmd_cap, "/bin/flvm ", "/bin/flvm.exe ");
    replace_token(cmd, cmd_cap, "\\bin\\flatl ", "\\bin\\flatl.exe ");
    replace_token(cmd, cmd_cap, "/bin/flatl ", "/bin/flatl.exe ");
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
