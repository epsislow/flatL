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

static int path_is_msys2_usr_sh(const char *p) {
    /* Prefer MSYS2 usr/bin/sh over Git Bash when both are on PATH. */
    return p && strstr(p, "msys64\\usr\\bin\\sh.exe") != NULL;
}

static int sh_exe_path(char *out, size_t out_sz) {
    char *unused;
    char found[FLAT_PORT_PATH_MAX];
    static const char *known[] = {
        "D:\\msys64\\usr\\bin\\sh.exe",
        "C:\\msys64\\usr\\bin\\sh.exe",
        "D:\\msys32\\usr\\bin\\sh.exe",
        "C:\\msys32\\usr\\bin\\sh.exe",
    };
    size_t k;

    for (k = 0; k < sizeof known / sizeof known[0]; k++) {
        if (flat_port_path_exists(known[k])) {
            if (snprintf(out, out_sz, "%s", known[k]) >= (int)out_sz) {
                return -1;
            }
            return 0;
        }
    }

    if (SearchPathA(NULL, "sh.exe", NULL, (DWORD)sizeof found, found, &unused) > 0) {
        /* Prefer MSYS2 path if we also found Git Bash via PATH. */
        if (!path_is_msys2_usr_sh(found) && strstr(found, "Git") != NULL) {
            for (k = 0; k < sizeof known / sizeof known[0]; k++) {
                if (flat_port_path_exists(known[k])) {
                    if (snprintf(out, out_sz, "%s", known[k]) >= (int)out_sz) {
                        return -1;
                    }
                    return 0;
                }
            }
        }
        if (snprintf(out, out_sz, "%s", found) >= (int)out_sz) {
            return -1;
        }
        return 0;
    }
    if (SearchPathA(NULL, "sh", ".exe", (DWORD)out_sz, out, &unused) > 0) {
        return 0;
    }
    return -1;
}

/* Write body to a unique .sh under the temp parent; path_out uses Windows separators. */
static int write_temp_sh_script(const char *body, char *path_out, size_t path_out_sz) {
    char parent[FLAT_PORT_PATH_MAX];
    char win_tmp[FLAT_PORT_PATH_MAX];
    FILE *f;
    unsigned long pid;
    unsigned tick;
    static unsigned seq;
    size_t n;

    if (!body || !path_out || path_out_sz == 0) {
        return -1;
    }
    if (flat_port_temp_parent(parent, sizeof parent, NULL) != 0) {
        if (GetTempPathA((DWORD)sizeof win_tmp, win_tmp) == 0 || !dir_writable(win_tmp)) {
            return -1;
        }
        if (snprintf(parent, sizeof parent, "%s", win_tmp) >= (int)sizeof parent) {
            return -1;
        }
    }
    pid = (unsigned long)GetCurrentProcessId();
    tick = (unsigned)GetTickCount();
    seq++;
    if (snprintf(path_out, path_out_sz, "%s\\flatl_sh_%lu_%u_%u.sh", parent, pid, tick, seq) >=
        (int)path_out_sz) {
        return -1;
    }
    f = fopen(path_out, "wb");
    if (!f) {
        return -1;
    }
    n = strlen(body);
    if (n > 0 && fwrite(body, 1, n, f) != n) {
        fclose(f);
        DeleteFileA(path_out);
        return -1;
    }
    if (fputc('\n', f) == EOF) {
        fclose(f);
        DeleteFileA(path_out);
        return -1;
    }
    if (fclose(f) != 0) {
        DeleteFileA(path_out);
        return -1;
    }
    return 0;
}

static int run_create_process_capture(char *cmdline_mut, char *out_buf, int out_buf_size,
                                      int *exit_code) {
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    HANDLE stdin_inherited = NULL;
    HANDLE null_rd = NULL;
    HANDLE null_wr = NULL;
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

    stdin_inherited = GetStdHandle(STD_INPUT_HANDLE);
    if (stdin_inherited == INVALID_HANDLE_VALUE || stdin_inherited == NULL ||
        GetFileType(stdin_inherited) == FILE_TYPE_UNKNOWN) {
        if (!CreatePipe(&null_rd, &null_wr, &sa, 0)) {
            goto fail;
        }
        /* Child reads inherited read end; write end stays in parent (closed after create). */
        if (!SetHandleInformation(null_rd, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            goto fail;
        }
        if (!SetHandleInformation(null_wr, HANDLE_FLAG_INHERIT, 0)) {
            goto fail;
        }
        CloseHandle(null_wr);
        null_wr = NULL;
        stdin_inherited = null_rd;
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdin_inherited;
    si.hStdOutput = wr;
    si.hStdError = wr;

    if (!CreateProcessA(NULL, cmdline_mut, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                        &pi)) {
        goto fail;
    }

    if (null_rd) {
        CloseHandle(null_rd);
        null_rd = NULL;
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
    if (null_wr) {
        CloseHandle(null_wr);
    }
    if (null_rd) {
        CloseHandle(null_rd);
    }
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
    char prefixed[16576];
    char script_win[FLAT_PORT_PATH_MAX];
    char script_posix[FLAT_PORT_PATH_MAX];
    char *cmdline = NULL;
    char *p;
    size_t cap;
    int rc;

    if (!cmd || !exit_code) {
        return -1;
    }

    if (sh_exe_path(shpath, sizeof shpath) != 0) {
        /* Pipeline run lines require POSIX shell (VAR=value, </dev/null, pipes). */
        return -1;
    }

    /* Ensure printf(1) / shell and child parse numbers with '.' regardless of host locale. */
    if (snprintf(prefixed, sizeof prefixed, "export LC_ALL=C; %s", cmd) >= (int)sizeof prefixed) {
        return -1;
    }

    /*
     * Run via a temp .sh file instead of sh -c '…'. Nested single quotes from
     * printf '…' / printf '' become '"'"' escapes that inject " into the
     * CreateProcess command line; MSYS2 argv parsing then mangled -c and
     * yielded exit 127 (command not found). A script path needs only Windows
     * double-quote escaping around the two paths.
     */
    if (write_temp_sh_script(prefixed, script_win, sizeof script_win) != 0) {
        return -1;
    }
    if (snprintf(script_posix, sizeof script_posix, "%s", script_win) >= (int)sizeof script_posix) {
        DeleteFileA(script_win);
        return -1;
    }
    for (p = script_posix; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    cap = strlen(shpath) + strlen(script_posix) + 16;
    cmdline = malloc(cap);
    if (!cmdline) {
        DeleteFileA(script_win);
        return -1;
    }
    if (snprintf(cmdline, cap, "\"%s\" \"%s\"", shpath, script_posix) >= (int)cap) {
        free(cmdline);
        DeleteFileA(script_win);
        return -1;
    }

    rc = run_create_process_capture(cmdline, out_buf, out_buf_size, exit_code);
    free(cmdline);
    DeleteFileA(script_win);
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

/* Turn Windows path backslashes into '/' for sh, but not inside '…' (e.g. printf '3.5\n'). */
static void win_paths_to_posix_slashes_outside_quotes(char *cmd) {
    int in_sq = 0;
    char *p;
    for (p = cmd; *p; p++) {
        if (*p == '\'') {
            in_sq = !in_sq;
            continue;
        }
        if (!in_sq && *p == '\\') {
            *p = '/';
        }
    }
}

/*
 * printf '' | REST  →  REST </dev/null
 * Empty stdin via pipe+printf is the common case.toml pattern; env/dom already
 * use </dev/null. Redirect avoids the pipe and matches what works on MSYS2.
 */
static void rewrite_empty_printf_pipe(char *cmd, size_t cmd_cap) {
    static const char needle[] = "printf '' | ";
    static const char suffix[] = " </dev/null";
    char *pos;
    char *rest;
    size_t before;
    size_t rest_len;
    size_t need;

    if (!cmd || cmd_cap < sizeof suffix) {
        return;
    }
    pos = strstr(cmd, needle);
    if (!pos) {
        return;
    }
    if (pos != cmd) {
        char prev = pos[-1];
        if (prev != ' ' && prev != '\t' && prev != ';') {
            return;
        }
    }
    rest = pos + (sizeof needle - 1);
    before = (size_t)(pos - cmd);
    rest_len = strlen(rest);
    need = before + rest_len + (sizeof suffix - 1) + 1;
    if (need > cmd_cap) {
        return;
    }
    memmove(pos, rest, rest_len + 1);
    memcpy(cmd + before + rest_len, suffix, sizeof suffix);
}

void flat_port_prepare_shell_cmd(char *cmd, size_t cmd_cap) {
    if (!cmd || cmd_cap == 0) {
        return;
    }
    /*
     * flat_port_shell_capture runs commands via MSYS2 sh (temp .sh). Backslashes
     * in Windows paths are treated as shell escapes (e.g. D:\w\...\bin\flc.exe
     * becomes D:w...binflc.exe and exits 127). Forward slashes work for Win32
     * APIs and our tools, so normalize path segments for the POSIX shell.
     * Do not rewrite '\' inside single-quoted literals (printf '...\n').
     */
    win_paths_to_posix_slashes_outside_quotes(cmd);
    rewrite_empty_printf_pipe(cmd, cmd_cap);
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
