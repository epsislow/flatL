#include "casefile.h"
#include "flat_port.h"
#include "flat_time.h"

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

static void apply_kv_flatl(FlatCase *c, const char *key, char *val) {
    strip_quotes(val);
    if (strcmp(key, "crun") == 0) {
        snprintf(c->flatl_crun, sizeof c->flatl_crun, "%s", val);
    } else if (strcmp(key, "compile") == 0) {
        snprintf(c->flatl_compile, sizeof c->flatl_compile, "%s", val);
    } else if (strcmp(key, "run") == 0) {
        snprintf(c->flatl_run, sizeof c->flatl_run, "%s", val);
    } else if (strcmp(key, "expect_exit") == 0) {
        c->flatl_expect_exit = atoi(val);
        c->flatl_expect_exit_set = 1;
    } else if (strncmp(key, "stdout_contains", 15) == 0) {
        if (strcmp(key, "stdout_contains") == 0) {
            snprintf(c->flatl_stdout_contains, sizeof c->flatl_stdout_contains, "%s", val);
            c->flatl_stdout_contains_set = 1;
        } else if (key[15] == '_' && key[16] != '\0') {
            int n = atoi(key + 16);
            int ix;
            if (n >= 2 && n <= FLAT_CASE_MAX_STDOUT_EXTRA + 1) {
                ix = n - 2;
                snprintf(c->flatl_stdout_contains_extra[ix], sizeof c->flatl_stdout_contains_extra[ix],
                         "%s", val);
                if (c->flatl_stdout_contains_extra_count < ix + 1) {
                    c->flatl_stdout_contains_extra_count = ix + 1;
                }
                if (!c->flatl_stdout_contains_set)
                    c->flatl_stdout_contains_set = 1;
            }
        }
    } else if (strcmp(key, "stdout_must_not_contain") == 0) {
        snprintf(c->flatl_stdout_must_not_contain, sizeof c->flatl_stdout_must_not_contain, "%s",
                 val);
        c->flatl_stdout_must_not_contain_set = 1;
    } else if (strcmp(key, "stderr_contains") == 0) {
        snprintf(c->flatl_stderr_contains, sizeof c->flatl_stderr_contains, "%s", val);
        c->flatl_stderr_contains_set = 1;
    }
}

static void apply_kv(FlatCase *c, const char *key, char *val) {
    strip_quotes(val);
    if (strcmp(key, "suite") == 0) {
        snprintf(c->suite, sizeof c->suite, "%s", val);
    } else if (strcmp(key, "id") == 0) {
        c->id = atoi(val);
    } else if (strcmp(key, "desc") == 0) {
        snprintf(c->desc, sizeof c->desc, "%s", val);
    } else if (strcmp(key, "categ") == 0) {
        snprintf(c->categ, sizeof c->categ, "%s", val);
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
    } else if (strncmp(key, "stdout_contains", 15) == 0) {
        if (strcmp(key, "stdout_contains") == 0) {
            snprintf(c->stdout_contains, sizeof c->stdout_contains, "%s", val);
        } else if (key[15] == '_' && key[16] != '\0') {
            int n = atoi(key + 16);
            int ix;
            if (n >= 2 && n <= FLAT_CASE_MAX_STDOUT_EXTRA + 1) {
                ix = n - 2;
                snprintf(c->stdout_contains_extra[ix], sizeof c->stdout_contains_extra[ix], "%s",
                         val);
                if (c->stdout_contains_extra_count < ix + 1) {
                    c->stdout_contains_extra_count = ix + 1;
                }
            }
        }
    } else if (strcmp(key, "stdout_must_not_contain") == 0) {
        snprintf(c->stdout_must_not_contain, sizeof c->stdout_must_not_contain, "%s", val);
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
            if (strcmp(section, "flatl") == 0) {
                out->flatl_section_seen = 1;
                apply_kv_flatl(out, key, val);
            } else {
                apply_kv(out, key, val);
            }
        }
    }
    fclose(f);

    if (out->categ[0] == '\0') {
        snprintf(out->categ, sizeof out->categ, "%s", FLAT_CASE_CATEG_DEFAULT);
    }
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

static int check_stdout_all(const FlatCase *c, const char *out, char *err, int err_size) {
    int i;
    if (check_output(out, c->stdout_contains, err, err_size) != 0) {
        return -1;
    }
    for (i = 0; i < c->stdout_contains_extra_count; i++) {
        if (check_output(out, c->stdout_contains_extra[i], err, err_size) != 0) {
            return -1;
        }
    }
    if (c->stdout_must_not_contain[0] != '\0' && out && strstr(out, c->stdout_must_not_contain)) {
        snprintf(err, (size_t)err_size, "output must not contain: %s", c->stdout_must_not_contain);
        return -1;
    }
    return 0;
}

static int check_stdout_flatl(const FlatCase *c, const char *out, char *err, int err_size) {
    int i;
    if (c->flatl_stdout_contains_set &&
        check_output(out, c->flatl_stdout_contains, err, err_size) != 0) {
        return -1;
    }
    for (i = 0; i < c->flatl_stdout_contains_extra_count; i++) {
        if (check_output(out, c->flatl_stdout_contains_extra[i], err, err_size) != 0) {
            return -1;
        }
    }
    if (c->flatl_stdout_must_not_contain_set && out &&
        strstr(out, c->flatl_stdout_must_not_contain)) {
        snprintf(err, (size_t)err_size, "output must not contain: %s",
                 c->flatl_stdout_must_not_contain);
        return -1;
    }
    return 0;
}

static int check_exit_want(int code, int want, const char *what, char *err, int err_size) {
    if (want < 0) {
        return 0;
    }
    if (code != want) {
        snprintf(err, (size_t)err_size, "%s exit %d want %d", what, code, want);
        return -1;
    }
    return 0;
}

static int check_stderr_flatl(const FlatCase *c, const char *out, char *err, int err_size) {
    if (!c->flatl_stderr_contains_set) {
        return 0;
    }
    return check_output(out, c->flatl_stderr_contains, err, err_size);
}

static const char FLATL_COMPILE[] = "{root}/bin/flatl compile -o {tmp}/out.fb {src}";

static void replace_token(char *cmd, size_t cap, const char *from, const char *to) {
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    char *pos;

    if (fl == 0 || !cmd) {
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

static void subst_flvm_with_flatl_run(char *cmd, size_t cap) {
    replace_token(cmd, cap, "/bin/flvm --trace ", "/bin/flatl run --trace ");
    replace_token(cmd, cap, "/bin/flvm.exe --trace ", "/bin/flatl.exe run --trace ");
    replace_token(cmd, cap, "\\bin\\flvm --trace ", "\\bin\\flatl run --trace ");
    replace_token(cmd, cap, "\\bin\\flvm.exe --trace ", "\\bin\\flatl.exe run --trace ");
    replace_token(cmd, cap, "/bin/flvm ", "/bin/flatl run ");
    replace_token(cmd, cap, "/bin/flvm.exe ", "/bin/flatl.exe run ");
    replace_token(cmd, cap, "\\bin\\flvm ", "\\bin\\flatl run ");
    replace_token(cmd, cap, "\\bin\\flvm.exe ", "\\bin\\flatl.exe run ");
}

static void inject_tool_time(char *cmd, size_t cap) {
    replace_token(cmd, cap, "/bin/flc ", "/bin/flc --time ");
    replace_token(cmd, cap, "/bin/flc.exe ", "/bin/flc.exe --time ");
    replace_token(cmd, cap, "\\bin\\flc ", "\\bin\\flc --time ");
    replace_token(cmd, cap, "\\bin\\flc.exe ", "\\bin\\flc.exe --time ");
    replace_token(cmd, cap, "/bin/flvm ", "/bin/flvm --time ");
    replace_token(cmd, cap, "/bin/flvm.exe ", "/bin/flvm.exe --time ");
    replace_token(cmd, cap, "\\bin\\flvm ", "\\bin\\flvm --time ");
    replace_token(cmd, cap, "\\bin\\flvm.exe ", "\\bin\\flvm.exe --time ");
    replace_token(cmd, cap, "/bin/flatl crun ", "/bin/flatl crun --time ");
    replace_token(cmd, cap, "/bin/flatl.exe crun ", "/bin/flatl.exe crun --time ");
    replace_token(cmd, cap, "/bin/flatl compile ", "/bin/flatl compile --time ");
    replace_token(cmd, cap, "/bin/flatl.exe compile ", "/bin/flatl.exe compile --time ");
    replace_token(cmd, cap, "/bin/flatl run ", "/bin/flatl run --time ");
    replace_token(cmd, cap, "/bin/flatl.exe run ", "/bin/flatl.exe run --time ");
    replace_token(cmd, cap, "/bin/flatl run --trace ", "/bin/flatl run --time --trace ");
    replace_token(cmd, cap, "/bin/flatl.exe run --trace ", "/bin/flatl.exe run --time --trace ");
}

static int shell_capture_timed(const char *cmd, int *code, char *out, size_t out_sz, double *ms_out) {
    double t0 = flat_time_monotonic_ms();
    int rc = flat_port_shell_capture(cmd, code, out, (int)out_sz);
    if (ms_out) {
        *ms_out = flat_time_monotonic_ms() - t0;
    }
    (void)t0;
    return rc;
}

static void timing_finish(FlatCaseTiming *timing, double t_case_start, int measure) {
    if (!timing || !measure) {
        return;
    }
    timing->total_ms = flat_time_monotonic_ms() - t_case_start;
    timing->valid = 1;
}

static void parse_flatl_crun_capture(const char *out, FlatCaseTiming *timing) {
    const char *line = strstr(out, "flatl: time_ms");
    if (!line) {
        return;
    }
    if (flat_time_parse_field_ms(line, "compile", &timing->compile_ms) == 0) {
        timing->have_compile = 1;
    }
    if (flat_time_parse_field_ms(line, "run", &timing->run_ms) == 0) {
        timing->have_run = 1;
    }
    flat_time_parse_tool_line(out, "flatl", &timing->units, &timing->fmod, &timing->have_units,
                              &timing->have_fmod);
}

void flat_case_format_time_suffix(const FlatCase *c, int use_flatl, const FlatCaseTiming *t,
                                  char *buf, size_t buf_sz) {
    if (!t || !t->valid || !buf || buf_sz == 0) {
        if (buf && buf_sz > 0) {
            buf[0] = '\0';
        }
        return;
    }
    flat_time_line_begin(buf, buf_sz);
    flat_time_append_field(buf, buf_sz, "prep", t->prep_ms);
    if (c->kind == CASE_KIND_EXEC && t->have_exec) {
        flat_time_append_field(buf, buf_sz, "exec", t->exec_ms);
    } else if (c->kind == CASE_KIND_PIPELINE) {
        if (t->have_flc) {
            flat_time_append_field(buf, buf_sz, "flc", t->flc_ms);
        }
        if (t->have_flvm) {
            flat_time_append_field(buf, buf_sz, "flvm", t->flvm_ms);
        }
        if (t->have_compile) {
            flat_time_append_field(buf, buf_sz, "compile", t->compile_ms);
        }
        if (t->have_run) {
            flat_time_append_field(buf, buf_sz, "run", t->run_ms);
        }
        if (t->have_units) {
            flat_time_append_int_field(buf, buf_sz, "units", t->units);
        }
        if (t->have_fmod) {
            flat_time_append_int_field(buf, buf_sz, "fmod", t->fmod);
        }
    }
    flat_time_append_field(buf, buf_sz, "total", t->total_ms);
    (void)use_flatl;
}

int flat_case_run(const FlatCase *c, const char *repo_root, int use_flatl, char *err, int err_size) {
    return flat_case_run_ex(c, repo_root, use_flatl, NULL, NULL, err, err_size);
}

int flat_case_run_ex(const FlatCase *c, const char *repo_root, int use_flatl,
                     const FlatCaseRunOptions *opts, FlatCaseTiming *timing, char *err, int err_size) {
    int i;

    int inject = opts && opts->inject_tool_time;
    int measure = opts && opts->measure;
    double t_case = 0.0;
    double t_prep_done = 0.0;

    if (!c || !repo_root) {
        snprintf(err, (size_t)err_size, "invalid arguments");
        return -1;
    }
    if (measure) {
        t_case = flat_time_monotonic_ms();
        if (timing) {
            memset(timing, 0, sizeof *timing);
        }
    }

    if (c->kind == CASE_KIND_FILES) {
        t_prep_done = flat_time_monotonic_ms();
        for (i = 0; i < c->path_check_count; i++) {
            if (!path_exists_at(repo_root, c->path_checks[i])) {
                snprintf(err, (size_t)err_size, "missing path: %s", c->path_checks[i]);
                timing_finish(timing, t_case, measure);
                return -1;
            }
        }
        if (timing && measure) {
            timing->prep_ms = t_prep_done - t_case;
            timing_finish(timing, t_case, measure);
        }
        return 0;
    }

    if (c->kind == CASE_KIND_EXEC) {
        char cmd[FLAT_CASE_MAX_CMD];
        char out[4096];
        int code = -1;
        double exec_ms = 0.0;

        expand_placeholders(cmd, sizeof cmd, repo_root, c->case_dir, NULL, NULL, c->cmd);
        if (inject) {
            inject_tool_time(cmd, sizeof cmd);
        }
        flat_port_prepare_shell_cmd(cmd, sizeof cmd);
        t_prep_done = flat_time_monotonic_ms();
        if (timing && measure) {
            timing->prep_ms = t_prep_done - t_case;
        }
        if (shell_capture_timed(cmd, &code, out, sizeof out, &exec_ms) != 0) {
            snprintf(err, (size_t)err_size, "failed to run: %s", cmd);
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (code != c->expect_exit) {
            snprintf(err, (size_t)err_size, "exit %d want %d (cmd: %s)", code, c->expect_exit, cmd);
            if (timing && measure) {
                timing->exec_ms = exec_ms;
                timing->have_exec = 1;
            }
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (check_stdout_all(c, out, err, err_size) != 0) {
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (check_output(out, c->stderr_contains, err, err_size) != 0) {
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (timing && measure) {
            timing->exec_ms = exec_ms;
            timing->have_exec = 1;
            timing_finish(timing, t_case, measure);
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

        if (use_flatl && c->flatl_crun[0] != '\0') {
            double crun_ms = 0.0;
            if (c->flatl_compile[0] != '\0' || c->flatl_run[0] != '\0') {
                snprintf(err, (size_t)err_size, "[flatl]: use crun or compile+run, not both");
                timing_finish(timing, t_case, measure);
                return -1;
            }
            expand_placeholders(compile_cmd, sizeof compile_cmd, repo_root, c->case_dir, tmpdir,
                                src_path, c->flatl_crun);
            if (inject) {
                inject_tool_time(compile_cmd, sizeof compile_cmd);
            }
            flat_port_prepare_shell_cmd(compile_cmd, sizeof compile_cmd);
            t_prep_done = flat_time_monotonic_ms();
            if (timing && measure) {
                timing->prep_ms = t_prep_done - t_case;
            }
            if (shell_capture_timed(compile_cmd, &code, out, sizeof out, &crun_ms) != 0) {
                snprintf(err, (size_t)err_size, "flatl crun failed: %s", compile_cmd);
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (timing && measure) {
                parse_flatl_crun_capture(out, timing);
                if (!timing->have_compile && !timing->have_run) {
                    timing->compile_ms = crun_ms;
                    timing->have_compile = 1;
                }
            }
            if (check_exit_want(code, c->flatl_expect_exit_set ? c->flatl_expect_exit : -1,
                                "flatl crun", err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (check_stdout_flatl(c, out, err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (check_stderr_flatl(c, out, err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            timing_finish(timing, t_case, measure);
            return 0;
        }

        if (use_flatl && c->flatl_compile[0] != '\0' && c->flatl_run[0] != '\0') {
            double compile_ms = 0.0;
            double run_ms = 0.0;
            expand_placeholders(compile_cmd, sizeof compile_cmd, repo_root, c->case_dir, tmpdir,
                                src_path, c->flatl_compile);
            expand_placeholders(run_cmd, sizeof run_cmd, repo_root, c->case_dir, tmpdir, src_path,
                                c->flatl_run);
            if (inject) {
                inject_tool_time(compile_cmd, sizeof compile_cmd);
                inject_tool_time(run_cmd, sizeof run_cmd);
            }
            flat_port_prepare_shell_cmd(compile_cmd, sizeof compile_cmd);
            flat_port_prepare_shell_cmd(run_cmd, sizeof run_cmd);
            t_prep_done = flat_time_monotonic_ms();
            if (timing && measure) {
                timing->prep_ms = t_prep_done - t_case;
            }
            if (shell_capture_timed(compile_cmd, &code, out, sizeof out, measure ? &compile_ms : NULL) !=
                0) {
                snprintf(err, (size_t)err_size, "flatl compile failed: %s", compile_cmd);
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (timing && measure) {
                timing->compile_ms = compile_ms;
                timing->have_compile = 1;
                flat_time_parse_tool_line(out, "flatl", &timing->units, &timing->fmod,
                                          &timing->have_units, &timing->have_fmod);
            }
            if (code != 0) {
                snprintf(err, (size_t)err_size, "flatl compile exit %d: %s", code, compile_cmd);
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (shell_capture_timed(run_cmd, &code, out, sizeof out, measure ? &run_ms : NULL) != 0) {
                snprintf(err, (size_t)err_size, "flatl run failed: %s", run_cmd);
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (timing && measure) {
                timing->run_ms = run_ms;
                timing->have_run = 1;
            }
            if (check_exit_want(code, c->flatl_expect_exit_set ? c->flatl_expect_exit : -1,
                                "flatl run", err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (check_stdout_flatl(c, out, err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            if (check_stderr_flatl(c, out, err, err_size) != 0) {
                timing_finish(timing, t_case, measure);
                return -1;
            }
            timing_finish(timing, t_case, measure);
            return 0;
        }

        if (use_flatl && c->flatl_section_seen) {
            snprintf(err, (size_t)err_size,
                     "[flatl] section incomplete (need crun or compile+run)");
            return -1;
        }

        if (use_flatl) {
            expand_placeholders(compile_cmd, sizeof compile_cmd, repo_root, c->case_dir, tmpdir,
                                src_path, FLATL_COMPILE);
        } else {
            expand_placeholders(compile_cmd, sizeof compile_cmd, repo_root, c->case_dir, tmpdir,
                                src_path, c->pipeline_compile);
        }
        expand_placeholders(run_cmd, sizeof run_cmd, repo_root, c->case_dir, tmpdir, src_path,
                            c->pipeline_run);
        if (use_flatl) {
            subst_flvm_with_flatl_run(run_cmd, sizeof run_cmd);
        }
        if (inject) {
            inject_tool_time(compile_cmd, sizeof compile_cmd);
            inject_tool_time(run_cmd, sizeof run_cmd);
        }
        flat_port_prepare_shell_cmd(compile_cmd, sizeof compile_cmd);
        flat_port_prepare_shell_cmd(run_cmd, sizeof run_cmd);
        t_prep_done = flat_time_monotonic_ms();
        if (timing && measure) {
            timing->prep_ms = t_prep_done - t_case;
        }
        {
            double flc_ms = 0.0;
            double flvm_ms = 0.0;
        if (shell_capture_timed(compile_cmd, &code, out, sizeof out, measure ? &flc_ms : NULL) != 0) {
            snprintf(err, (size_t)err_size, "compile failed: %s", compile_cmd);
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (timing && measure) {
            if (use_flatl) {
                timing->have_compile = 1;
                timing->compile_ms = flc_ms;
                timing->have_flc = 0;
                flat_time_parse_tool_line(out, "flatl", &timing->units, &timing->fmod,
                                          &timing->have_units, &timing->have_fmod);
            } else {
                timing->flc_ms = flc_ms;
                timing->have_flc = 1;
                flat_time_parse_tool_line(out, "flc", &timing->units, &timing->fmod,
                                          &timing->have_units, &timing->have_fmod);
            }
        }
        if (code != 0) {
            snprintf(err, (size_t)err_size, "%s exit %d: %s", use_flatl ? "flatl compile" : "flc",
                     code, compile_cmd);
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (shell_capture_timed(run_cmd, &code, out, sizeof out, measure ? &flvm_ms : NULL) != 0) {
            snprintf(err, (size_t)err_size, "run failed: %s", run_cmd);
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (timing && measure) {
            if (use_flatl) {
                timing->have_run = 1;
                timing->run_ms = flvm_ms;
                timing->have_flvm = 0;
            } else {
                timing->flvm_ms = flvm_ms;
                timing->have_flvm = 1;
            }
        }
        if (code != c->expect_exit) {
            snprintf(err, (size_t)err_size, "%s exit %d want %d", use_flatl ? "flatl run" : "vm",
                     code, c->expect_exit);
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (check_stdout_all(c, out, err, err_size) != 0) {
            timing_finish(timing, t_case, measure);
            return -1;
        }
        if (check_output(out, c->stderr_contains, err, err_size) != 0) {
            timing_finish(timing, t_case, measure);
            return -1;
        }
        timing_finish(timing, t_case, measure);
        return 0;
        }
    }

    snprintf(err, (size_t)err_size, "unknown or unset case kind");
    timing_finish(timing, t_case, measure);
    return -1;
}
