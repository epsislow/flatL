#include "flat_path.h"

#include <stdio.h>
#include <string.h>

#if defined(FLAT_WASM_BUILD)
#define FLAT_PATH_HOST_SEP '/'
#elif defined(_WIN32) || defined(_WIN64)
#define FLAT_PATH_HOST_SEP '\\'
#else
#define FLAT_PATH_HOST_SEP '/'
#endif

#define FLAT_PATH_MAX_SEGS 256

static char g_sep = FLAT_PATH_HOST_SEP;

char flat_path_sep(void) {
    return g_sep;
}

void flat_path_sep_reset(void) {
    g_sep = FLAT_PATH_HOST_SEP;
}

int flat_path_sep_set(char sep) {
    if (sep != '/' && sep != '\\') return -1;
    g_sep = sep;
    return 0;
}

int flat_path_sep_set_str(const char *s) {
    if (!s || !s[0]) return -1;
    if ((s[0] == '/' || s[0] == '\\') && s[1] == '\0') return flat_path_sep_set(s[0]);
    if (s[0] == '\\' && s[1] == '\\' && s[2] == '\0') return flat_path_sep_set('\\');
    return -1;
}

int flat_path_is_sep(char c) {
    return c == '/' || c == '\\';
}

static void set_why(const char **why, const char *w) {
    if (why) *why = w;
}

typedef struct {
    char drive[4]; /* "C:" or empty */
    int absolute;
    char segs[FLAT_PATH_MAX_SEGS][FLAT_PATH_MAX];
    int n;
} PathParts;

static int push_seg(PathParts *pp, const char *s, size_t len) {
    if (pp->n >= FLAT_PATH_MAX_SEGS || len >= FLAT_PATH_MAX) return -1;
    memcpy(pp->segs[pp->n], s, len);
    pp->segs[pp->n][len] = '\0';
    pp->n++;
    return 0;
}

static int split_path(const char *p, PathParts *pp) {
    const char *s = p;
    size_t i = 0;
    memset(pp, 0, sizeof *pp);
    if (!p) return -1;
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':') {
        pp->drive[0] = s[0];
        pp->drive[1] = ':';
        pp->drive[2] = '\0';
        s += 2;
        if (flat_path_is_sep(s[0])) {
            pp->absolute = 1;
            s++;
        }
    } else if (flat_path_is_sep(s[0])) {
        pp->absolute = 1;
        s++;
    }
    while (s[i]) {
        size_t start = i;
        while (s[i] && !flat_path_is_sep(s[i])) i++;
        if (i > start) {
            if (push_seg(pp, s + start, i - start) != 0) return -1;
        }
        if (flat_path_is_sep(s[i])) i++;
    }
    return 0;
}

static int normalize_stack(PathParts *pp) {
    char stack[FLAT_PATH_MAX_SEGS][FLAT_PATH_MAX];
    int sn = 0;
    int i;
    for (i = 0; i < pp->n; i++) {
        const char *s = pp->segs[i];
        if (strcmp(s, ".") == 0) continue;
        if (strcmp(s, "..") == 0) {
            if (sn > 0 && strcmp(stack[sn - 1], "..") != 0) {
                sn--;
            } else if (!pp->absolute) {
                if (sn >= FLAT_PATH_MAX_SEGS) return -1;
                snprintf(stack[sn++], FLAT_PATH_MAX, "..");
            }
            continue;
        }
        if (sn >= FLAT_PATH_MAX_SEGS) return -1;
        snprintf(stack[sn++], FLAT_PATH_MAX, "%s", s);
    }
    for (i = 0; i < sn; i++) snprintf(pp->segs[i], FLAT_PATH_MAX, "%s", stack[i]);
    pp->n = sn;
    return 0;
}

/*
 * Emit:
 *  - absolute, no drive: "/a/b" or "\a\b"
 *  - absolute + drive: "C:\a\b"
 *  - relative + drive: "C:a\b" (no sep after drive — Windows relative-to-drive)
 *  - relative: "a/b" or "."
 *  - root only: "/" or "C:\"
 */
static int build_out(const PathParts *pp, char *out, size_t out_sz) {
    size_t o = 0;
    int i;
    char sep = flat_path_sep();
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';

    if (pp->drive[0]) {
        if (o + 2 >= out_sz) return -1;
        out[o++] = pp->drive[0];
        out[o++] = ':';
        out[o] = '\0';
    }
    if (pp->absolute) {
        if (o + 1 >= out_sz) return -1;
        out[o++] = sep;
        out[o] = '\0';
    }
    for (i = 0; i < pp->n; i++) {
        size_t sl = strlen(pp->segs[i]);
        if (i > 0 || (pp->absolute && i == 0)) {
            if (i > 0) {
                if (o + 1 >= out_sz) return -1;
                out[o++] = sep;
                out[o] = '\0';
            }
            /* i==0 && absolute: root sep already written */
        }
        if (o + sl >= out_sz) return -1;
        memcpy(out + o, pp->segs[i], sl);
        o += sl;
        out[o] = '\0';
    }
    if (pp->n == 0 && !pp->absolute && !pp->drive[0]) {
        if (out_sz < 2) return -1;
        out[0] = '.';
        out[1] = '\0';
    }
    return 0;
}

static int is_abs_local(const char *p) {
    if (!p || !p[0]) return 0;
    if (flat_path_is_sep(p[0])) return 1;
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' &&
        flat_path_is_sep(p[2])) {
        return 1;
    }
    return 0;
}

int flat_path_join(const char *const *parts, int nparts, char *out, size_t out_sz, const char **why) {
    PathParts acc;
    int i;
    set_why(why, NULL);
    if (!parts || nparts < 2 || !out) {
        set_why(why, "path.join.args");
        return -1;
    }
    memset(&acc, 0, sizeof acc);
    for (i = 0; i < nparts; i++) {
        PathParts cur;
        if (!parts[i] || parts[i][0] == '\0') {
            set_why(why, parts[i] && !parts[i][0] ? "path.join.empty" : "path.join.args");
            return -1;
        }
        if (split_path(parts[i], &cur) != 0) {
            set_why(why, "path.join.memory");
            return -1;
        }
        if (cur.absolute || cur.drive[0]) {
            acc = cur;
        } else {
            int j;
            for (j = 0; j < cur.n; j++) {
                if (push_seg(&acc, cur.segs[j], strlen(cur.segs[j])) != 0) {
                    set_why(why, "path.join.memory");
                    return -1;
                }
            }
        }
    }
    if (normalize_stack(&acc) != 0 || build_out(&acc, out, out_sz) != 0) {
        set_why(why, "path.join.memory");
        return -1;
    }
    return 0;
}

int flat_path_base(const char *p, char *out, size_t out_sz, const char **why) {
    PathParts pp;
    set_why(why, NULL);
    if (!p || !out) {
        set_why(why, "path.base.args");
        return -1;
    }
    if (!p[0]) {
        set_why(why, "path.base.empty");
        return -1;
    }
    if (split_path(p, &pp) != 0) {
        set_why(why, "path.base.memory");
        return -1;
    }
    if (pp.n == 0) {
        snprintf(out, out_sz, "%s", "");
        return 0;
    }
    snprintf(out, out_sz, "%s", pp.segs[pp.n - 1]);
    return 0;
}

int flat_path_dir(const char *p, char *out, size_t out_sz, const char **why) {
    PathParts pp;
    set_why(why, NULL);
    if (!p || !out) {
        set_why(why, "path.dir.args");
        return -1;
    }
    if (!p[0]) {
        set_why(why, "path.dir.empty");
        return -1;
    }
    if (split_path(p, &pp) != 0) {
        set_why(why, "path.dir.memory");
        return -1;
    }
    if (pp.n > 0) pp.n--;
    if (build_out(&pp, out, out_sz) != 0) {
        set_why(why, "path.dir.memory");
        return -1;
    }
    return 0;
}

int flat_path_ext(const char *p, char *out, size_t out_sz, const char **why) {
    char base[FLAT_PATH_MAX];
    const char *dot;
    set_why(why, NULL);
    if (!p || !out) {
        set_why(why, "path.ext.args");
        return -1;
    }
    if (!p[0]) {
        set_why(why, "path.ext.empty");
        return -1;
    }
    if (flat_path_base(p, base, sizeof base, why) != 0) return -1;
    if (!base[0] || base[0] == '.') {
        snprintf(out, out_sz, "%s", "");
        return 0;
    }
    dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) {
        snprintf(out, out_sz, "%s", "");
        return 0;
    }
    snprintf(out, out_sz, "%s", dot + 1);
    return 0;
}

int flat_path_normalize(const char *p, char *out, size_t out_sz, const char **why) {
    PathParts pp;
    set_why(why, NULL);
    if (!p || !out) {
        set_why(why, "path.normalize.args");
        return -1;
    }
    if (!p[0]) {
        set_why(why, "path.normalize.empty");
        return -1;
    }
    if (split_path(p, &pp) != 0 || normalize_stack(&pp) != 0 || build_out(&pp, out, out_sz) != 0) {
        set_why(why, "path.normalize.memory");
        return -1;
    }
    return 0;
}

int flat_path_is_abs(const char *p, int *out_abs, const char **why) {
    set_why(why, NULL);
    if (!p || !out_abs) {
        set_why(why, "path.isAbs.args");
        return -1;
    }
    if (!p[0]) {
        set_why(why, "path.isAbs.empty");
        return -1;
    }
    *out_abs = is_abs_local(p) ? 1 : 0;
    return 0;
}
