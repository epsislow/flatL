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
    printf("  suite[id-spec]   e.g. smoke[1]  smoke[1-3,5]  (omit = all cases)\n\n");
    printf("Options:\n");
    printf("  -h, --help       show help\n");
    printf("  -v, --verbose    print failure details\n");
    printf("  -e, --each       progress line per suite\n");
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

static int parse_args(int argc, char **argv, FilterList *fl, int *verbose, int *each, int *help) {
    int i;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            *help = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            *verbose = 1;
        } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--each") == 0) {
            *each = 1;
        } else {
            const char *lb = strchr(arg, '[');
            if (!lb) {
                fprintf(stderr, "Unknown argument: %s\n", arg);
                return -1;
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
    int passed = 0;
    int failed = 0;
    int i;

    if (parse_args(argc, argv, &filters, &verbose, &each, &help) != 0) {
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

    printf("Running (%d discovered):\n", ncases);
    if (each) {
        printf("\n");
    }

    {
        const char *cur_suite = "";
        for (i = 0; i < ncases; i++) {
            FlatCase *c = &cases[i];
            char err[512];

            if (!filter_match(&filters, c)) {
                continue;
            }
            if (each && strcmp(cur_suite, c->suite) != 0) {
                if (cur_suite[0] != '\0') {
                    printf("\n");
                }
                printf("%s%s:%s\n", ANSI_YELLOW, c->suite, ANSI_RESET);
                cur_suite = c->suite;
            }

            if (flat_case_run(c, repo, err, sizeof err) == 0) {
                passed++;
                printf("%s.%s", ANSI_GREEN, ANSI_RESET);
            } else {
                failed++;
                printf("%sF%s", ANSI_RED, ANSI_RESET);
                if (verbose) {
                    printf("\n  FAIL %s#%d — %s\n    %s\n", c->suite, c->id, c->desc, err);
                } else {
                    printf("\n  FAIL %s#%d — %s\n", c->suite, c->id, c->desc);
                }
            }
        }
        printf("\n");
    }

    printf("Passed: %s%d%s Failed: %s%d%s Total: %s%d%s\n", ANSI_YELLOW, passed, ANSI_RESET,
           ANSI_YELLOW, failed, ANSI_RESET, ANSI_YELLOW, passed + failed, ANSI_RESET);

    flat_discover_free(cases);
    filters_free(&filters);
    return failed ? 1 : 0;
}
