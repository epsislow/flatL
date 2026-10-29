#include "casefile.h"
#include "discover.h"
#include "flat_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISCOVER_PATH_MAX 1024

typedef struct {
    FlatCase *items;
    int count;
    int cap;
} CaseList;

typedef struct {
    CaseList *list;
    const char *suite_name;
    char suite_path[DISCOVER_PATH_MAX];
} SuiteScanCtx;

typedef struct {
    CaseList *list;
    const char *cases_root;
    char cases_root_buf[DISCOVER_PATH_MAX];
} RootScanCtx;

static void copy_suite_dir_name(char *dst, size_t dst_size, const char *dir_name) {
    size_t n = 0;
    while (n + 1 < dst_size && dir_name[n] != '\0') {
        n++;
    }
    memcpy(dst, dir_name, n);
    dst[n] = '\0';
}

static int case_list_push(CaseList *list, const FlatCase *c) {
    if (list->count >= list->cap) {
        int ncap = list->cap ? list->cap * 2 : 32;
        FlatCase *p = realloc(list->items, (size_t)ncap * sizeof(FlatCase));
        if (!p) {
            return -1;
        }
        list->items = p;
        list->cap = ncap;
    }
    list->items[list->count++] = *c;
    return 0;
}

static int cmp_case(const void *a, const void *b) {
    const FlatCase *ca = a;
    const FlatCase *cb = b;
    int s = strcmp(ca->suite, cb->suite);
    if (s != 0) {
        return s;
    }
    return ca->id - cb->id;
}

static int on_case_dir(const char *case_dir_name, void *ctx) {
    SuiteScanCtx *sctx = ctx;
    char case_path[DISCOVER_PATH_MAX];
    char toml[DISCOVER_PATH_MAX];
    FlatCase c;

    if (snprintf(case_path, sizeof case_path, "%s/%s", sctx->suite_path, case_dir_name) >=
        (int)sizeof case_path) {
        return 0;
    }
    if (snprintf(toml, sizeof toml, "%s/case.toml", case_path) >= (int)sizeof toml) {
        return 0;
    }
    if (flat_case_load(toml, &c) != 0) {
        return 0;
    }
    if (c.suite[0] == '\0') {
        copy_suite_dir_name(c.suite, sizeof c.suite, sctx->suite_name);
    }
    if (case_list_push(sctx->list, &c) != 0) {
        return -1;
    }
    return 0;
}

static int on_suite_dir(const char *suite_name, void *ctx) {
    RootScanCtx *rctx = ctx;
    SuiteScanCtx sctx;

    if (snprintf(sctx.suite_path, sizeof sctx.suite_path, "%s/%s", rctx->cases_root,
                 suite_name) >= (int)sizeof sctx.suite_path) {
        return 0;
    }
    sctx.list = rctx->list;
    sctx.suite_name = suite_name;
    return flat_port_scan_dir(sctx.suite_path, on_case_dir, &sctx);
}

int flat_discover_cases(const char *repo_root, FlatCase **out_cases, int *out_count) {
    char cases_root[DISCOVER_PATH_MAX];
    CaseList list = {0};
    RootScanCtx rctx;

    if (!repo_root || !out_cases || !out_count) {
        return -1;
    }

    snprintf(cases_root, sizeof cases_root, "%s/test/cases", repo_root);
    rctx.list = &list;
    rctx.cases_root = cases_root;
    if (flat_port_scan_dir(cases_root, on_suite_dir, &rctx) != 0) {
        return -1;
    }

    if (list.count > 1) {
        qsort(list.items, (size_t)list.count, sizeof(FlatCase), cmp_case);
    }

    *out_cases = list.items;
    *out_count = list.count;
    return 0;
}

void flat_discover_free(FlatCase *cases) {
    free(cases);
}
