#include "doc_lookup.h"

#include "flat_native.h"
#include "flat_native_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void bounded_strcpy(char *dst, size_t dst_sz, const char *src) {
    size_t n;

    if (dst_sz == 0) {
        return;
    }
    n = strlen(src);
    if (n >= dst_sz) {
        n = dst_sz - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void emit_lines_cb(void *userdata, flat_doc_line_cb cb, char **lines, int total,
                          const char *footer_kind) {
    int show = total > FLAT_DOC_LIST_CAP ? FLAT_DOC_LIST_CAP : total;
    int i;
    char footer[64];

    for (i = 0; i < show; i++) {
        cb(userdata, lines[i]);
    }
    if (total > FLAT_DOC_LIST_CAP) {
        cb(userdata, "....");
    }
    snprintf(footer, sizeof footer, "(%d %s)", total, footer_kind);
    cb(userdata, footer);
}

static int optional_visible(const char *mod, const FlatOptionalNativeDecl *decls, int decl_count) {
    int i;

    if (flat_native_is_core_module(mod)) {
        return 1;
    }
    for (i = 0; i < decl_count; i++) {
        if (strcmp(decls[i].module, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

static int resolve_query_module(const char *tok, const FlatOptionalNativeDecl *decls, int decl_count,
                                char *mod_out, size_t mod_sz) {
    int i;

    if (!tok || !tok[0]) {
        return -1;
    }
    for (i = 0; i < decl_count; i++) {
        if (decls[i].alias[0] && strcmp(decls[i].alias, tok) == 0) {
            snprintf(mod_out, mod_sz, "%s", decls[i].module);
            return 0;
        }
    }
    snprintf(mod_out, mod_sz, "%s", tok);
    return 0;
}

static int registry_has_module(const char *mod) {
    size_t i;

    for (i = 0; i < (size_t)FLAT_NATIVE_COUNT; i++) {
        char m[FGIR_NATIVE_MODULE_LEN];
        if (flat_native_module_for_id((uint16_t)i, m, sizeof m) != 0) {
            continue;
        }
        if (strcmp(m, mod) == 0) {
            return 1;
        }
    }
    return 0;
}

static int module_is_visible(const char *mod, const FlatOptionalNativeDecl *decls, int decl_count) {
    if (flat_native_is_core_module(mod)) {
        return 1;
    }
    if (!registry_has_module(mod)) {
        return 0;
    }
    return optional_visible(mod, decls, decl_count);
}

static int query_is_module_dot_shorthand(const char *q) {
    const char *dot = strchr(q, '.');
    size_t n;
    size_t i;

    if (!dot || dot[1] != '\0' || dot == q) {
        return 0;
    }
    n = (size_t)(dot - q);
    for (i = 0; i < (size_t)FLAT_NATIVE_COUNT; i++) {
        char m[FGIR_NATIVE_MODULE_LEN];
        if (flat_native_module_for_id((uint16_t)i, m, sizeof m) != 0) {
            continue;
        }
        if (strncmp(m, q, n + 1) == 0 && strlen(m) > n + 1) {
            return 0;
        }
    }
    return 1;
}

static int alias_for_module(const char *mod, const FlatOptionalNativeDecl *decls, int decl_count,
                            char *alias_out, size_t alias_sz) {
    int i;

    alias_out[0] = '\0';
    for (i = 0; i < decl_count; i++) {
        if (strcmp(decls[i].module, mod) == 0 && decls[i].alias[0] != '\0') {
            snprintf(alias_out, alias_sz, "%s", decls[i].alias);
            return 1;
        }
    }
    return 0;
}

static void format_line(uint16_t id, const char *prefix_tok, char *out, size_t out_sz) {
    char mod[FGIR_NATIVE_MODULE_LEN];
    const char *func;
    const char *dot;

    if (flat_native_module_for_id(id, mod, sizeof mod) != 0) {
        out[0] = '\0';
        return;
    }
    dot = strrchr(flat_native_name(id), '.');
    func = dot ? dot + 1 : flat_native_name(id);
    if (prefix_tok && prefix_tok[0]) {
        snprintf(out, out_sz, "%s.%s", prefix_tok, func);
    } else {
        snprintf(out, out_sz, "%s.%s", mod, func);
    }
}

typedef struct {
    void *userdata;
    flat_doc_line_cb cb;
} DocSink;

static void file_line_cb(void *userdata, const char *line) {
    FILE *out = (FILE *)userdata;
    fputs(line, out);
    fputc('\n', out);
}

static int emit_blocks_cb(DocSink *sink, const FlatBytecode *bc, const char *prefix) {
    const char *matches[FGIR_MAX_BLOCKS];
    int n = 0;
    int i;
    size_t plen;

    if (!bc || !prefix || !bc->block_labels || bc->block_count == 0) {
        sink->cb(sink->userdata, "no blocks");
        return 0;
    }
    plen = strlen(prefix);
    for (i = 0; i < (int)bc->block_count; i++) {
        if (strncmp(bc->block_labels[i], prefix, plen) == 0) {
            matches[n++] = bc->block_labels[i];
        }
    }
    if (n == 0) {
        sink->cb(sink->userdata, "no blocks");
        return 0;
    }
    qsort((void *)matches, (size_t)n, sizeof matches[0], cmp_str);
    {
        char *mut[FGIR_MAX_BLOCKS];
        for (i = 0; i < n; i++) {
            mut[i] = (char *)matches[i];
        }
        emit_lines_cb(sink->userdata, sink->cb, mut, n, "blocks");
    }
    return 0;
}

static int emit_natives_cb(DocSink *sink, const FlatOptionalNativeDecl *decls, int decl_count,
                           const char *query_in) {
    char query[256];
    char *lines[FLAT_NATIVE_COUNT];
    char mod[FGIR_NATIVE_MODULE_LEN];
    int total = 0;
    size_t i;
    size_t qlen;
    const char *dot;
    char list_tok[FGIR_NATIVE_MODULE_LEN];
    int list_module = 0;

    snprintf(query, sizeof query, "%s", query_in ? query_in : "");
    flat_native_trim(query);
    if (query[0] == '\0') {
        sink->cb(sink->userdata, "unknown module");
        return 0;
    }
    qlen = strlen(query);

    for (i = 0; i < (size_t)FLAT_NATIVE_COUNT; i++) {
        char canon[128];
        char alias_line[128];
        char alias[FGIR_NATIVE_ALIAS_LEN];
        char m[FGIR_NATIVE_MODULE_LEN];

        snprintf(canon, sizeof canon, "%s", flat_native_name((uint16_t)i));
        if (strcmp(canon, query) == 0) {
            sink->cb(sink->userdata, flat_native_doc_sig((uint16_t)i));
            return 0;
        }
        if (flat_native_module_for_id((uint16_t)i, m, sizeof m) != 0) {
            continue;
        }
        if (alias_for_module(m, decls, decl_count, alias, sizeof alias)) {
            format_line((uint16_t)i, alias, alias_line, sizeof alias_line);
            if (strcmp(alias_line, query) == 0) {
                sink->cb(sink->userdata, flat_native_doc_sig((uint16_t)i));
                return 0;
            }
        }
    }

    dot = strchr(query, '.');
    if (query_is_module_dot_shorthand(query)) {
        char mod_tok[FGIR_NATIVE_MODULE_LEN];
        size_t n = (size_t)(dot - query);
        memcpy(mod_tok, query, n);
        mod_tok[n] = '\0';
        if (resolve_query_module(mod_tok, decls, decl_count, mod, sizeof mod) == 0 &&
            module_is_visible(mod, decls, decl_count)) {
            bounded_strcpy(list_tok, sizeof list_tok, mod_tok);
            list_module = 1;
            dot = NULL;
        }
    }
    if (!dot) {
        if (!list_module) {
            if (resolve_query_module(query, decls, decl_count, mod, sizeof mod) != 0) {
                sink->cb(sink->userdata, "unknown module");
                return 0;
            }
            if (module_is_visible(mod, decls, decl_count)) {
                bounded_strcpy(list_tok, sizeof list_tok, query);
                list_module = 1;
            } else if (registry_has_module(mod) && !flat_native_is_core_module(mod)) {
                sink->cb(sink->userdata, "unknown module");
                return 0;
            } else {
                list_module = 0;
            }
        }
    } else if (!list_module) {
        list_tok[0] = '\0';
    }

    for (i = 0; i < (size_t)FLAT_NATIVE_COUNT; i++) {
        char canon[128];
        char line[128];
        char m[FGIR_NATIVE_MODULE_LEN];
        char *dup;

        snprintf(canon, sizeof canon, "%s", flat_native_name((uint16_t)i));
        if (flat_native_module_for_id((uint16_t)i, m, sizeof m) != 0) {
            continue;
        }
        if (!optional_visible(m, decls, decl_count)) {
            continue;
        }
        if (list_module) {
            if (resolve_query_module(list_tok, decls, decl_count, mod, sizeof mod) != 0 ||
                strcmp(m, mod) != 0) {
                continue;
            }
            format_line((uint16_t)i, list_tok, line, sizeof line);
        } else {
            if (strncmp(canon, query, qlen) != 0) {
                continue;
            }
            snprintf(line, sizeof line, "%s", canon);
        }
        {
            size_t ll = strlen(line) + 1;
            dup = malloc(ll);
            if (!dup) {
                continue;
            }
            memcpy(dup, line, ll);
        }
        lines[total++] = dup;
    }

    if (total == 0) {
        dot = strchr(query, '.');
        if (dot) {
            char tok[FGIR_NATIVE_MODULE_LEN];
            size_t n = (size_t)(dot - query);
            if (n >= sizeof tok) {
                sink->cb(sink->userdata, "no functions");
                return 0;
            }
            memcpy(tok, query, n);
            tok[n] = '\0';
            if (resolve_query_module(tok, decls, decl_count, mod, sizeof mod) != 0 ||
                !module_is_visible(mod, decls, decl_count)) {
                sink->cb(sink->userdata, "unknown module");
            } else {
                sink->cb(sink->userdata, "no functions");
            }
        } else {
            sink->cb(sink->userdata, "no functions");
        }
        return 0;
    }

    qsort(lines, (size_t)total, sizeof lines[0], cmp_str);
    emit_lines_cb(sink->userdata, sink->cb, lines, total, "functions");
    for (i = 0; i < (size_t)total; i++) {
        free(lines[i]);
    }
    return 0;
}

int flat_doc_emit_cb(void *userdata, flat_doc_line_cb cb, const FlatBytecode *bc,
                     const char *query_in) {
    char query[256];
    DocSink sink;

    if (!cb) {
        return -1;
    }
    sink.userdata = userdata;
    sink.cb = cb;
    snprintf(query, sizeof query, "%s", query_in ? query_in : "");
    flat_native_trim(query);
    if (query[0] == '#') {
        return emit_blocks_cb(&sink, bc, query);
    }
    return emit_natives_cb(&sink, bc ? bc->optional_natives : NULL,
                           bc ? bc->optional_native_count : 0, query);
}

int flat_doc_emit(FILE *out, const FlatBytecode *bc, const char *query_in) {
    if (!out) {
        return -1;
    }
    return flat_doc_emit_cb(out, file_line_cb, bc, query_in);
}
