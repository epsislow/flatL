#include "casefile.h"
#include "discover.h"
#include "flat_str.h"
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
    printf("  -e, --each                 progress grouped per suite\n");
    printf("  -w, --progress-width N     max . / F characters per line (default: %d)\n",
           DEFAULT_PROGRESS_WIDTH);
    printf("  --flatl                    pipeline: flatl compile + flatl run (keeps run pipes)\n");
}

static void progress_finish_line(Progress *p) {
    if (p->chars_on_line > 0) {
        printf(" (%d/%d)\n", p->run, p->total);
        p->chars_on_line = 0;
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
}

static void progress_on_result(Progress *p, int ok, const FlatCase *c, int verbose,
                               const char *err) {
    p->run++;
    if (ok) {
        printf("%s.%s", ANSI_GREEN, ANSI_RESET);
        p->chars_on_line++;
        if (p->chars_on_line >= p->width) {
            progress_finish_line(p);
        }
    } else {
        printf("%sF%s", ANSI_RED, ANSI_RESET);
        progress_on_fail(p, c, verbose, err);
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

static int parse_args(int argc, char **argv, FilterList *fl, int *verbose, int *each, int *help,
                      int *progress_width, int *use_flatl) {
    int i;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            *help = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            *verbose = 1;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--each") == 0) {
            *each = 1;
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
                          const char *suite_only) {
    int i;
    int n = 0;

    for (i = 0; i < ncases; i++) {
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

int main(int argc, char **argv) {
    char repo[512];
    FlatCase *cases = NULL;
    int ncases = 0;
    FilterList filters = {0};
    int verbose = 0;
    int each = 0;
    int help = 0;
    int progress_width = DEFAULT_PROGRESS_WIDTH;
    int use_flatl = 0;
    Progress progress;
    int run_total = 0;
    int passed = 0;
    int failed = 0;
    int i;

    if (parse_args(argc, argv, &filters, &verbose, &each, &help, &progress_width, &use_flatl) !=
        0) {
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

    run_total = count_filtered(cases, ncases, &filters, NULL);
    progress_init(&progress, run_total, progress_width);

    printf("Running (%d discovered)", ncases);
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
            int ok;

            if (!filter_match(&filters, c)) {
                continue;
            }
            if (each && strcmp(cur_suite, c->suite) != 0) {
                if (cur_suite[0] != '\0') {
                    progress_finish_line(&progress);
                    printf("\n");
                }
                cur_suite = c->suite;
                progress_init(&progress, count_filtered(cases, ncases, &filters, cur_suite),
                              progress_width);
                printf("%s%s:%s\n", ANSI_YELLOW, c->suite, ANSI_RESET);
            }

            ok = flat_case_run(c, repo, use_flatl, err, sizeof err) == 0;
            if (ok) {
                passed++;
            } else {
                failed++;
            }
            progress_on_result(&progress, ok, c, verbose, err);
        }
        progress_finish_line(&progress);
        if (progress.run == 0) {
            printf(" (0/%d)\n", progress.total);
        }
        printf("\n");
    }

    printf("Passed: %s%d%s Failed: %s%d%s Total: %s%d%s\n", ANSI_YELLOW, passed, ANSI_RESET,
           ANSI_YELLOW, failed, ANSI_RESET, ANSI_YELLOW, passed + failed, ANSI_RESET);

    flat_discover_free(cases);
    filters_free(&filters);
    return failed ? 1 : 0;
}
