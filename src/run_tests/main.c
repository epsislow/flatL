#include "casefile.h"
#include "discover.h"
#include "flat_port.h"
#include "flat_str.h"
#include "flat_time.h"
#include "repo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANSI_GREEN "\x1b[32m"
#define ANSI_RED "\x1b[31m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RESET "\x1b[0m"

#define DEFAULT_PROGRESS_WIDTH 40

typedef struct {
    int run;
    int chars_on_line;
    int total;
    int width;
} Progress;

typedef struct {
    char suite[FLAT_CASE_SUITE_LEN];
    int *ids;
    int id_count;
    int id_cap;
    int all_ids;
} SuiteFilter;

typedef struct {
    SuiteFilter *items;
    int count;
    int cap;
} FilterList;

typedef struct {
    char tags[16][FLAT_CASE_CATEG_LEN];
    int count;
    int all;
    int explicit_spec;
} CategFilter;

typedef struct {
    char suite[FLAT_CASE_SUITE_LEN];
    int id;
    char desc[FLAT_CASE_MAX_DESC];
} FailEntry;

static void print_help(const char *prog) {
    printf("flatL test runner\n\n");
    printf("Usage:\n");
    printf("  %s [filters...] [options]\n\n", prog);
    printf("Filters:\n");
    printf("  suite            e.g. flatl  json  (all cases in that suite)\n");
    printf("  suite[id-spec]   e.g. smoke[1]  smoke[1-3,5]  (omit = all cases)\n\n");
    printf("Options:\n");
    printf("  -h, --help                 show help\n");
    printf("  -v, --verbose              print failure details\n");
    printf("  -e, --each                 per suite: #id (k/N) desc + . / F (N = selected in suite)\n");
    printf("  -t, --time                 per-case timings (use with -e; injects --time in toolchain)\n");
    printf("  -w, --progress-width N     max . / F characters per line (default: %d)\n",
           DEFAULT_PROGRESS_WIDTH);
    printf("  --flatl                    pipeline: [flatl] in case.toml, else compile+run fallback\n");
    printf("  --categ=TAG[,TAG...]       run cases with categ TAG (default run: categ=default)\n");
    printf("  --categ=all                all categories (same as make test-all)\n");
    printf("  --list                     list suite names and case counts (no tests run)\n");
    printf("\nCases with mods= in case.toml are skipped when a required module is not in\n");
    printf("  `bin/flvm --mods` (fallback: flc / flatl). Example: mods = \"http\"\n");
    printf("\nSession footer Total tests time: is always printed on stdout after the summary.\n");
}

#define BUILT_MODS_MAX 64
#define BUILT_MOD_LEN 32

typedef struct {
    char ids[BUILT_MODS_MAX][BUILT_MOD_LEN];
    int count;
    int loaded;
} BuiltMods;

static int built_mods_has(const BuiltMods *bm, const char *mod) {
    int i;
    if (!bm || !mod) {
        return 0;
    }
    for (i = 0; i < bm->count; i++) {
        if (strcmp(bm->ids[i], mod) == 0) {
            return 1;
        }
    }
    return 0;
}

static int case_missing_mod(const FlatCase *c, const BuiltMods *bm, char *missing, size_t missing_sz) {
    int i;
    if (!c || c->mods_count <= 0) {
        return 0;
    }
    if (!bm || !bm->loaded) {
        return 0;
    }
    for (i = 0; i < c->mods_count; i++) {
        if (!built_mods_has(bm, c->mods[i])) {
            if (missing && missing_sz > 0) {
                snprintf(missing, missing_sz, "%s", c->mods[i]);
            }
            return 1;
        }
    }
    return 0;
}

static int load_built_mods(const char *repo, BuiltMods *out) {
    char cmd[FLAT_CASE_MAX_PATH * 2];
    char buf[4096];
    int exit_code = 1;
    char *line;

    if (!repo || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    snprintf(cmd, sizeof cmd, "%s/bin/flvm --mods", repo);
    flat_port_prepare_shell_cmd(cmd, sizeof cmd);
    if (flat_port_shell_capture(cmd, &exit_code, buf, (int)sizeof buf) != 0 || exit_code != 0) {
        snprintf(cmd, sizeof cmd, "%s/bin/flc --mods", repo);
        flat_port_prepare_shell_cmd(cmd, sizeof cmd);
        if (flat_port_shell_capture(cmd, &exit_code, buf, (int)sizeof buf) != 0 || exit_code != 0) {
            snprintf(cmd, sizeof cmd, "%s/bin/flatl --mods", repo);
            flat_port_prepare_shell_cmd(cmd, sizeof cmd);
            if (flat_port_shell_capture(cmd, &exit_code, buf, (int)sizeof buf) != 0 ||
                exit_code != 0) {
                return -1;
            }
        }
    }
    line = buf;
    while (*line) {
        char *p;
        char *eol;
        size_t n;
        eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') {
            eol++;
        }
        p = line;
        line = (*eol) ? eol + 1 : eol;
        while (*line == '\n' || *line == '\r') {
            line++;
        }
        while (p < eol && isspace((unsigned char)*p)) {
            p++;
        }
        while (eol > p && isspace((unsigned char)eol[-1])) {
            eol--;
        }
        if (p >= eol || *p == '#') {
            continue;
        }
        n = (size_t)(eol - p);
        if (n == 0 || out->count >= BUILT_MODS_MAX) {
            continue;
        }
        if (n >= BUILT_MOD_LEN) {
            n = BUILT_MOD_LEN - 1;
        }
        memcpy(out->ids[out->count], p, n);
        out->ids[out->count][n] = '\0';
        out->count++;
    }
    out->loaded = 1;
    return 0;
}

static void progress_flush(void) {
    fflush(stdout);
}

static void progress_finish_line(Progress *p) {
    if (p->chars_on_line > 0) {
        printf(" (%d/%d)\n", p->run, p->total);
        p->chars_on_line = 0;
        progress_flush();
    }
}

static void progress_init(Progress *p, int total, int width) {
    p->run = 0;
    p->chars_on_line = 0;
    p->total = total;
    p->width = width > 0 ? width : DEFAULT_PROGRESS_WIDTH;
}

static void progress_on_fail(Progress *p, const FlatCase *c, int verbose, const char *err) {
    if (verbose) {
        printf("\n  FAIL %s#%d — %s\n    %s\n", c->suite, c->id, c->desc, err);
    } else {
        printf("\n  FAIL %s#%d — %s\n", c->suite, c->id, c->desc);
    }
    p->chars_on_line = 0;
    progress_flush();
}

static void progress_on_result(Progress *p, int ok, const FlatCase *c, int verbose, int each,
                                 int show_time, int use_flatl, const FlatCaseTiming *timing,
                                 const char *err) {
    char suffix[512];

    p->run++;
    if (each) {
        printf("  #%d (%d/%d) %s\n  ", c->id, p->run, p->total, c->desc);
    }
    if (ok) {
        printf("%s.%s", ANSI_GREEN, ANSI_RESET);
    } else {
        printf("%sF%s", ANSI_RED, ANSI_RESET);
    }
    if (show_time && timing && timing->valid) {
        flat_case_format_time_suffix(c, use_flatl, timing, suffix, sizeof suffix);
        flat_time_print_runner_suffix(suffix);
    }
    if (each || show_time) {
        printf("\n");
        p->chars_on_line = 0;
    } else if (ok) {
        p->chars_on_line++;
        if (p->chars_on_line >= p->width) {
            progress_finish_line(p);
        }
    } else {
        progress_on_fail(p, c, verbose, err);
        return;
    }
    progress_flush();
    if (!ok && !each && !show_time) {
        progress_on_fail(p, c, verbose, err);
    } else if (!ok && (each || show_time)) {
        if (verbose) {
            printf("    %s\n", err);
        }
    }
}

static int parse_positive_int(const char *s, int fallback) {
    char *end;
    long v;

    if (!s || !*s) {
        return fallback;
    }
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v <= 0) {
        return fallback;
    }
    return (int)v;
}

static int parse_id_spec(const char *spec, int **out_ids, int *out_count) {
    char buf[256];
    char *bp;
    char *tok;
    int cap = 0;
    int count = 0;
    int *ids = NULL;

    snprintf(buf, sizeof buf, "%s", spec);
    bp = buf;
    while ((tok = flat_str_next(&bp, ',')) != NULL) {
        tok = flat_str_trim(tok);
        if (*tok == '\0') {
            continue;
        }
        {
            char *dash = strchr(tok, '-');
            if (dash) {
                int a = atoi(tok);
                int b = atoi(dash + 1);
                int i;
                if (a > b) {
                    int t = a;
                    a = b;
                    b = t;
                }
                for (i = a; i <= b; i++) {
                    if (count >= cap) {
                        cap = cap ? cap * 2 : 8;
                        ids = realloc(ids, (size_t)cap * sizeof(int));
                    }
                    ids[count++] = i;
                }
            } else {
                if (count >= cap) {
                    cap = cap ? cap * 2 : 8;
                    ids = realloc(ids, (size_t)cap * sizeof(int));
                }
                ids[count++] = atoi(tok);
            }
        }
    }
    *out_ids = ids;
    *out_count = count;
    return count > 0 ? 0 : -1;
}

static void categ_filter_init_default(CategFilter *cf) {
    memset(cf, 0, sizeof *cf);
    snprintf(cf->tags[0], sizeof cf->tags[0], "%s", FLAT_CASE_CATEG_DEFAULT);
    cf->count = 1;
}

static int categ_filter_add(CategFilter *cf, const char *tag) {
    int i;

    if (!cf || !tag || tag[0] == '\0') {
        return -1;
    }
    if (strlen(tag) >= FLAT_CASE_CATEG_LEN) {
        return -1;
    }
    for (i = 0; i < cf->count; i++) {
        if (strcmp(cf->tags[i], tag) == 0) {
            return 0;
        }
    }
    if (cf->count >= 16) {
        return -1;
    }
    snprintf(cf->tags[cf->count], sizeof cf->tags[cf->count], "%s", tag);
    cf->count++;
    return 0;
}

static int categ_filter_parse_spec(CategFilter *cf, const char *spec) {
    char buf[256];
    char *bp;
    char *tok;

    if (!cf || !spec) {
        return -1;
    }
    memset(cf, 0, sizeof *cf);
    cf->explicit_spec = 1;
    if (strcmp(spec, "all") == 0 || strcmp(spec, "*") == 0) {
        cf->all = 1;
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", spec);
    bp = buf;
    while ((tok = flat_str_next(&bp, ',')) != NULL) {
        tok = flat_str_trim(tok);
        if (*tok == '\0') {
            continue;
        }
        if (categ_filter_add(cf, tok) != 0) {
            return -1;
        }
    }
    return cf->count > 0 ? 0 : -1;
}

static int categ_match(const CategFilter *cf, const FlatCase *c) {
    int i;
    const char *tag;

    if (!cf || !c) {
        return 0;
    }
    if (cf->all) {
        return 1;
    }
    tag = c->categ[0] ? c->categ : FLAT_CASE_CATEG_DEFAULT;
    for (i = 0; i < cf->count; i++) {
        if (strcmp(cf->tags[i], tag) == 0) {
            return 1;
        }
    }
    return 0;
}

static int filter_push(FilterList *fl, const char *suite, const char *spec) {
    SuiteFilter *sf;

    if (fl->count >= fl->cap) {
        int n = fl->cap ? fl->cap * 2 : 4;
        SuiteFilter *p = realloc(fl->items, (size_t)n * sizeof(SuiteFilter));
        if (!p) {
            return -1;
        }
        fl->items = p;
        fl->cap = n;
    }
    sf = &fl->items[fl->count++];
    memset(sf, 0, sizeof *sf);
    snprintf(sf->suite, sizeof sf->suite, "%s", suite);
    if (spec == NULL || spec[0] == '\0' || strcmp(spec, "*") == 0) {
        sf->all_ids = 1;
        return 0;
    }
    return parse_id_spec(spec, &sf->ids, &sf->id_count);
}

static int parse_args(int argc, char **argv, FilterList *fl, CategFilter *categ, int *verbose,
                      int *each, int *help, int *progress_width, int *use_flatl, int *show_time,
                      int *list_only) {
    int i;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            *help = 1;
        } else if (strcmp(arg, "--list") == 0) {
            *list_only = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            *verbose = 1;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--each") == 0) {
            *each = 1;
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--time") == 0) {
            *show_time = 1;
        } else if (strcmp(arg, "--flatl") == 0) {
            *use_flatl = 1;
        } else if (strcmp(arg, "-w") == 0 || strcmp(arg, "--progress-width") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires a number\n", arg);
                return -1;
            }
            *progress_width = parse_positive_int(argv[++i], *progress_width);
        } else if (strncmp(arg, "--progress-width=", 17) == 0) {
            *progress_width = parse_positive_int(arg + 17, *progress_width);
        } else if (strncmp(arg, "--categ=", 8) == 0) {
            if (categ_filter_parse_spec(categ, arg + 8) != 0) {
                fprintf(stderr, "Invalid --categ: %s\n", arg + 8);
                return -1;
            }
        } else if (arg[0] == '-' && arg[1] == 'w' && arg[2] != '\0' && isdigit((unsigned char)arg[2])) {
            *progress_width = parse_positive_int(arg + 2, *progress_width);
        } else {
            const char *lb = strchr(arg, '[');
            if (!lb) {
                if (filter_push(fl, arg, NULL) != 0) {
                    fprintf(stderr, "Invalid filter: %s\n", arg);
                    return -1;
                }
                continue;
            }
            {
                char suite[FLAT_CASE_SUITE_LEN];
                char spec[256];
                size_t n = (size_t)(lb - arg);
                const char *rb = strchr(lb, ']');
                if (!rb || n >= sizeof suite) {
                    fprintf(stderr, "Invalid filter: %s\n", arg);
                    return -1;
                }
                memcpy(suite, arg, n);
                suite[n] = '\0';
                snprintf(spec, sizeof spec, "%.*s", (int)(rb - lb - 1), lb + 1);
                if (filter_push(fl, suite, spec) != 0) {
                    fprintf(stderr, "Invalid id spec in: %s\n", arg);
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int filter_match(const FilterList *fl, const FlatCase *c) {
    int i;
    if (fl->count == 0) {
        return 1;
    }
    for (i = 0; i < fl->count; i++) {
        const SuiteFilter *sf = &fl->items[i];
        int j;
        if (strcmp(sf->suite, c->suite) != 0) {
            continue;
        }
        if (sf->all_ids) {
            return 1;
        }
        for (j = 0; j < sf->id_count; j++) {
            if (sf->ids[j] == c->id) {
                return 1;
            }
        }
    }
    return 0;
}

static int count_filtered(const FlatCase *cases, int ncases, const FilterList *fl,
                          const CategFilter *categ, const char *suite_only) {
    int i;
    int n = 0;

    for (i = 0; i < ncases; i++) {
        if (!categ_match(categ, &cases[i])) {
            continue;
        }
        if (!filter_match(fl, &cases[i])) {
            continue;
        }
        if (suite_only && strcmp(cases[i].suite, suite_only) != 0) {
            continue;
        }
        n++;
    }
    return n;
}

static void filters_free(FilterList *fl) {
    int i;
    for (i = 0; i < fl->count; i++) {
        free(fl->items[i].ids);
    }
    free(fl->items);
    memset(fl, 0, sizeof *fl);
}

static int fail_list_push(FailEntry **items, int *count, int *cap, const FlatCase *c) {
    FailEntry *p;

    if (*count >= *cap) {
        int n = *cap ? *cap * 2 : 8;
        p = realloc(*items, (size_t)n * sizeof(FailEntry));
        if (!p) {
            return -1;
        }
        *items = p;
        *cap = n;
    }
    snprintf((*items)[*count].suite, sizeof (*items)[*count].suite, "%s", c->suite);
    (*items)[*count].id = c->id;
    snprintf((*items)[*count].desc, sizeof (*items)[*count].desc, "%s", c->desc);
    (*count)++;
    return 0;
}

static void fail_list_print(const FailEntry *items, int count) {
    int i;

    if (count <= 0) {
        return;
    }
    printf("Fails list:\n");
    for (i = 0; i < count; i++) {
        printf("%s#%d — %s\n", items[i].suite, items[i].id, items[i].desc);
    }
}

static void fail_list_free(FailEntry *items) {
    free(items);
}

static void catalog_emit_suite(const char *suite, int count, int min_id, int max_id, int *suite_count,
                               int *case_total) {
    if (count <= 0) {
        return;
    }
    (*suite_count)++;
    *case_total += count;
    if (count == 1) {
        printf("%-24s  %d case   (id %d)\n", suite, count, min_id);
    } else if (min_id == max_id) {
        printf("%-24s  %d cases  (id %d)\n", suite, count, min_id);
    } else {
        printf("%-24s  %d cases  (ids %d–%d)\n", suite, count, min_id, max_id);
    }
}

static void print_suite_catalog(const FlatCase *cases, int ncases, const FilterList *fl,
                                const CategFilter *categ) {
    int i;
    int suite_count = 0;
    int case_total = 0;
    char cur_suite[FLAT_CASE_SUITE_LEN];
    int count = 0;
    int min_id = 0;
    int max_id = 0;

    cur_suite[0] = '\0';

    if (ncases <= 0) {
        printf("(no cases)\n");
        return;
    }
    for (i = 0; i < ncases; i++) {
        const FlatCase *c = &cases[i];
        if (!categ_match(categ, c) || !filter_match(fl, c)) {
            continue;
        }
        if (strcmp(cur_suite, c->suite) != 0) {
            catalog_emit_suite(cur_suite, count, min_id, max_id, &suite_count, &case_total);
            snprintf(cur_suite, sizeof cur_suite, "%s", c->suite);
            count = 1;
            min_id = c->id;
            max_id = c->id;
            continue;
        }
        if (c->id < min_id) {
            min_id = c->id;
        }
        if (c->id > max_id) {
            max_id = c->id;
        }
        count++;
    }
    catalog_emit_suite(cur_suite, count, min_id, max_id, &suite_count, &case_total);

    if (case_total == 0) {
        printf("(no cases match filters)\n");
        return;
    }
    printf("\nTotal: %d cases in %d suite(s)", case_total, suite_count);
    if (ncases != case_total) {
        printf("  (%d discovered)", ncases);
    }
    if (categ != NULL && !categ->all && categ->count > 0) {
        int ci;
        printf("; categ");
        for (ci = 0; ci < categ->count; ci++) {
            printf(ci ? ",%s" : "=%s", categ->tags[ci]);
        }
    } else if (categ != NULL && categ->all) {
        printf("; categ=all");
    }
    printf("\n");
}

int main(int argc, char **argv) {
    char repo[512];
    FlatCase *cases = NULL;
    int ncases = 0;
    FilterList filters = {0};
    CategFilter categ = {0};
    int verbose = 0;
    int each = 0;
    int show_time = 0;
    int help = 0;
    int progress_width = DEFAULT_PROGRESS_WIDTH;
    int use_flatl = 0;
    int list_only = 0;
    Progress progress;
    int run_total = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int i;
    double session_t0;
    FlatCaseRunOptions run_opts = {0};
    char captured_output[4096];
    FailEntry *fail_list = NULL;
    int fail_list_count = 0;
    int fail_list_cap = 0;
    BuiltMods built_mods = {0};

    categ_filter_init_default(&categ);

    if (parse_args(argc, argv, &filters, &categ, &verbose, &each, &help, &progress_width, &use_flatl,
                   &show_time, &list_only) != 0) {
        return 2;
    }
    if (help) {
        print_help(argv[0]);
        filters_free(&filters);
        return 0;
    }

    if (flat_find_repo_root(repo, sizeof repo) != 0) {
        fprintf(stderr, "run_tests: could not find repo root (Makefile + include/flat.h)\n");
        filters_free(&filters);
        return 1;
    }

    if (flat_discover_cases(repo, &cases, &ncases) != 0 || ncases == 0) {
        fprintf(stderr, "run_tests: no cases under test/cases/\n");
        filters_free(&filters);
        return 1;
    }

    if (list_only) {
        print_suite_catalog(cases, ncases, &filters, &categ);
        flat_discover_free(cases);
        filters_free(&filters);
        return 0;
    }

    run_total = count_filtered(cases, ncases, &filters, &categ, NULL);
    progress_init(&progress, run_total, progress_width);
    session_t0 = flat_time_monotonic_ms();
    run_opts.measure = 1;
    run_opts.inject_tool_time = each && show_time;
    run_opts.captured_output = captured_output;
    run_opts.captured_output_size = sizeof captured_output;

    if (load_built_mods(repo, &built_mods) != 0) {
        fprintf(stderr,
                "run_tests: warning: could not read flvm/flc/flatl --mods; mods= skips disabled\n");
        built_mods.loaded = 0;
    }

    printf("Running (%d selected", run_total);
    if (run_total != ncases) {
        printf(", %d discovered", ncases);
    }
    if (!categ.all && categ.count > 0) {
        int ci;
        printf("; categ");
        for (ci = 0; ci < categ.count; ci++) {
            printf(ci ? ",%s" : "=%s", categ.tags[ci]);
        }
    } else if (categ.all) {
        printf("; categ=all");
    }
    printf(")");
    if (use_flatl) {
        printf("  %s[driver: flatl]%s", ANSI_YELLOW, ANSI_RESET);
    }
    printf(":\n");
    if (each) {
        printf("\n");
    }

    {
        const char *cur_suite = "";
        for (i = 0; i < ncases; i++) {
            FlatCase *c = &cases[i];
            char err[512];
            FlatCaseTiming timing = {0};
            int ok;

            if (!categ_match(&categ, c)) {
                continue;
            }
            if (!filter_match(&filters, c)) {
                continue;
            }
            if (each && strcmp(cur_suite, c->suite) != 0) {
                if (cur_suite[0] != '\0') {
                    progress_finish_line(&progress);
                    printf("\n");
                }
                cur_suite = c->suite;
                progress_init(&progress, count_filtered(cases, ncases, &filters, &categ, cur_suite),
                              progress_width);
                printf("%s%s:%s\n", ANSI_YELLOW, c->suite, ANSI_RESET);
            }

            {
                char missing[BUILT_MOD_LEN];
                if (case_missing_mod(c, &built_mods, missing, sizeof missing)) {
                    skipped++;
                    progress.run++;
                    if (each) {
                        printf("  #%d (%d/%d) %s\n  %ss%s  skip (missing mod %s)\n", c->id,
                               progress.run, progress.total, c->desc, ANSI_YELLOW, ANSI_RESET,
                               missing);
                    } else {
                        printf("%ss%s", ANSI_YELLOW, ANSI_RESET);
                        progress.chars_on_line++;
                        if (progress.chars_on_line >= progress.width) {
                            progress_finish_line(&progress);
                        }
                    }
                    fflush(stdout);
                    continue;
                }
            }

            captured_output[0] = '\0';
            ok = flat_case_run_ex(c, repo, use_flatl, &run_opts, &timing, err, sizeof err) == 0;
            if (ok) {
                passed++;
            } else {
                failed++;
                if (!each) {
                    (void)fail_list_push(&fail_list, &fail_list_count, &fail_list_cap, c);
                }
            }
            if (each && show_time) {
                progress_on_result(&progress, ok, c, verbose, each, 1, use_flatl, &timing, err);
            } else {
                progress_on_result(&progress, ok, c, verbose, each, 0, use_flatl, NULL, err);
            }
            if (c->show_output && captured_output[0] != '\0') {
                size_t output_len = strlen(captured_output);
                printf("%s", captured_output);
                if (output_len > 0 && captured_output[output_len - 1] != '\n') {
                    printf("\n");
                }
                fflush(stdout);
            }
        }
        progress_finish_line(&progress);
        if (progress.run == 0) {
            printf(" (0/%d)\n", progress.total);
        }
        printf("\n");
    }

    printf("Passed: %s%d%s Failed: %s%d%s", ANSI_YELLOW, passed, ANSI_RESET, ANSI_YELLOW, failed,
           ANSI_RESET);
    if (skipped > 0) {
        printf(" Skipped: %s%d%s", ANSI_YELLOW, skipped, ANSI_RESET);
    }
    printf(" Total: %s%d%s\n", ANSI_YELLOW, passed + failed + skipped, ANSI_RESET);
    flat_time_print_tests_footer(flat_time_monotonic_ms() - session_t0);
    if (!each) {
        fail_list_print(fail_list, fail_list_count);
    }

    fail_list_free(fail_list);
    flat_discover_free(cases);
    filters_free(&filters);
    return failed ? 1 : 0;
}
