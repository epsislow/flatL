#include "casefile.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    FlatCase *items;
    int count;
    int cap;
} CaseList;

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

int flat_discover_cases(const char *repo_root, FlatCase **out_cases, int *out_count) {
    char cases_root[512];
    DIR *suites;
    struct dirent *se;
    CaseList list = {0};

    if (!repo_root || !out_cases || !out_count) {
        return -1;
    }

    snprintf(cases_root, sizeof cases_root, "%s/test/cases", repo_root);
    suites = opendir(cases_root);
    if (!suites) {
        return -1;
    }

    while ((se = readdir(suites)) != NULL) {
        char suite_path[512];
        DIR *ids;

        if (se->d_name[0] == '.') {
            continue;
        }
        snprintf(suite_path, sizeof suite_path, "%s/%s", cases_root, se->d_name);
        ids = opendir(suite_path);
        if (!ids) {
            continue;
        }
        {
            struct dirent *ie;
            while ((ie = readdir(ids)) != NULL) {
                char case_path[512];
                char toml[512];
                FlatCase c;

                if (ie->d_name[0] == '.') {
                    continue;
                }
                snprintf(case_path, sizeof case_path, "%s/%s", suite_path, ie->d_name);
                snprintf(toml, sizeof toml, "%s/case.toml", case_path);
                if (flat_case_load(toml, &c) != 0) {
                    continue;
                }
                if (c.suite[0] == '\0') {
                    snprintf(c.suite, sizeof c.suite, "%s", se->d_name);
                }
                if (case_list_push(&list, &c) != 0) {
                    closedir(ids);
                    closedir(suites);
                    free(list.items);
                    return -1;
                }
            }
        }
        closedir(ids);
    }
    closedir(suites);

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
