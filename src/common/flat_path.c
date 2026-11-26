#include "flat_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#if defined(FLAT_WASM_BUILD)
#define FLAT_PATH_HOST_SEP '/'
#elif defined(_WIN32) || defined(_WIN64)
#define FLAT_PATH_HOST_SEP '\\'
#else
#define FLAT_PATH_HOST_SEP '/'
#endif

#define FLAT_PATH_MAX_SEGS 256

static atomic_uchar g_sep = ATOMIC_VAR_INIT(FLAT_PATH_HOST_SEP);

char flat_path_sep(void) {
    return (char)atomic_load_explicit(&g_sep, memory_order_acquire);
}
void flat_path_sep_reset(void) {
    atomic_store_explicit(&g_sep, FLAT_PATH_HOST_SEP, memory_order_release);
}
int flat_path_sep_set(char sep) {
    if (sep != '/' && sep != '\\') return -1;
    atomic_store_explicit(&g_sep, (unsigned char)sep, memory_order_release);
    return 0;
}
void flat_path_context_init(FlatPathContext *ctx) { if (ctx) ctx->sep = flat_path_sep(); }
int flat_path_context_set(FlatPathContext *ctx, char sep) {
    if (!ctx || (sep != '/' && sep != '\\')) return -1;
    ctx->sep = sep;
    return 0;
}
char flat_path_context_sep(const FlatPathContext *ctx) {
    return ctx && (ctx->sep == '/' || ctx->sep == '\\') ? ctx->sep :
           (ctx ? FLAT_PATH_HOST_SEP : flat_path_sep());
}
int flat_path_sep_set_str(const char *s) {
    if (!s || !s[0]) return -1;
    if ((s[0] == '/' || s[0] == '\\') && s[1] == '\0') return flat_path_sep_set(s[0]);
    if (s[0] == '\\' && s[1] == '\\' && s[2] == '\0') return flat_path_sep_set('\\');
    return -1;
}
int flat_path_is_sep(char c) { return c == '/' || c == '\\'; }

static void set_why(const char **why, const char *w) { if (why) *why = w; }

typedef struct {
    char drive[4];
    int absolute;
    char **segs;
    int n;
    int cap;
} PathParts;

static void parts_clear(PathParts *pp) {
    int i;
    if (!pp) return;
    for (i = 0; i < pp->n; ++i) free(pp->segs[i]);
    free(pp->segs);
    memset(pp, 0, sizeof *pp);
}

static int parts_reserve(PathParts *pp, int need) {
    int cap;
    char **next;
    if (need <= pp->cap) return 0;
    cap = pp->cap ? pp->cap : 8;
    while (cap < need) {
        if (cap >= FLAT_PATH_MAX_SEGS) return -1;
        cap *= 2;
        if (cap > FLAT_PATH_MAX_SEGS) cap = FLAT_PATH_MAX_SEGS;
    }
    next = (char **)realloc(pp->segs, (size_t)cap * sizeof *next);
    if (!next) return -1;
    pp->segs = next;
    pp->cap = cap;
    return 0;
}

static int push_seg(PathParts *pp, const char *s, size_t len) {
    char *copy;
    if (!pp || !s || pp->n >= FLAT_PATH_MAX_SEGS || len >= FLAT_PATH_MAX) return -1;
    if (parts_reserve(pp, pp->n + 1) != 0) return -1;
    copy = (char *)malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, s, len);
    copy[len] = '\0';
    pp->segs[pp->n++] = copy;
    return 0;
}

static int split_path(const char *p, PathParts *pp) {
    const char *s = p;
    size_t i = 0;
    if (!p || !pp) return -1;
    memset(pp, 0, sizeof *pp);
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':') {
        pp->drive[0] = s[0]; pp->drive[1] = ':'; pp->drive[2] = '\0';
        s += 2;
        if (flat_path_is_sep(s[0])) { pp->absolute = 1; s++; }
    } else if (flat_path_is_sep(s[0])) { pp->absolute = 1; s++; }
    while (s[i]) {
        size_t start = i;
        while (s[i] && !flat_path_is_sep(s[i])) i++;
        if (i > start && push_seg(pp, s + start, i - start) != 0) {
            parts_clear(pp);
            return -1;
        }
        if (flat_path_is_sep(s[i])) i++;
    }
    return 0;
}

static int normalize_parts(PathParts *pp) {
    int read_i, write_i = 0;
    for (read_i = 0; read_i < pp->n; ++read_i) {
        char *s = pp->segs[read_i];
        if (strcmp(s, ".") == 0) { free(s); continue; }
        if (strcmp(s, "..") == 0) {
            if (write_i > 0 && strcmp(pp->segs[write_i - 1], "..") != 0) {
                free(pp->segs[write_i - 1]);
                write_i--;
            } else if (!pp->absolute) {
                if (write_i >= FLAT_PATH_MAX_SEGS) return -1;
                pp->segs[write_i++] = s;
            } else free(s);
            continue;
        }
        pp->segs[write_i++] = s;
    }
    pp->n = write_i;
    return 0;
}

/* Emits normalized path using the currently selected path separator. */
static int build_out(const PathParts *pp, char sep, char *out, size_t out_sz) {
    size_t o = 0;
    int i;
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';
    if (pp->drive[0]) {
        if (o + 2 >= out_sz) return -1;
        out[o++] = pp->drive[0]; out[o++] = ':'; out[o] = '\0';
    }
    if (pp->absolute) {
        if (o + 1 >= out_sz) return -1;
        out[o++] = sep; out[o] = '\0';
    }
    for (i = 0; i < pp->n; ++i) {
        size_t sl = strlen(pp->segs[i]);
        if (i > 0) {
            if (o + 1 >= out_sz) return -1;
            out[o++] = sep; out[o] = '\0';
        }
        if (o + sl >= out_sz) return -1;
        memcpy(out + o, pp->segs[i], sl); o += sl; out[o] = '\0';
    }
    if (pp->n == 0 && !pp->absolute && !pp->drive[0]) {
        if (out_sz < 2) return -1;
        out[0] = '.'; out[1] = '\0';
    }
    return 0;
}

static int is_abs_local(const char *p) {
    if (!p || !p[0]) return 0;
    if (flat_path_is_sep(p[0])) return 1;
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
           p[1] == ':' && flat_path_is_sep(p[2]);
}

int flat_path_join_ctx(const FlatPathContext *ctx, const char *const *parts, int nparts, char *out, size_t out_sz, const char **why) {
    PathParts acc = {0};
    int i, rc = -1;
    set_why(why, NULL);
    if (!parts || nparts < 2 || !out) { set_why(why, "path.join.args"); return -1; }
    for (i = 0; i < nparts; ++i) {
        PathParts cur = {0};
        int j;
        if (!parts[i] || !parts[i][0]) { set_why(why, parts[i] ? "path.join.empty" : "path.join.args"); goto done; }
        if (split_path(parts[i], &cur) != 0) { set_why(why, "path.join.memory"); goto done; }
        if (cur.absolute || cur.drive[0]) {
            parts_clear(&acc);
            acc = cur;
            memset(&cur, 0, sizeof cur);
        } else {
            for (j = 0; j < cur.n; ++j) {
                if (push_seg(&acc, cur.segs[j], strlen(cur.segs[j])) != 0) {
                    parts_clear(&cur); set_why(why, "path.join.memory"); goto done;
                }
            }
        }
        parts_clear(&cur);
    }
    if (normalize_parts(&acc) != 0 || build_out(&acc, flat_path_context_sep(ctx), out, out_sz) != 0) {
        set_why(why, "path.join.memory"); goto done;
    }
    rc = 0;
done:
    parts_clear(&acc);
    return rc;
}

int flat_path_base_ctx(const FlatPathContext *ctx, const char *p, char *out, size_t out_sz, const char **why) {
    (void)ctx;
    PathParts pp = {0};
    int rc = -1;
    set_why(why, NULL);
    if (!p || !out || out_sz == 0) { set_why(why, "path.base.args"); return -1; }
    if (!p[0]) { set_why(why, "path.base.empty"); return -1; }
    if (split_path(p, &pp) != 0) { set_why(why, "path.base.memory"); goto done; }
    if (pp.n == 0) out[0] = '\0';
    else if (snprintf(out, out_sz, "%s", pp.segs[pp.n - 1]) >= (int)out_sz) { set_why(why, "path.base.memory"); goto done; }
    rc = 0;
done: parts_clear(&pp); return rc;
}

int flat_path_dir_ctx(const FlatPathContext *ctx, const char *p, char *out, size_t out_sz, const char **why) {
    PathParts pp = {0};
    int rc = -1;
    set_why(why, NULL);
    if (!p || !out || out_sz == 0) { set_why(why, "path.dir.args"); return -1; }
    if (!p[0]) { set_why(why, "path.dir.empty"); return -1; }
    if (split_path(p, &pp) != 0) { set_why(why, "path.dir.memory"); goto done; }
    if (pp.n > 0) { free(pp.segs[pp.n - 1]); pp.n--; }
    if (build_out(&pp, flat_path_context_sep(ctx), out, out_sz) != 0) { set_why(why, "path.dir.memory"); goto done; }
    rc = 0;
done: parts_clear(&pp); return rc;
}

int flat_path_ext_ctx(const FlatPathContext *ctx, const char *p, char *out, size_t out_sz, const char **why) {
    (void)ctx;
    char base[FLAT_PATH_MAX];
    const char *dot;
    set_why(why, NULL);
    if (!p || !out || out_sz == 0) { set_why(why, "path.ext.args"); return -1; }
    if (!p[0]) { set_why(why, "path.ext.empty"); return -1; }
    if (flat_path_base_ctx(ctx, p, base, sizeof base, why) != 0) return -1;
    if (!base[0] || base[0] == '.') { out[0] = '\0'; return 0; }
    dot = strrchr(base, '.');
    if (!dot || !dot[1] || dot == base) { out[0] = '\0'; return 0; }
    if (snprintf(out, out_sz, "%s", dot + 1) >= (int)out_sz) { set_why(why, "path.ext.memory"); return -1; }
    return 0;
}

int flat_path_normalize_ctx(const FlatPathContext *ctx, const char *p, char *out, size_t out_sz, const char **why) {
    PathParts pp = {0};
    int rc = -1;
    set_why(why, NULL);
    if (!p || !out || out_sz == 0) { set_why(why, "path.normalize.args"); return -1; }
    if (!p[0]) { set_why(why, "path.normalize.empty"); return -1; }
    if (split_path(p, &pp) != 0) { set_why(why, "path.normalize.memory"); goto done; }
    if (normalize_parts(&pp) != 0 || build_out(&pp, flat_path_context_sep(ctx), out, out_sz) != 0) {
        set_why(why, "path.normalize.memory"); goto done;
    }
    rc = 0;
done: parts_clear(&pp); return rc;
}

int flat_path_is_abs_ctx(const FlatPathContext *ctx, const char *p, int *out_abs, const char **why) {
    (void)ctx;
    set_why(why, NULL);
    if (!p || !out_abs) { set_why(why, "path.isAbs.args"); return -1; }
    if (!p[0]) { set_why(why, "path.isAbs.empty"); return -1; }
    *out_abs = is_abs_local(p) ? 1 : 0;
    return 0;
}


int flat_path_join(const char *const *p, int n, char *o, size_t z, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_join_ctx(&c, p, n, o, z, w);
}
int flat_path_base(const char *p, char *o, size_t z, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_base_ctx(&c, p, o, z, w);
}
int flat_path_dir(const char *p, char *o, size_t z, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_dir_ctx(&c, p, o, z, w);
}
int flat_path_ext(const char *p, char *o, size_t z, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_ext_ctx(&c, p, o, z, w);
}
int flat_path_normalize(const char *p, char *o, size_t z, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_normalize_ctx(&c, p, o, z, w);
}
int flat_path_is_abs(const char *p, int *a, const char **w) {
    FlatPathContext c; flat_path_context_init(&c); return flat_path_is_abs_ctx(&c, p, a, w);
}
