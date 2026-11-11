#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "flat_guard.h"
#include "flat_func.h"
#include "flat_link.h"
#include "fgir.h"
#include "flat_native_use.h"
#include "flat_schema.h"
#include "flat_port.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#endif

#define FLAT_LINK_MAX_MODULES 128
#define FLAT_LINK_MAX_USE_PER_FILE 32

typedef struct {
    char quoted[FLAT_PORT_PATH_MAX];
    char prefix[FGIR_MAX_LABEL];
    int line_no;
} FlatUseLine;

typedef struct {
    char canonical[FLAT_PORT_PATH_MAX];
    char prefix[FGIR_MAX_LABEL];
    char source_display[FLAT_PORT_PATH_MAX];
} FlatModEntry;

typedef struct {
    char quoted[FLAT_PORT_PATH_MAX];
    char ident[64];
    int line_no;
} FlatFmodUse;

static char *trim_local(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    {
        char *e = s + strlen(s) - 1;
        while (e > s && isspace((unsigned char)*e)) {
            *e-- = '\0';
        }
    }
    return s;
}

static int line_is_comment_or_blank(const char *p) {
    return *p == '\0' || (p[0] == '-' && p[1] == '-');
}

static int validate_as_prefix(const char *prefix, char *err, size_t err_size) {
    size_t i;
    if (!prefix || prefix[0] != '#') {
        snprintf(err, err_size, "use: as requires #Prefix");
        return -1;
    }
    if (prefix[1] == '\0') {
        snprintf(err, err_size, "use: empty as prefix");
        return -1;
    }
    if (!isalpha((unsigned char)prefix[1])) {
        snprintf(err, err_size, "use: as prefix must start with a letter after #");
        return -1;
    }
    for (i = 2; prefix[i]; i++) {
        unsigned char c = (unsigned char)prefix[i];
        if (isalnum(c) || c == '_' || c == '-' || c == ':') {
            continue;
        }
        snprintf(err, err_size, "use: invalid character in as prefix");
        return -1;
    }
    return 0;
}

static void path_dirname_of(const char *path, char *out, size_t out_sz) {
    char tmp[FLAT_PORT_PATH_MAX];
    const char *slash;
    size_t n;

    snprintf(tmp, sizeof tmp, "%s", path);
    slash = strrchr(tmp, '/');
#ifdef _WIN32
    {
        const char *bs = strrchr(tmp, '\\');
        if (!slash || (bs && bs > slash)) {
            slash = bs;
        }
    }
#endif
    if (!slash) {
        snprintf(out, out_sz, ".");
        return;
    }
    n = (size_t)(slash - tmp);
    if (n == 0) {
        snprintf(out, out_sz, "/");
        return;
    }
    snprintf(out, out_sz, "%.*s", (int)n, tmp);
}

static int path_join2(const char *dir, const char *rel, char *out, size_t out_sz) {
    if (!dir || !rel || !out || out_sz == 0) {
        return -1;
    }
    if (dir[0] == '\0') {
        snprintf(out, out_sz, "%s", rel);
        return 0;
    }
    {
        size_t dlen = strlen(dir);
        int need_sep = dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\';
        snprintf(out, out_sz, need_sep ? "%s/%s" : "%s%s", dir, rel);
    }
    return 0;
}

static int file_exists(const char *path) {
    return flat_port_path_exists(path);
}

static int canonicalize_existing(const char *path, char *canonical, size_t canon_sz) {
#ifdef _WIN32
    DWORD n = GetFullPathNameA(path, (DWORD)canon_sz, canonical, NULL);
    if (n == 0 || n >= canon_sz) {
        return -1;
    }
    return 0;
#else
    (void)path;
    snprintf(canonical, canon_sz, "%s", path);
    return 0;
#endif
}

static int resolve_use_path(const char *source_file, const char *quoted, char *canonical,
                            char *try_rel, size_t try_sz, char *err, size_t err_size) {
    char dir[FLAT_PORT_PATH_MAX];
    char candidate[FLAT_PORT_PATH_MAX];

    path_dirname_of(source_file, dir, sizeof dir);
    if (path_join2(dir, quoted, candidate, sizeof candidate) != 0) {
        snprintf(err, err_size, "use: path too long");
        return -1;
    }
    snprintf(try_rel, try_sz, "%s", candidate);
    if (!file_exists(candidate)) {
        snprintf(err, err_size, "cannot resolve \"%s\" relative to this file", quoted);
        return -1;
    }
    if (canonicalize_existing(candidate, canonical, FLAT_PORT_PATH_MAX) != 0) {
        snprintf(err, err_size, "cannot resolve \"%s\" relative to this file", quoted);
        return -1;
    }
    return 0;
}

static int starts_with_use(const char *s) {
    return strncmp(s, "use", 3) == 0 && (s[3] == '\0' || isspace((unsigned char)s[3]));
}

static int starts_with_use_fmod(const char *s) {
    const char *p;
    if (!starts_with_use(s)) {
        return 0;
    }
    p = s + 3;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return strncmp(p, "fmod", 4) == 0 &&
           (p[4] == '\0' || isspace((unsigned char)p[4]));
}

static int validate_fmod_ident(const char *ident, char *err, size_t err_size) {
    size_t i;
    if (!ident || !ident[0]) {
        snprintf(err, err_size, "use fmod: missing import name");
        return -1;
    }
    if (!(isalpha((unsigned char)ident[0]) || ident[0] == '_')) {
        snprintf(err, err_size, "use fmod: import name must start with letter or _");
        return -1;
    }
    for (i = 1; ident[i]; i++) {
        unsigned char c = (unsigned char)ident[i];
        if (!isalnum(c) && c != '_') {
            snprintf(err, err_size, "use fmod: invalid import name");
            return -1;
        }
    }
    return 0;
}

static int parse_use_fmod_line(const char *line, FlatFmodUse *out, char *err, size_t err_size) {
    char buf[FGIR_MAX_LINE];
    char *p;
    char *qstart;
    char *qend;

    snprintf(buf, sizeof buf, "%s", line);
    p = trim_local(buf);
    if (!starts_with_use_fmod(p)) {
        return -1;
    }
    p += 3;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "fmod", 4) != 0) {
        return -1;
    }
    p += 4;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        snprintf(err, err_size, "use fmod: expected quoted path");
        return -1;
    }
    qstart = p + 1;
    qend = strchr(qstart, '"');
    if (!qend) {
        snprintf(err, err_size, "use fmod: unterminated string");
        return -1;
    }
    *qend = '\0';
    snprintf(out->quoted, sizeof out->quoted, "%s", qstart);
    p = qend + 1;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "as", 2) != 0 || (p[2] != '\0' && !isspace((unsigned char)p[2]))) {
        snprintf(err, err_size, "use fmod: missing as name");
        return -1;
    }
    p += 2;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0' || isspace((unsigned char)*p)) {
        snprintf(err, err_size, "use fmod: missing import name");
        return -1;
    }
    {
        char *e = p;
        while (*e && !isspace((unsigned char)*e)) {
            e++;
        }
        if (*e) {
            *e = '\0';
        }
        snprintf(out->ident, sizeof out->ident, "%s", p);
    }
    return validate_fmod_ident(out->ident, err, err_size);
}

static int scan_header_fmod_uses(const char *path, const FlatLinkOptions *opts, FlatFmodUse *uses,
                                 int max_uses, int *out_count, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int in_block = 0;
    int n = 0;
    int in_header = 1;
    int schema_cur = -1;

    *out_count = 0;
    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *p;
        line_no++;
        if (schema_cur >= 0 && (line[0] == ' ' || line[0] == '\t')) {
            char fk[FGIR_OBJ_KEY_MAX + 1];
            char fs[FGIR_SCHEMA_TYPE_SPEC_MAX + 1];
            if (flat_schema_parse_field_line(line, fk, sizeof fk, fs, sizeof fs) == 0) {
                continue;
            }
        }
        p = trim_local(line);
        if (in_block) {
            if (strncmp(p, "-->", 3) == 0) {
                in_block = 0;
            }
            continue;
        }
        if (strncmp(p, "--<", 3) == 0) {
            in_block = 1;
            continue;
        }
        if (line_is_comment_or_blank(p)) {
            continue;
        }
        if (in_header && starts_with_use_fmod(p)) {
            if (n >= max_uses) {
                snprintf(err, err_size, "%s:%d: too many use fmod directives", path, line_no);
                fclose(f);
                return -1;
            }
            uses[n].line_no = line_no;
            if (parse_use_fmod_line(p, &uses[n], err, err_size) != 0) {
                char msg[256];
                snprintf(msg, sizeof msg, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, msg);
                fclose(f);
                return -1;
            }
            n++;
            continue;
        }
        if (*p == '#') {
            schema_cur = -1;
            in_header = 0;
            break;
        }
        if (in_header) {
            char sname[FLAT_SCHEMA_NAME_MAX + 1];
            if (flat_schema_parse_header_line(p, sname, sizeof sname) == 0) {
                schema_cur = 1;
                continue;
            }
            if (schema_cur >= 0) {
                schema_cur = -1;
            }
            if (strncmp(p, "def ", 4) == 0) {
                in_header = 0;
                break;
            }
            if (strncmp(p, "func:", 5) == 0 || strncmp(p, "mod(", 4) == 0 || strncmp(p, "export", 6) == 0) {
                continue;
            }
        }
    }
    if (in_block) {
        snprintf(err, err_size, "%s: unclosed block comment", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    {
        int i, j;
        for (i = 0; i < n; i++) {
            char canon_i[FLAT_PORT_PATH_MAX];
            char try_i[FLAT_PORT_PATH_MAX];
            char e2[256];
            if (resolve_use_path(path, uses[i].quoted, canon_i, try_i, sizeof try_i, e2, sizeof e2) != 0) {
                snprintf(err, err_size, "%s:%d: use fmod: %s", path, uses[i].line_no, e2);
                if (opts && opts->verbose) {
                    fprintf(stderr, "flc: verbose: tried absolute path %s\n", try_i);
                }
                return -1;
            }
            for (j = i + 1; j < n; j++) {
                char canon_j[FLAT_PORT_PATH_MAX];
                char try_j[FLAT_PORT_PATH_MAX];
                if (resolve_use_path(path, uses[j].quoted, canon_j, try_j, sizeof try_j, e2, sizeof e2) != 0) {
                    snprintf(err, err_size, "%s:%d: use fmod: %s", path, uses[j].line_no, e2);
                    return -1;
                }
                if (strcmp(canon_i, canon_j) == 0) {
                    snprintf(err, err_size, "%s:%d: duplicate use fmod for \"%s\"", path, uses[j].line_no,
                             uses[j].quoted);
                    return -1;
                }
                if (strcmp(uses[i].ident, uses[j].ident) == 0) {
                    snprintf(err, err_size, "%s:%d: duplicate fmod import name \"%s\"", path, uses[j].line_no,
                             uses[j].ident);
                    return -1;
                }
            }
        }
    }

    *out_count = n;
    return 0;
}

static int starts_with_use_native(const char *s) {
    const char *p;
    if (!starts_with_use(s)) {
        return 0;
    }
    p = s + 3;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return strncmp(p, "native", 6) == 0 &&
           (p[6] == '\0' || isspace((unsigned char)p[6]));
}

static int parse_use_line(const char *line, FlatUseLine *out, char *err, size_t err_size) {
    char buf[FGIR_MAX_LINE];
    char *p;
    char *qstart;
    char *qend;
    char *as_tok;

    snprintf(buf, sizeof buf, "%s", line);
    p = trim_local(buf);
    if (!starts_with_use(p)) {
        return -1;
    }
    p += 3;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        snprintf(err, err_size, "use: expected quoted path");
        return -1;
    }
    qstart = p + 1;
    qend = strchr(qstart, '"');
    if (!qend) {
        snprintf(err, err_size, "use: unterminated string");
        return -1;
    }
    *qend = '\0';
    snprintf(out->quoted, sizeof out->quoted, "%s", qstart);
    p = qend + 1;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "as", 2) != 0 || (p[2] != '\0' && !isspace((unsigned char)p[2]))) {
        snprintf(err, err_size, "use: missing as #Prefix");
        return -1;
    }
    p += 2;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '#') {
        snprintf(err, err_size, "use: missing as #Prefix");
        return -1;
    }
    as_tok = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    if (*p) {
        *p = '\0';
    }
    snprintf(out->prefix, sizeof out->prefix, "%s", as_tok);
    return validate_as_prefix(out->prefix, err, err_size);
}

static int scan_header_uses(const char *path, const FlatLinkOptions *opts, FlatUseLine *uses,
                            int max_uses, int *out_count, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int in_block = 0;
    int n = 0;
    int in_header = 1;
    int schema_cur = -1;

    *out_count = 0;
    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *p;
        line_no++;
        if (schema_cur >= 0 && (line[0] == ' ' || line[0] == '\t')) {
            char fk[FGIR_OBJ_KEY_MAX + 1];
            char fs[FGIR_SCHEMA_TYPE_SPEC_MAX + 1];
            if (flat_schema_parse_field_line(line, fk, sizeof fk, fs, sizeof fs) == 0) {
                continue;
            }
        }
        p = trim_local(line);
        if (in_block) {
            if (strncmp(p, "-->", 3) == 0) {
                in_block = 0;
            }
            continue;
        }
        if (strncmp(p, "--<", 3) == 0) {
            in_block = 1;
            continue;
        }
        if (line_is_comment_or_blank(p)) {
            continue;
        }
        if (in_header && starts_with_use(p)) {
            if (starts_with_use_native(p)) {
                continue;
            }
            if (starts_with_use_fmod(p)) {
                continue;
            }
            if (n >= max_uses) {
                snprintf(err, err_size, "%s:%d: too many use directives", path, line_no);
                fclose(f);
                return -1;
            }
            uses[n].line_no = line_no;
            if (parse_use_line(p, &uses[n], err, err_size) != 0) {
                char msg[256];
                snprintf(msg, sizeof msg, "%s", err);
                snprintf(err, err_size, "%s:%d: %s", path, line_no, msg);
                fclose(f);
                return -1;
            }
            n++;
            continue;
        }
        if (*p == '#') {
            schema_cur = -1;
            in_header = 0;
            break;
        }
        if (in_header) {
            char sname[FLAT_SCHEMA_NAME_MAX + 1];
            if (flat_schema_parse_header_line(p, sname, sizeof sname) == 0) {
                schema_cur = 1;
                continue;
            }
            if (schema_cur >= 0) {
                schema_cur = -1;
            }
            if (strncmp(p, "def ", 4) == 0) {
                in_header = 0;
                break;
            }
            if (strncmp(p, "func:", 5) == 0 || strncmp(p, "mod(", 4) == 0 || strncmp(p, "export", 6) == 0) {
                continue;
            }
            snprintf(err, err_size, "%s:%d: expected use directive or block", path, line_no);
            fclose(f);
            return -1;
        }
    }
    if (in_block) {
        snprintf(err, err_size, "%s: unclosed block comment", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    {
        int i, j;
        for (i = 0; i < n; i++) {
            char canon_i[FLAT_PORT_PATH_MAX];
            char try_i[FLAT_PORT_PATH_MAX];
            char e2[256];
            if (resolve_use_path(path, uses[i].quoted, canon_i, try_i, sizeof try_i, e2, sizeof e2) != 0) {
                snprintf(err, err_size, "%s:%d: use: %s", path, uses[i].line_no, e2);
                if (opts && opts->verbose) {
                    fprintf(stderr, "flc: verbose: tried absolute path %s\n", try_i);
                }
                return -1;
            }
            for (j = i + 1; j < n; j++) {
                char canon_j[FLAT_PORT_PATH_MAX];
                char try_j[FLAT_PORT_PATH_MAX];
                if (resolve_use_path(path, uses[j].quoted, canon_j, try_j, sizeof try_j, e2, sizeof e2) != 0) {
                    snprintf(err, err_size, "%s:%d: use: %s", path, uses[j].line_no, e2);
                    return -1;
                }
                if (strcmp(canon_i, canon_j) == 0) {
                    snprintf(err, err_size, "%s:%d: duplicate use for \"%s\"", path, uses[j].line_no,
                             uses[j].quoted);
                    return -1;
                }
            }
        }
    }

    *out_count = n;
    return 0;
}

static int find_mod(const FlatModEntry *mods, int mod_count, const char *canonical) {
    int i;
    for (i = 0; i < mod_count; i++) {
        if (strcmp(mods[i].canonical, canonical) == 0) {
            return i;
        }
    }
    return -1;
}

static int discover_modules(const char *entry_path, const FlatLinkOptions *opts, FlatModEntry *mods,
                            int *mod_count, char *err, size_t err_size) {
    char queue[FLAT_LINK_MAX_MODULES][FLAT_PORT_PATH_MAX];
    int qhead = 0;
    int qtail = 0;
    int count = 0;

    *mod_count = 0;
    snprintf(queue[qtail++], FLAT_PORT_PATH_MAX, "%s", entry_path);

    while (qhead < qtail) {
        char path[FLAT_PORT_PATH_MAX];
        FlatUseLine uses[FLAT_LINK_MAX_USE_PER_FILE];
        int nu = 0;
        int i;

        snprintf(path, sizeof path, "%s", queue[qhead++]);
        if (scan_header_uses(path, opts, uses, FLAT_LINK_MAX_USE_PER_FILE, &nu, err, err_size) != 0) {
            return -1;
        }
        for (i = 0; i < nu; i++) {
            char canonical[FLAT_PORT_PATH_MAX];
            char try_rel[FLAT_PORT_PATH_MAX];
            char e2[256];
            int mi;

            if (resolve_use_path(path, uses[i].quoted, canonical, try_rel, sizeof try_rel, e2, sizeof e2) != 0) {
                snprintf(err, err_size, "%s:%d: use: %s", path, uses[i].line_no, e2);
                if (opts && opts->verbose) {
                    fprintf(stderr, "flc: verbose: tried absolute path %s\n", try_rel);
                }
                return -1;
            }
            mi = find_mod(mods, count, canonical);
            if (mi >= 0) {
                if (strcmp(mods[mi].prefix, uses[i].prefix) != 0) {
                    snprintf(err, err_size, "%s:%d: module already imported as %s", path, uses[i].line_no,
                             mods[mi].prefix);
                    return -1;
                }
                continue;
            }
            if (count >= FLAT_LINK_MAX_MODULES) {
                snprintf(err, err_size, "too many imported modules");
                return -1;
            }
            snprintf(mods[count].canonical, sizeof mods[count].canonical, "%s", canonical);
            snprintf(mods[count].prefix, sizeof mods[count].prefix, "%s", uses[i].prefix);
            snprintf(mods[count].source_display, sizeof mods[count].source_display, "%s", uses[i].quoted);
            count++;

            if (qtail >= FLAT_LINK_MAX_MODULES) {
                snprintf(err, err_size, "use graph too large");
                return -1;
            }
            snprintf(queue[qtail++], FLAT_PORT_PATH_MAX, "%s", canonical);
        }
    }

    *mod_count = count;
    return 0;
}

static int is_handler_label(const char *lab) {
    return flat_label_is_or_handler(lab) || strncmp(lab, "#panic.", 7) == 0;
}

static int is_primary_block_label(const char *lab) {
    return lab[0] == '#' && !is_handler_label(lab);
}

static int label_in_list(const char *lab, const char labels[][FGIR_MAX_LABEL], int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(labels[i], lab) == 0) {
            return 1;
        }
    }
    return 0;
}

static void prefix_body_from_as(const char *as_prefix, char *body, size_t body_sz) {
    if (as_prefix[0] == '#') {
        snprintf(body, body_sz, "%s", as_prefix + 1);
    } else {
        snprintf(body, body_sz, "%s", as_prefix);
    }
}

static void globalize_primary(const char *local_lab, const char *prefix_body, char *out, size_t out_sz) {
    if (local_lab[0] != '#') {
        snprintf(out, out_sz, "%s", local_lab);
        return;
    }
    snprintf(out, out_sz, "#%s%s", prefix_body, local_lab + 1);
}

static int globalize_block_label(const char *local, const char *prefix_body,
                                 const char prim[][FGIR_MAX_LABEL], int n_prim, char *out,
                                 size_t out_sz) {
    if (is_primary_block_label(local)) {
        globalize_primary(local, prefix_body, out, out_sz);
        return 0;
    }
    if (flat_or_handler_body(local) != NULL) {
        char target[FGIR_MAX_LABEL];
        const char *rest = flat_or_handler_body(local);
        const char *dot = strrchr(rest, '.');
        if (!dot || dot == rest) {
            snprintf(out, out_sz, "%s", local);
            return 0;
        }
        snprintf(target, sizeof target, "%.*s", (int)(dot - rest), rest);
        if (label_in_list(target, prim, n_prim)) {
            char gtarget[FGIR_MAX_LABEL];
            const char *suffix = dot + 1;
            globalize_primary(target, prefix_body, gtarget, sizeof gtarget);
            snprintf(out, out_sz, FLAT_OR_HANDLER_PREFIX "%s.%s", gtarget, suffix);
            return 0;
        }
    } else if (strncmp(local, "#panic.", 7) == 0) {
        char target[FGIR_MAX_LABEL];
        const char *rest = local + 7;
        const char *dot = strrchr(rest, '.');
        if (!dot || dot == rest) {
            snprintf(out, out_sz, "%s", local);
            return 0;
        }
        snprintf(target, sizeof target, "%.*s", (int)(dot - rest), rest);
        if (label_in_list(target, prim, n_prim)) {
            char gtarget[FGIR_MAX_LABEL];
            const char *suffix = dot + 1;
            globalize_primary(target, prefix_body, gtarget, sizeof gtarget);
            snprintf(out, out_sz, "#panic.%s.%s", gtarget, suffix);
            return 0;
        }
    }
    snprintf(out, out_sz, "%s", local);
    return 0;
}

static void maybe_rewrite_ref(char *ref, size_t ref_sz, const char *prefix_body,
                              const char prim[][FGIR_MAX_LABEL], int n_prim) {
    char tmp[FGIR_MAX_LABEL];
    if (ref[0] == '\0') {
        return;
    }
    if (!label_in_list(ref, prim, n_prim)) {
        return;
    }
    globalize_primary(ref, prefix_body, tmp, sizeof tmp);
    snprintf(ref, ref_sz, "%s", tmp);
}

static int find_block_out(FgirProgram *out, const char *label) {
    int i;
    for (i = 0; i < out->block_count; i++) {
        if (strcmp(out->blocks[i].label, label) == 0) {
            return i;
        }
    }
    return -1;
}

static int append_block(FgirProgram *out, const FgirBlock *src, char *err, size_t err_size) {
    if (out->block_count >= FGIR_MAX_BLOCKS) {
        snprintf(err, err_size, "link: too many blocks");
        return -1;
    }
    if (find_block_out(out, src->label) >= 0) {
        snprintf(err, err_size, "link: duplicate block \"%s\"", src->label);
        return -1;
    }
    out->blocks[out->block_count] = *src;
    if (strcmp(src->label, "#main") == 0) {
        out->main_index = out->block_count;
    }
    out->block_count++;
    return 0;
}

static int merge_module(FgirProgram *out, const FgirProgram *mod, const char *as_prefix, char *err,
                        size_t err_size) {
    char prefix_body[FGIR_MAX_LABEL];
    char (*prim)[FGIR_MAX_LABEL] = NULL;
    int n_prim = 0;
    int i, j;
    int rc = 0;

    prefix_body_from_as(as_prefix, prefix_body, sizeof prefix_body);
    for (i = 0; i < mod->block_count; i++) {
        if (is_primary_block_label(mod->blocks[i].label)) {
            n_prim++;
        }
    }
    if (n_prim > 0) {
        prim = calloc((size_t)n_prim, sizeof(*prim));
        if (!prim) {
            snprintf(err, err_size, "out of memory");
            return -1;
        }
        n_prim = 0;
        for (i = 0; i < mod->block_count; i++) {
            if (is_primary_block_label(mod->blocks[i].label)) {
                snprintf(prim[n_prim], FGIR_MAX_LABEL, "%s", mod->blocks[i].label);
                n_prim++;
            }
        }
    }

    for (i = 0; i < mod->block_count; i++) {
        FgirBlock b;
        char glab[FGIR_MAX_LABEL];

        globalize_block_label(mod->blocks[i].label, prefix_body, prim, n_prim, glab, sizeof glab);
        memset(&b, 0, sizeof b);
        snprintf(b.label, sizeof b.label, "%s", glab);
        b.param_count = mod->blocks[i].param_count;
        for (j = 0; j < b.param_count; j++) {
            snprintf(b.params[j], sizeof b.params[j], "%s", mod->blocks[i].params[j]);
        }
        b.instr_count = mod->blocks[i].instr_count;
        for (j = 0; j < b.instr_count; j++) {
            b.instr[j] = mod->blocks[i].instr[j];
            maybe_rewrite_ref(b.instr[j].call_target, sizeof b.instr[j].call_target, prefix_body,
                              prim, n_prim);
            maybe_rewrite_ref(b.instr[j].foreach_block, sizeof b.instr[j].foreach_block, prefix_body,
                              prim, n_prim);
        }
        b.native_unit_index = mod->blocks[i].native_unit_index;
        if (append_block(out, &b, err, err_size) != 0) {
            rc = -1;
            break;
        }
    }
    free(prim);
    return rc;
}

static int merge_fmod_unit(FgirProgram *out, const FgirProgram *mod, char *err, size_t err_size) {
    int i;

    for (i = 0; i < mod->function_count; i++) {
        if (out->function_count >= FGIR_MAX_FUNCTIONS) {
            snprintf(err, err_size, "too many functions after fmod link");
            return -1;
        }
        out->functions[out->function_count++] = mod->functions[i];
    }
    for (i = 0; i < mod->block_count; i++) {
        FgirBlock b = mod->blocks[i];
        if (append_block(out, &b, err, err_size) != 0) {
            return -1;
        }
    }
    return 0;
}

static int merge_entry(FgirProgram *out, const FgirProgram *entry, char *err, size_t err_size) {
    int i;
    for (i = 0; i < entry->function_count; i++) {
        if (out->function_count >= FGIR_MAX_FUNCTIONS) {
            snprintf(err, err_size, "too many functions after link");
            return -1;
        }
        out->functions[out->function_count++] = entry->functions[i];
    }
    for (i = 0; i < entry->block_count; i++) {
        FgirBlock b = entry->blocks[i];
        if (append_block(out, &b, err, err_size) != 0) {
            return -1;
        }
    }
    for (i = 0; i < entry->schema_count; i++) {
        if (out->schema_count >= FGIR_SCHEMA_MAX) {
            snprintf(err, err_size, "too many schemas after link");
            return -1;
        }
        out->schemas[out->schema_count++] = entry->schemas[i];
    }
    return 0;
}

static int prepare_unit_decls(FgirProgram *mod, const char *path, int *unit_index_out, char *err,
                              size_t err_size) {
    FlatOptionalNativeDecl decls[FGIR_MAX_OPTIONAL_NATIVES];
    int decl_count = 0;
    int i;

    if (flat_native_use_scan_file(path, decls, FGIR_MAX_OPTIONAL_NATIVES, &decl_count, err,
                                  err_size) != 0) {
        return -1;
    }
    mod->native_unit_count = 1;
    mod->native_units[0].decl_count = decl_count;
    if (decl_count > 0) {
        memcpy(mod->native_units[0].decls, decls,
               (size_t)decl_count * sizeof(FlatOptionalNativeDecl));
    }
    for (i = 0; i < mod->block_count; i++) {
        mod->blocks[i].native_unit_index = 0;
    }
    *unit_index_out = 0;
    return 0;
}

static int attach_unit_to_out(FgirProgram *out, const FgirProgram *mod, int *out_ui, char *err,
                              size_t err_size) {
    const FgirNativeUnit *u = &mod->native_units[0];
    return flat_native_use_add_unit(out, u->decls, u->decl_count, out_ui, err, err_size);
}

int flat_link_program(const char *entry_path, const FlatLinkOptions *opts, FgirProgram *out,
                      FlatLinkReport *report, char *err, size_t err_size) {
    FlatModEntry mods[FLAT_LINK_MAX_MODULES];
    int mod_count = 0;
    int fmod_linked = 0;
    FgirProgram *entry_prog = NULL;
    FgirProgram *mod = NULL;
    int i;

    if (!entry_path || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->main_index = -1;

    if (discover_modules(entry_path, opts, mods, &mod_count, err, err_size) != 0) {
        return -1;
    }

    {
        FlatFmodUse fmods[32];
        int fmod_count = 0;
        int fi;

        if (scan_header_fmod_uses(entry_path, opts, fmods, 32, &fmod_count, err, err_size) != 0) {
            return -1;
        }
        fmod_linked = fmod_count;
        mod = calloc(1, sizeof(FgirProgram));
        if (!mod) {
            snprintf(err, err_size, "out of memory");
            return -1;
        }
        for (fi = 0; fi < fmod_count; fi++) {
            char canonical[FLAT_PORT_PATH_MAX];
            char try_rel[FLAT_PORT_PATH_MAX];
            char e2[256];
            int local_ui;
            int out_ui;

            memset(mod, 0, sizeof *mod);
            mod->main_index = -1;
            if (resolve_use_path(entry_path, fmods[fi].quoted, canonical, try_rel, sizeof try_rel, e2,
                                 sizeof e2) != 0) {
                snprintf(err, err_size, "%s:%d: use fmod: %s", entry_path, fmods[fi].line_no, e2);
                free(mod);
                return -1;
            }
            if (flat_parse_unit(canonical, 0, mod, err, err_size) != 0) {
                free(mod);
                return -1;
            }
            if (!mod->mod_id_set) {
                snprintf(err, err_size, "%s: mod('id') missing in fmod unit", canonical);
                free(mod);
                return -1;
            }
            if (flat_func_prepare_fmod_unit(mod, fmods[fi].ident, err, err_size) != 0) {
                char detail[512];
                snprintf(detail, sizeof detail, "%s", err);
                snprintf(err, err_size, "%s: %s", canonical, detail);
                free(mod);
                return -1;
            }
            if (prepare_unit_decls(mod, canonical, &local_ui, err, err_size) != 0) {
                free(mod);
                return -1;
            }
            if (attach_unit_to_out(out, mod, &out_ui, err, err_size) != 0) {
                free(mod);
                return -1;
            }
            for (local_ui = 0; local_ui < mod->block_count; local_ui++) {
                mod->blocks[local_ui].native_unit_index = out_ui;
            }
            if (merge_fmod_unit(out, mod, err, err_size) != 0) {
                free(mod);
                return -1;
            }
        }
        free(mod);
        mod = NULL;
    }

    mod = calloc(1, sizeof(FgirProgram));
    if (!mod) {
        snprintf(err, err_size, "out of memory");
        return -1;
    }

    for (i = 0; i < mod_count; i++) {
        int local_ui;
        int out_ui;

        memset(mod, 0, sizeof *mod);
        mod->main_index = -1;
        if (flat_parse_unit(mods[i].canonical, 0, mod, err, err_size) != 0) {
            free(mod);
            return -1;
        }
        if (prepare_unit_decls(mod, mods[i].canonical, &local_ui, err, err_size) != 0) {
            free(mod);
            return -1;
        }
        if (attach_unit_to_out(out, mod, &out_ui, err, err_size) != 0) {
            free(mod);
            return -1;
        }
        for (local_ui = 0; local_ui < mod->block_count; local_ui++) {
            mod->blocks[local_ui].native_unit_index = out_ui;
        }
        if (merge_module(out, mod, mods[i].prefix, err, err_size) != 0) {
            free(mod);
            return -1;
        }
    }
    free(mod);

    entry_prog = calloc(1, sizeof(FgirProgram));
    if (!entry_prog) {
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    entry_prog->main_index = -1;
    if (flat_parse_unit(entry_path, 1, entry_prog, err, err_size) != 0) {
        free(entry_prog);
        return -1;
    }
    {
        int local_ui;
        int out_ui;

        if (prepare_unit_decls(entry_prog, entry_path, &local_ui, err, err_size) != 0) {
            free(entry_prog);
            return -1;
        }
        if (attach_unit_to_out(out, entry_prog, &out_ui, err, err_size) != 0) {
            free(entry_prog);
            return -1;
        }
        for (local_ui = 0; local_ui < entry_prog->block_count; local_ui++) {
            entry_prog->blocks[local_ui].native_unit_index = out_ui;
        }
    }
    if (merge_entry(out, entry_prog, err, err_size) != 0) {
        free(entry_prog);
        return -1;
    }
    free(entry_prog);

    if (flat_native_use_build_fnat_union(out) != 0) {
        snprintf(err, err_size, "too many optional native modules in program");
        return -1;
    }
    flat_func_reclassify_program(out);
    if (flat_native_use_validate_program(out, err, err_size) != 0) {
        return -1;
    }
    if (flat_func_lower_program(out, err, err_size) != 0) {
        return -1;
    }
    if (flat_func_validate_program(out, err, err_size) != 0) {
        return -1;
    }

    if (out->main_index < 0) {
        snprintf(err, err_size, "%s: missing #main", entry_path);
        return -1;
    }

    if (report) {
        memset(report, 0, sizeof *report);
        report->units = out->native_unit_count;
        report->fmod = fmod_linked;
    }

    return 0;
}
