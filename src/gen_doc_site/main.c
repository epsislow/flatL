#include "gen_doc_index.h"
#include "gen_doc_site.h"
#include "repo.h"

#include <stdio.h>
#include <string.h>

static int usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--root DIR] [--index PATH] [--out DIR]\n"
            "\n"
            "  Builds doc_site/ from docs/index.json and Markdown sources.\n"
            "  Default --root: repository root (search upward from cwd).\n"
            "  Default --index: docs/index.json under root.\n"
            "  Default --out: doc_site under root.\n",
            prog);
    return 2;
}

static int path_under_root(const char *root, const char *path) {
    size_t rl = strlen(root);
    if (strncmp(path, root, rl) != 0) {
        return 0;
    }
    if (path[rl] == '\0') {
        return 1;
    }
    if (path[rl] == '/' || path[rl] == '\\') {
        return 1;
    }
    return 0;
}

static int join_path(char *out, size_t out_sz, const char *a, const char *b) {
    if (snprintf(out, out_sz, "%s/%s", a, b) >= (int)out_sz) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char root[512];
    char index_rel[512] = "docs/index.json";
    char out_rel[512] = "doc_site";
    char index_path[640];
    char docs_dir[640];
    char out_dir[640];
    char templates_dir[640];
    char data_js[640];
    GenDocIndex idx;
    char err[512];
    int i;
    int have_root = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            snprintf(root, sizeof root, "%s", argv[++i]);
            have_root = 1;
            continue;
        }
        if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
            snprintf(index_rel, sizeof index_rel, "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            snprintf(out_rel, sizeof out_rel, "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return usage(argv[0]);
        }
        return usage(argv[0]);
    }

    if (!have_root) {
        if (flat_find_repo_root(root, (int)sizeof root) != 0) {
            fprintf(stderr, "gen_doc_site: repository root not found (use --root)\n");
            return 1;
        }
    }

    if (join_path(docs_dir, sizeof docs_dir, root, "docs") != 0 ||
        join_path(index_path, sizeof index_path, root, index_rel) != 0 ||
        join_path(out_dir, sizeof out_dir, root, out_rel) != 0 ||
        join_path(templates_dir, sizeof templates_dir, root, "src/gen_doc_site/templates") != 0 ||
        join_path(data_js, sizeof data_js, out_dir, "js/doc-data.js") != 0) {
        fprintf(stderr, "gen_doc_site: path too long\n");
        return 1;
    }

    if (!path_under_root(root, index_path) || !path_under_root(root, out_dir)) {
        fprintf(stderr, "gen_doc_site: index and out must be under --root\n");
        return 1;
    }
    if (strstr(out_rel, "..") != NULL) {
        fprintf(stderr, "gen_doc_site: invalid --out path\n");
        return 1;
    }

    memset(&idx, 0, sizeof idx);
    if (gen_doc_index_load(docs_dir, index_path, &idx, err, sizeof err) != 0) {
        fprintf(stderr, "gen_doc_site: %s\n", err);
        return 1;
    }

    if (gen_doc_remove_tree(out_dir, err, sizeof err) != 0) {
        fprintf(stderr, "gen_doc_site: %s\n", err);
        gen_doc_index_free(&idx);
        return 1;
    }

    if (gen_doc_copy_templates(templates_dir, out_dir, err, sizeof err) != 0) {
        fprintf(stderr, "gen_doc_site: %s\n", err);
        gen_doc_index_free(&idx);
        return 1;
    }

    if (gen_doc_emit_js(data_js, &idx, err, sizeof err) != 0) {
        fprintf(stderr, "gen_doc_site: %s\n", err);
        gen_doc_index_free(&idx);
        return 1;
    }

    {
        int n = idx.section_count;
        gen_doc_index_free(&idx);
        printf("Wrote %s (%d sections)\n", out_rel, n);
    }
    return 0;
}
