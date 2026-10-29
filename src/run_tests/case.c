#include "casefile.h"
#include "flat_port.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    return flat_port_path_exists(full);
}

static void expand_placeholders(char *dst, int dst_size, const char *repo, const char *case_dir,
                                const char *tmp, const char *src_path, const char *cmd) {
    const char *src = cmd;
    char *d = dst;
    int left = dst_size - 1;

    (void)src_path;
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
        if (strncmp(src, "{case}", 6) == 0) {
            int n = snprintf(d, (size_t)left + 1, "%s", case_dir ? case_dir : "");
            if (n > left) {
                break;
            }
            d += n;
            left -= n;
            src += 6;
            continue;
        }
        if (strncmp(src, "{tmp}", 5) == 0) {
            char tmp_fallback[FLAT_CASE_MAX_PATH];
            const char *t = tmp;
            if (!t) {
                if (flat_port_temp_parent(tmp_fallback, sizeof tmp_fallback, repo) == 0) {
                    t = tmp_fallback;
                } else {
                    t = "/tmp";
                }
            }
            int n = snprintf(d, (size_t)left + 1, "%s", t);
            if (n > left) {
                break;
            }
            d += n;
            left -= n;
            src += 5;
            continue;
        }
        if (strncmp(src, "{src}", 5) == 0) {
            int n = snprintf(d, (size_t)left + 1, "%s", src_path ? src_path : "");
            if (n > left) {
                break;
            }
            d += n;
            left -= n;
            src += 5;
            continue;
        }
        *d++ = *src++;
        left--;
    }
    *d = '\0';
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

        expand_placeholders(cmd, sizeof cmd, repo_root, c->case_dir, NULL, NULL, c->cmd);
        if (flat_port_shell_capture(cmd, &code, out, sizeof out) != 0) {
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
        char tmpdir[FLAT_CASE_MAX_PATH];
        char src_path[FLAT_CASE_MAX_PATH + 64];
        char compile_cmd[FLAT_CASE_MAX_CMD];
        char run_cmd[FLAT_CASE_MAX_CMD];
        char out[4096];
        int code;

        if (c->pipeline_compile[0] == '\0' || c->pipeline_run[0] == '\0') {
            snprintf(err, (size_t)err_size, "pipeline missing compile/run");
            return -1;
        }
        if (flat_port_make_temp_dir(tmpdir, sizeof tmpdir, repo_root) != 0) {
            snprintf(err, (size_t)err_size,
                     "no writable temp dir (try export TMPDIR=$HOME/tmp or PREFIX/tmp on Termux)");
            return -1;
        }
        flat_port_path_to_native(tmpdir);
        if (snprintf(src_path, sizeof src_path, "%s/%s", c->case_dir,
                     c->pipeline_source[0] ? c->pipeline_source : "program.fl") >=
            (int)sizeof src_path) {
            snprintf(err, (size_t)err_size, "pipeline source path too long");
            return -1;
        }
        flat_port_path_to_native(src_path);
        expand_placeholders(compile_cmd, sizeof compile_cmd, repo_root, c->case_dir, tmpdir,
                            src_path, c->pipeline_compile);
        expand_placeholders(run_cmd, sizeof run_cmd, repo_root, c->case_dir, tmpdir, src_path,
                            c->pipeline_run);
        flat_port_prepare_shell_cmd(compile_cmd, sizeof compile_cmd);
        flat_port_prepare_shell_cmd(run_cmd, sizeof run_cmd);
        if (flat_port_shell_capture(compile_cmd, &code, out, sizeof out) != 0) {
            snprintf(err, (size_t)err_size, "compile failed: %s", compile_cmd);
            return -1;
        }
        if (code != 0) {
            snprintf(err, (size_t)err_size, "flc exit %d: %s", code, compile_cmd);
            return -1;
        }
        if (flat_port_shell_capture(run_cmd, &code, out, sizeof out) != 0) {
            snprintf(err, (size_t)err_size, "run failed: %s", run_cmd);
            return -1;
        }
        if (code != c->expect_exit) {
            snprintf(err, (size_t)err_size, "vm exit %d want %d", code, c->expect_exit);
            return -1;
        }
        if (check_output(out, c->stdout_contains, err, err_size) != 0) {
            return -1;
        }
        return 0;
    }

    snprintf(err, (size_t)err_size, "unknown or unset case kind");
    return -1;
}
