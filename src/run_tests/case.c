#define _POSIX_C_SOURCE 200809L

#include "casefile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static void strip_quotes(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        s[n - 1] = '\0';
        memmove(s, s + 1, n - 1);
    }
}

static int parse_kind(const char *v, CaseKind *out) {
    if (strcmp(v, "files") == 0) {
        *out = CASE_KIND_FILES;
        return 0;
    }
    if (strcmp(v, "exec") == 0) {
        *out = CASE_KIND_EXEC;
        return 0;
    }
    if (strcmp(v, "pipeline") == 0) {
        *out = CASE_KIND_PIPELINE;
        return 0;
    }
    return -1;
}

static void apply_kv(FlatCase *c, const char *key, char *val) {
    strip_quotes(val);
    if (strcmp(key, "suite") == 0) {
        snprintf(c->suite, sizeof c->suite, "%s", val);
    } else if (strcmp(key, "id") == 0) {
        c->id = atoi(val);
    } else if (strcmp(key, "desc") == 0) {
        snprintf(c->desc, sizeof c->desc, "%s", val);
    } else if (strcmp(key, "kind") == 0) {
        parse_kind(val, &c->kind);
    } else if (strcmp(key, "path_exists") == 0 || strcmp(key, "check") == 0) {
        if (c->path_check_count < FLAT_CASE_MAX_CHECKS) {
            snprintf(c->path_checks[c->path_check_count], FLAT_CASE_MAX_PATH, "%s", val);
            c->path_check_count++;
        }
    } else if (strcmp(key, "cmd") == 0) {
        snprintf(c->cmd, sizeof c->cmd, "%s", val);
    } else if (strcmp(key, "expect_exit") == 0) {
        c->expect_exit = atoi(val);
    } else if (strcmp(key, "stdout_contains") == 0) {
        snprintf(c->stdout_contains, sizeof c->stdout_contains, "%s", val);
    } else if (strcmp(key, "stderr_contains") == 0) {
        snprintf(c->stderr_contains, sizeof c->stderr_contains, "%s", val);
    } else if (strcmp(key, "source") == 0) {
        snprintf(c->pipeline_source, sizeof c->pipeline_source, "%s", val);
    } else if (strcmp(key, "compile") == 0) {
        snprintf(c->pipeline_compile, sizeof c->pipeline_compile, "%s", val);
    } else if (strcmp(key, "run") == 0) {
        snprintf(c->pipeline_run, sizeof c->pipeline_run, "%s", val);
    }
}

int flat_case_load(const char *case_toml_path, FlatCase *out) {
    FILE *f;
    char line[1024];
    char section[64] = "";

    if (!case_toml_path || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->expect_exit = 0;

    {
        const char *slash = strrchr(case_toml_path, '/');
        if (slash) {
            size_t n = (size_t)(slash - case_toml_path);
            if (n >= sizeof out->case_dir) {
                n = sizeof out->case_dir - 1;
            }
            memcpy(out->case_dir, case_toml_path, n);
            out->case_dir[n] = '\0';
        }
    }

    f = fopen(case_toml_path, "r");
    if (!f) {
        return -1;
    }

    while (fgets(line, sizeof line, f)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') {
            continue;
        }
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof section, "%s", p + 1);
            }
            continue;
        }
        {
            char *eq = strchr(p, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            char *key = trim(p);
            char *val = trim(eq + 1);
            (void)section;
            apply_kv(out, key, val);
        }
    }
    fclose(f);

    if (out->suite[0] == '\0' || out->id <= 0) {
        return -1;
    }
    return 0;
}

static int path_exists_at(const char *repo, const char *rel) {
    char full[FLAT_CASE_MAX_PATH];
    snprintf(full, sizeof full, "%s/%s", repo, rel);
    return access(full, F_OK) == 0;
}

static void expand_root(char *dst, int dst_size, const char *repo, const char *cmd) {
    const char *src = cmd;
    char *d = dst;
    int left = dst_size - 1;

    while (*src && left > 0) {
        if (strncmp(src, "{root}", 6) == 0) {
            int n = snprintf(d, (size_t)left + 1, "%s", repo);
            if (n > left) {
                break;
            }
            d += n;
            left -= n;
            src += 6;
            continue;
        }
        *d++ = *src++;
        left--;
    }
    *d = '\0';
}

static int run_shell_capture(const char *cmd, int *exit_code, char *out_buf, int out_size) {
    char shell_cmd[FLAT_CASE_MAX_CMD + 64];
    FILE *fp;

    snprintf(shell_cmd, sizeof shell_cmd, "%s 2>&1", cmd);
    fp = popen(shell_cmd, "r");
    if (!fp) {
        return -1;
    }
    if (out_buf && out_size > 0) {
        int n = 0;
        int c;
        out_buf[0] = '\0';
        while (n < out_size - 1 && (c = fgetc(fp)) != EOF) {
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

static int check_output(const char *hay, const char *needle, char *err, int err_size) {
    if (needle[0] == '\0') {
        return 0;
    }
    if (!hay || !strstr(hay, needle)) {
        snprintf(err, (size_t)err_size, "output missing substring: %s", needle);
        return -1;
    }
    return 0;
}

int flat_case_run(const FlatCase *c, const char *repo_root, char *err, int err_size) {
    int i;

    if (!c || !repo_root) {
        snprintf(err, (size_t)err_size, "invalid arguments");
        return -1;
    }

    if (c->kind == CASE_KIND_FILES) {
        for (i = 0; i < c->path_check_count; i++) {
            if (!path_exists_at(repo_root, c->path_checks[i])) {
                snprintf(err, (size_t)err_size, "missing path: %s", c->path_checks[i]);
                return -1;
            }
        }
        return 0;
    }

    if (c->kind == CASE_KIND_EXEC) {
        char cmd[FLAT_CASE_MAX_CMD];
        char out[4096];
        int code = -1;

        expand_root(cmd, sizeof cmd, repo_root, c->cmd);
        if (run_shell_capture(cmd, &code, out, sizeof out) != 0) {
            snprintf(err, (size_t)err_size, "failed to run: %s", cmd);
            return -1;
        }
        if (code != c->expect_exit) {
            snprintf(err, (size_t)err_size, "exit %d want %d (cmd: %s)", code, c->expect_exit, cmd);
            return -1;
        }
        if (check_output(out, c->stdout_contains, err, err_size) != 0) {
            return -1;
        }
        if (check_output(out, c->stderr_contains, err, err_size) != 0) {
            return -1;
        }
        return 0;
    }

    if (c->kind == CASE_KIND_PIPELINE) {
        snprintf(err, (size_t)err_size, "pipeline kind not implemented yet");
        return -1;
    }

    snprintf(err, (size_t)err_size, "unknown or unset case kind");
    return -1;
}
