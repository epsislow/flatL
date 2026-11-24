#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_http_runtime.h"
#include "flat_enc.h"
#include "flat_value.h"
#include "flat_number.h"
#include "flat_file.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

static int http_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}
static int http_ok(FlatNativeCtx *ctx) { if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK; return 0; }
static char *dup_n(const char *s, size_t n) { char *p = malloc(n + 1); if (!p) return NULL; memcpy(p,s,n); p[n]='\0'; return p; }
static int make_string_value(FlatNativeCtx *ctx, const char *data, size_t len, FlatValue *out) {
    uint32_t ref;
    if (!ctx || !out || (!data && len)) return -1;
    if (len <= FLAT_INLINE_STR_MAX) return flat_value_set_str_inline(out, data ? data : "", len);
    if (flat_pool_alloc_copy(ctx->pool, data, len, &ref) != 0) {
        if (ctx) {
            snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "pool.full");
        }
        return -1;
    }
    return flat_value_set_str_ref(out, ref);
}
static int value_string(FlatNativeCtx *ctx, const FlatValue *v, char **out, size_t *len) {
    const uint8_t *p; size_t n;
    if (!ctx || !v || !out || !len) return -1;
    if (v->tag == VAL_STR_INLINE) { n=v->u.ins.len; p=(const uint8_t *)v->u.ins.bytes; }
    else if (v->tag == VAL_STR_REF) { p=flat_pool_read(ctx->pool,v->u.ref_id,&n); if (!p) return -1; }
    else return -1;
    *out=dup_n((const char *)p,n); if (!*out) return -1; *len=n; return 0;
}
static int parse_value(FlatNativeCtx *ctx,const char *expr,FlatValue *v) { return flat_native_parse_rhs(ctx,expr,v); }
static void release_args(FlatNativeCtx *ctx, FlatValue *v, int n) { int i; for (i = 0; i < n; ++i) flat_native_parse_rhs_release(ctx, &v[i]); }
static int split_args(char *s, char **a, char **b, char **c, char **d) {
    int depth = 0;
    char q = 0;
    char *p;
    int n = 0;
    int maxn = d ? 4 : 3;
    char **dst[4];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    for (p = s; *p; p++) {
        if (q) {
            if (*p == q && (p == s || p[-1] != '\\')) q = 0;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            q = *p;
            continue;
        }
        if (*p == '[' || *p == '{' || *p == '(') depth++;
        else if ((*p == ']' || *p == '}' || *p == ')') && depth) depth--;
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            if (n < maxn) *dst[n] = flat_native_trim(s);
            n++;
            s = p + 1;
        }
    }
    if (n < maxn) *dst[n] = flat_native_trim(s);
    return n + 1;
}
static int get_args(FlatNativeCtx *ctx, FlatValue *v, int want) {
    char buf[4096], *a = NULL, *b = NULL, *c = NULL, *d = NULL;
    int n;
    if (!ctx || !ctx->inner) return -1;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    n = split_args(buf, &a, &b, &c, want > 3 ? &d : NULL);
    if (n != want) return -1;
    if (parse_value(ctx, a, &v[0]) != 0) return -1;
    if (want > 1 && parse_value(ctx, b, &v[1]) != 0) {
        release_args(ctx, v, 1);
        return -1;
    }
    if (want > 2 && parse_value(ctx, c, &v[2]) != 0) {
        release_args(ctx, v, 2);
        return -1;
    }
    if (want > 3 && parse_value(ctx, d, &v[3]) != 0) {
        release_args(ctx, v, 3);
        return -1;
    }
    return 0;
}
static int num_arg(const FlatValue *v,uint32_t *out) { double d; if(!v||v->tag!=VAL_NUMBER)return -1; d=flat_number_as_f64(v); if(d<1||d>FLAT_HTTP_MAX_REQUESTS||d!=(double)(uint32_t)d)return -1; *out=(uint32_t)d; return 0; }
int flat_http_request_live(const FlatHttpRuntime *rt,uint32_t id){return rt&&id>=1&&id<=FLAT_HTTP_MAX_REQUESTS&&rt->requests[id].live;}
static FlatHttpRequestSlot *resolve(FlatNativeCtx *ctx,const char *fn,uint32_t *id){FlatValue v;if(!ctx||!ctx->http||get_args(ctx,&v,1)!=0||num_arg(&v,id)!=0||!flat_http_request_live(ctx->http,*id)){http_fail(ctx,fn);return NULL;}return &ctx->http->requests[*id];}
static void http_parts_clear(FlatHttpRequestSlot *s);
void flat_http_runtime_destroy(FlatHttpRuntime *rt){
    uint32_t i;
    if (!rt) return;
    for (i = 1; i <= FLAT_HTTP_MAX_REQUESTS; i++) {
        FlatHttpRequestSlot *s = &rt->requests[i];
        size_t j;
        free(s->method);
        free(s->url);
        free(s->body);
        for (j = 0; j < s->header_count; j++) free(s->headers[j]);
        free(s->headers);
        flat_http_response_free(&s->response);
        http_parts_clear(s);
        memset(s, 0, sizeof *s);
    }
}
static int append_header(FlatHttpRequestSlot *s,const char *name,const char *value){size_t n=strlen(name),v=strlen(value),i;char *line;char **next;if(!n||n>FLAT_OBJ_KEY_MAX||strchr(name,':')||strchr(name,'\r')||strchr(name,'\n')||strchr(value,'\r')||strchr(value,'\n'))return -1;line=malloc(n+v+3);if(!line)return -1;for(i=0;i<n;i++)line[i]=(char)tolower((unsigned char)name[i]);line[n]=':';line[n+1]=' ';memcpy(line+n+2,value,v+1);next=realloc(s->headers,(s->header_count+1)*sizeof *next);if(!next){free(line);return -1;}s->headers=next;s->headers[s->header_count++]=line;return 0;}
int flat_native_http_create(FlatNativeCtx *ctx){
    FlatValue a[2];
    char *m = NULL, *u = NULL;
    size_t ml, ul;
    uint32_t i;
    if (!ctx || !ctx->http || !ctx->out || get_args(ctx, a, 2) != 0 ||
        value_string(ctx, &a[0], &m, &ml) != 0 || value_string(ctx, &a[1], &u, &ul) != 0) {
        free(m);
        free(u);
        return http_fail(ctx, "http.create.args");
    }
    for (i = 1; i <= FLAT_HTTP_MAX_REQUESTS; i++)
        if (!ctx->http->requests[i].live) {
            FlatHttpRequestSlot *s = &ctx->http->requests[i];
            memset(s, 0, sizeof *s);
            s->live = 1;
            s->method = m;
            s->url = u;
            s->timeout_ms = FLAT_HTTP_DEFAULT_TIMEOUT_MS;
            s->max_body_bytes = FLAT_HTTP_DEFAULT_MAX_BODY;
            s->max_redirects = FLAT_HTTP_DEFAULT_MAX_REDIRECTS;
            s->parts_max = FLAT_HTTP_DEFAULT_PARTS_MAX;
            return http_ok(ctx) + flat_value_set_number(ctx->out, (double)i);
        }
    free(m);
    free(u);
    return http_fail(ctx, "http.create.limit");
}
static int set_or_replace_header(FlatHttpRequestSlot *s, const char *name, const char *value) {
    size_t i, nname;
    char **kept;
    size_t kept_n = 0;
    if (!s || !name || !value) return -1;
    nname = strlen(name);
    kept = (char **)malloc((s->header_count + 1) * sizeof *kept);
    if (!kept) return -1;
    for (i = 0; i < s->header_count; ++i) {
        const char *h = s->headers[i];
        size_t j = 0;
        int same = 1;
        if (!h) continue;
        while (j < nname && h[j] && h[j] != ':') {
            char a = (char)tolower((unsigned char)h[j]);
            char b = (char)tolower((unsigned char)name[j]);
            if (a != b) { same = 0; break; }
            j++;
        }
        if (same && j == nname && h[j] == ':') {
            free(s->headers[i]);
            continue;
        }
        kept[kept_n++] = s->headers[i];
    }
    free(s->headers);
    s->headers = kept;
    s->header_count = kept_n;
    return append_header(s, name, value);
}

static void http_part_free(FlatHttpPart *p) {
    if (!p) return;
    free(p->name);
    free(p->data);
    free(p->filename);
    memset(p, 0, sizeof *p);
}

static void http_parts_invalidate_body(FlatHttpRequestSlot *s) {
    if (!s) return;
    free(s->body);
    s->body = NULL;
    s->body_len = 0;
    s->parts_dirty = 1;
    s->sent = 0;
}

static void http_parts_clear(FlatHttpRequestSlot *s) {
    size_t i;
    if (!s) return;
    for (i = 0; i < s->part_count; ++i) http_part_free(&s->parts[i]);
    free(s->parts);
    s->parts = NULL;
    s->part_count = 0;
    free(s->multipart_boundary);
    s->multipart_boundary = NULL;
    s->multipart_mode = 0;
    s->parts_dirty = 0;
}

static void http_parts_reset_keep_mode(FlatHttpRequestSlot *s) {
    size_t i;
    if (!s) return;
    for (i = 0; i < s->part_count; ++i) http_part_free(&s->parts[i]);
    free(s->parts);
    s->parts = NULL;
    s->part_count = 0;
    free(s->multipart_boundary);
    s->multipart_boundary = NULL;
    s->multipart_mode = 1;
    http_parts_invalidate_body(s);
}

static int http_ensure_boundary(FlatHttpRequestSlot *s) {
    char buf[48];
    static unsigned long seq;
    if (!s) return -1;
    if (s->multipart_boundary) return 0;
    seq++;
    snprintf(buf, sizeof buf, "----flatL%08lx%08lx", (unsigned long)(uintptr_t)s, seq);
    s->multipart_boundary = dup_n(buf, strlen(buf));
    return s->multipart_boundary ? 0 : -1;
}

static int http_append_bytes(char **buf, size_t *len, size_t *cap, const void *data, size_t n) {
    char *nb;
    size_t ncap;
    if (!buf || !len || !cap) return -1;
    if (n == 0) return 0;
    ncap = *cap ? *cap : 64;
    while (*len + n + 1 > ncap) ncap *= 2;
    if (ncap != *cap) {
        nb = (char *)realloc(*buf, ncap);
        if (!nb) return -1;
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static int http_append_cstr(char **buf, size_t *len, size_t *cap, const char *s) {
    return http_append_bytes(buf, len, cap, s, s ? strlen(s) : 0);
}

/* Quote a form-data token; escape \ and " */
static int http_append_quoted(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t i;
    if (http_append_cstr(buf, len, cap, "\"") != 0) return -1;
    for (i = 0; s && s[i]; ++i) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            if (http_append_cstr(buf, len, cap, "\\") != 0) return -1;
        }
        if (http_append_bytes(buf, len, cap, &c, 1) != 0) return -1;
    }
    return http_append_cstr(buf, len, cap, "\"");
}

static int http_parts_load_slot(FlatHttpRequestSlot *s) {
    char *body = NULL;
    size_t body_len = 0, body_cap = 0;
    size_t i;
    char ct[128];
    if (!s) return -1;
    if (http_ensure_boundary(s) != 0) return -1;
    for (i = 0; i < s->part_count; ++i) {
        FlatHttpPart *p = &s->parts[i];
        if (http_append_cstr(&body, &body_len, &body_cap, "--") != 0) goto fail;
        if (http_append_cstr(&body, &body_len, &body_cap, s->multipart_boundary) != 0) goto fail;
        if (http_append_cstr(&body, &body_len, &body_cap, "\r\nContent-Disposition: form-data; name=") != 0)
            goto fail;
        if (http_append_quoted(&body, &body_len, &body_cap, p->name ? p->name : "") != 0) goto fail;
        if (p->kind == FLAT_HTTP_PART_FILE) {
            if (http_append_cstr(&body, &body_len, &body_cap, "; filename=") != 0) goto fail;
            if (http_append_quoted(&body, &body_len, &body_cap, p->filename ? p->filename : "") != 0)
                goto fail;
            if (http_append_cstr(&body, &body_len, &body_cap,
                                 "\r\nContent-Type: application/octet-stream") != 0)
                goto fail;
        }
        if (http_append_cstr(&body, &body_len, &body_cap, "\r\n\r\n") != 0) goto fail;
        if (http_append_bytes(&body, &body_len, &body_cap, p->data ? p->data : "", p->data_len) != 0)
            goto fail;
        if (http_append_cstr(&body, &body_len, &body_cap, "\r\n") != 0) goto fail;
    }
    if (http_append_cstr(&body, &body_len, &body_cap, "--") != 0) goto fail;
    if (http_append_cstr(&body, &body_len, &body_cap, s->multipart_boundary) != 0) goto fail;
    if (http_append_cstr(&body, &body_len, &body_cap, "--\r\n") != 0) goto fail;
    free(s->body);
    s->body = body ? body : dup_n("", 0);
    s->body_len = body_len;
    body = NULL;
    snprintf(ct, sizeof ct, "multipart/form-data; boundary=%s", s->multipart_boundary);
    if (set_or_replace_header(s, "Content-Type", ct) != 0) return -1;
    s->parts_dirty = 0;
    s->sent = 0;
    return 0;
fail:
    free(body);
    return -1;
}

static int http_parts_enter(FlatHttpRequestSlot *s) {
    if (!s) return -1;
    if (!s->multipart_mode) {
        free(s->body);
        s->body = NULL;
        s->body_len = 0;
        s->multipart_mode = 1;
        if (s->parts_max == 0) s->parts_max = FLAT_HTTP_DEFAULT_PARTS_MAX;
    }
    http_parts_invalidate_body(s);
    return 0;
}

static int http_part_push(FlatHttpRequestSlot *s, FlatHttpPart *part) {
    FlatHttpPart *next;
    if (!s || !part) return -1;
    if (s->part_count >= s->parts_max) return -2;
    next = (FlatHttpPart *)realloc(s->parts, (s->part_count + 1) * sizeof *next);
    if (!next) return -1;
    s->parts = next;
    s->parts[s->part_count++] = *part;
    memset(part, 0, sizeof *part);
    return 0;
}

int flat_native_http_header(FlatNativeCtx *ctx){FlatValue a[3];uint32_t id;char *name=NULL,*val=NULL;size_t nl,vl;FlatHttpRequestSlot *s;if(get_args(ctx,a,3)!=0||num_arg(&a[0],&id)!=0||!flat_http_request_live(ctx->http,id)||value_string(ctx,&a[1],&name,&nl)!=0||value_string(ctx,&a[2],&val,&vl)!=0){free(name);free(val);return http_fail(ctx,"http.header.args");}s=&ctx->http->requests[id];if(append_header(s,name,val)!=0){free(name);free(val);return http_fail(ctx,"http.header.value");}free(name);free(val);return http_ok(ctx);}
static int http_out_body_bytes(FlatNativeCtx *ctx, const char *data, size_t len, const char *oom_why) {
    uint32_t ref;
    if (!ctx || !ctx->out) return -1;
    if (len <= FLAT_INLINE_STR_MAX) {
        http_ok(ctx);
        return flat_value_set_str_inline(ctx->out, data ? data : "", len);
    }
    if (flat_pool_alloc_copy(ctx->pool, data ? data : "", len, &ref) != 0) return http_fail(ctx, oom_why);
    http_ok(ctx);
    return flat_value_set_str_ref(ctx->out, ref);
}

int flat_native_http_request_body(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    FlatHttpRequestSlot *s;
    char *p = NULL;
    size_t n;
    char b[4096], *x = NULL, *y = NULL, *z = NULL;
    int argc;
    if (!ctx || !ctx->out || !ctx->inner) return -1;
    snprintf(b, sizeof b, "%s", ctx->inner);
    argc = split_args(b, &x, &y, &z, NULL);
    if (argc == 1) {
        FlatValue v;
        if (parse_value(ctx, x, &v) != 0 || num_arg(&v, &id) != 0 || !flat_http_request_live(ctx->http, id))
            return http_fail(ctx, "http.requestBody.id");
        s = &ctx->http->requests[id];
        return http_out_body_bytes(ctx, s->body, s->body_len, "pool.full");
    }
    if (argc != 2 || parse_value(ctx, x, &a[0]) != 0 || parse_value(ctx, y, &a[1]) != 0 ||
        num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &p, &n) != 0) {
        free(p);
        return http_fail(ctx, "http.requestBody.args");
    }
    s = &ctx->http->requests[id];
    http_parts_clear(s);
    free(s->body);
    s->body = p;
    s->body_len = n;
    s->sent = 0;
    return http_ok(ctx);
}

int flat_native_http_response_body(FlatNativeCtx *ctx) {
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.responseBody.id", &id);
    if (!s) return -1;
    if (!s->sent) return http_fail(ctx, "http.responseBody.not_sent");
    return http_out_body_bytes(ctx, s->response.body, s->response.body_len, "pool.full");
}
int flat_native_http_timeout(FlatNativeCtx *ctx){FlatValue a[2];uint32_t id;double ms;if(get_args(ctx,a,2)!=0||num_arg(&a[0],&id)!=0||!flat_http_request_live(ctx->http,id)||a[1].tag!=VAL_NUMBER)return http_fail(ctx,"http.timeout.args");ms=flat_number_as_f64(&a[1]);if(ms<1||ms>600000)return http_fail(ctx,"http.timeout.range");ctx->http->requests[id].timeout_ms=(long)ms;return http_ok(ctx);}
int flat_native_http_send(FlatNativeCtx *ctx){
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.send.id", &id);
    FlatHttpRequest r;
    if (!s) return -1;
    if (s->multipart_mode && s->parts_dirty) {
        if (http_parts_load_slot(s) != 0) return http_fail(ctx, "http.partsLoad.memory");
    }
    flat_http_response_free(&s->response);
    memset(&r, 0, sizeof r);
    r.method = s->method;
    r.url = s->url;
    r.body = s->body;
    r.body_len = s->body_len;
    r.headers = (const char *const *)s->headers;
    r.header_count = s->header_count;
    r.timeout_ms = s->timeout_ms;
    r.max_body_bytes = s->max_body_bytes;
    r.max_redirects = s->max_redirects;
    flat_http_perform(&r, &s->response);
    s->sent = 1;
    if (s->response.redirect_limit_hit) return http_fail(ctx, "http.redirect.limit");
    return http_ok(ctx);
}
int flat_native_http_status(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.status.id",&id);if(!s||!s->sent)return http_fail(ctx,"http.status.not_sent");http_ok(ctx);return flat_value_set_number(ctx->out,(double)s->response.status);}
int flat_native_http_error(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.error.id",&id);uint32_t ref;size_t n;if(!s||!s->sent)return http_fail(ctx,"http.error.not_sent");n=strlen(s->response.error);if(n<=FLAT_INLINE_STR_MAX){http_ok(ctx);return flat_value_set_str_inline(ctx->out,s->response.error,n);}if(flat_pool_alloc_copy(ctx->pool,s->response.error,n,&ref)!=0)return http_fail(ctx,"pool.full");http_ok(ctx);return flat_value_set_str_ref(ctx->out,ref);}
int flat_native_http_error_code(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.errorCode.id",&id);if(!s||!s->sent)return http_fail(ctx,"http.errorCode.not_sent");http_ok(ctx);return flat_value_set_number(ctx->out,(double)s->response.error_code);}
int flat_native_http_close(FlatNativeCtx *ctx){
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.close.id", &id);
    size_t i;
    if (!s) return -1;
    free(s->method);
    free(s->url);
    free(s->body);
    for (i = 0; i < s->header_count; i++) free(s->headers[i]);
    free(s->headers);
    flat_http_response_free(&s->response);
    http_parts_clear(s);
    memset(s, 0, sizeof *s);
    return http_ok(ctx);
}


int flat_native_http_json(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    char *json;
    size_t cap = FLAT_HTTP_DEFAULT_MAX_BODY;
    FlatHttpRequestSlot *s;
    int rc = -1;
    if (get_args(ctx, a, 2) != 0) return http_fail(ctx, "http.json.args");
    if (num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id)) {
        release_args(ctx, a, 2);
        return http_fail(ctx, "http.json.args");
    }
    s = &ctx->http->requests[id];
    json = malloc(cap + 1);
    if (!json) { release_args(ctx, a, 2); return http_fail(ctx, "http.json.memory"); }
    if (flat_json_stringify(&a[1], ctx->pool, ctx->coll, json, cap + 1) != 0) {
        free(json); release_args(ctx, a, 2); return http_fail(ctx, "http.json.serialize");
    }
    http_parts_clear(s);
    free(s->body); s->body = json; s->body_len = strlen(json); s->sent = 0;
    {
        size_t i; int found = 0;
        for (i = 0; i < s->header_count; i++) if (strncmp(s->headers[i], "content-type:", 13) == 0) found = 1;
        if (!found && append_header(s, "Content-Type", "application/json") != 0) {
            release_args(ctx, a, 2); return http_fail(ctx, "http.json.content_type");
        }
    }
    rc = http_ok(ctx);
    release_args(ctx, a, 2);
    return rc;
}

int flat_native_http_headers(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    FlatHttpRequestSlot *s;
    int n, i, rc = -1;
    if (get_args(ctx, a, 2) != 0) return http_fail(ctx, "http.headers.args");
    if (num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) || a[1].tag != VAL_OBJECT) {
        release_args(ctx, a, 2);
        return http_fail(ctx, "http.headers.args");
    }
    s = &ctx->http->requests[id];
    for (i = 0; i < (int)s->header_count; i++) free(s->headers[i]);
    free(s->headers); s->headers = NULL; s->header_count = 0;
    if (flat_obj_len(ctx->coll, a[1].u.coll_id, &n) != 0) goto done;
    for (i = 0; i < n; i++) {
        const char *key = NULL;
        FlatValue vals;
        int j, vc;
        if (flat_obj_entry_at(ctx->coll, a[1].u.coll_id, i, &key, &vals) != 0 || !key) goto done;
        if (vals.tag == VAL_LIST) {
            if (flat_list_len(ctx->coll, vals.u.coll_id, &vc) != 0) goto done;
            for (j = 0; j < vc; j++) {
                FlatValue x; char *value = NULL; size_t vl;
                if (flat_list_get(ctx->coll, vals.u.coll_id, j, &x) != 0 || value_string(ctx, &x, &value, &vl) != 0) goto done;
                if (append_header(s, key, value) != 0) { free(value); goto done; }
                free(value);
            }
        } else {
            char *value = NULL; size_t vl;
            if (value_string(ctx, &vals, &value, &vl) != 0) goto done;
            if (append_header(s, key, value) != 0) { free(value); goto done; }
            free(value);
        }
    }
    rc = http_ok(ctx);
done:
    release_args(ctx, a, 2);
    return rc == 0 ? 0 : http_fail(ctx, "http.headers.invalid");
}

/* Build OBJECT map name -> LIST of STRING from "name: value" header lines.
 * Grouping buffers are heap-allocated (FlatValue grid is ~1MB — too large for stack). */
static int build_headers_object(FlatNativeCtx *ctx, char **lines, size_t count, uint32_t *out_obj) {
    FlatValue *lists_v = NULL;
    char (*keys)[FLAT_OBJ_KEY_MAX + 1] = NULL;
    FlatValue (*grouped)[64] = NULL;
    int *counts = NULL;
    int i, j, nkeys = 0, rc = -1;
    uint32_t obj_id = 0;
    if (!ctx || !out_obj) return -1;
    lists_v = (FlatValue *)calloc((size_t)FLAT_OBJ_MAX_KEYS, sizeof *lists_v);
    keys = (char (*)[FLAT_OBJ_KEY_MAX + 1])calloc((size_t)FLAT_OBJ_MAX_KEYS, sizeof *keys);
    grouped = (FlatValue (*)[64])calloc((size_t)FLAT_OBJ_MAX_KEYS, sizeof *grouped);
    counts = (int *)calloc((size_t)FLAT_OBJ_MAX_KEYS, sizeof *counts);
    if (!lists_v || !keys || !grouped || !counts) goto done;
    for (i = 0; i < (int)count; ++i) {
        char *line = lines[i], *colon;
        char name[FLAT_OBJ_KEY_MAX + 1], *value;
        int found = -1;
        size_t k, nn;
        FlatValue item;
        if (!line) continue;
        colon = strchr(line, ':');
        if (!colon) continue;
        nn = (size_t)(colon - line);
        if (!nn || nn > FLAT_OBJ_KEY_MAX) continue;
        for (k = 0; k < nn; ++k) name[k] = (char)tolower((unsigned char)line[k]);
        name[nn] = '\0';
        value = colon + 1;
        while (*value == ' ' || *value == '\t') value++;
        for (j = 0; j < nkeys; ++j) if (strcmp(keys[j], name) == 0) { found = j; break; }
        if (found < 0) {
            if (nkeys >= FLAT_OBJ_MAX_KEYS) continue;
            found = nkeys++;
            snprintf(keys[found], sizeof keys[found], "%s", name);
        }
        if (counts[found] >= 64) continue;
        if (make_string_value(ctx, value, strlen(value), &item) != 0) continue;
        grouped[found][counts[found]++] = item;
    }
    if (flat_obj_empty(ctx->coll, ctx->pool, &obj_id) != 0) goto done;
    for (i = 0; i < nkeys; ++i) {
        uint32_t list_id, next_obj;
        if (flat_list_build(ctx->coll, ctx->pool, grouped[i], counts[i], &list_id) != 0 ||
            flat_value_set_list(&lists_v[i], list_id) != 0 ||
            flat_obj_set_copy(ctx->coll, ctx->pool, obj_id, keys[i], &lists_v[i], &next_obj) != 0)
            goto done;
        obj_id = next_obj;
    }
    *out_obj = obj_id;
    rc = 0;
done:
    free(lists_v);
    free(keys);
    free(grouped);
    free(counts);
    return rc;
}

int flat_native_http_response_headers(FlatNativeCtx *ctx) {
    uint32_t id, obj_id;
    FlatHttpRequestSlot *s;
    FlatValue arg;
    if (!ctx || !ctx->out || get_args(ctx, &arg, 1) != 0 || num_arg(&arg, &id) != 0 ||
        !flat_http_request_live(ctx->http, id)) return http_fail(ctx, "http.responseHeaders.id");
    s = &ctx->http->requests[id];
    if (!s->sent) return http_fail(ctx, "http.responseHeaders.not_sent");
    if (build_headers_object(ctx, s->response.headers, s->response.header_count, &obj_id) != 0)
        return http_fail(ctx, "http.responseHeaders.memory");
    http_ok(ctx);
    return flat_value_set_object(ctx->out, obj_id);
}

int flat_native_http_max_body(FlatNativeCtx *ctx) {
    FlatValue a[2];uint32_t id;double bytes;if(get_args(ctx,a,2)!=0||num_arg(&a[0],&id)!=0||!flat_http_request_live(ctx->http,id)||a[1].tag!=VAL_NUMBER)return http_fail(ctx,"http.maxBody.args");bytes=flat_number_as_f64(&a[1]);if(bytes<1||bytes>FLAT_POOL_MAX_CHUNK||bytes!=(double)(size_t)bytes)return http_fail(ctx,"http.maxBody.range");ctx->http->requests[id].max_body_bytes=(size_t)bytes;return http_ok(ctx);
}

/* Map CURLcode-like transport codes to stable panic labels (not the full curl enum). */
static const char *http_transport_label(int code) {
    if (code == 0) return NULL;
    if (code == 28) return "http.transport.timeout";
    if (code == 23) return "http.transport.overflow";
    if (code == 27) return "http.transport.memory";
    if (code == 6 || code == 7) return "http.transport.connect";
    /* Common libcurl TLS / SSL codes (and WASM aliases that reuse them). */
    if (code == 35 || code == 51 || code == 53 || code == 54 || code == 58 ||
        code == 59 || code == 60 || code == 64 || code == 66 || code == 77 ||
        code == 80 || code == 82 || code == 83 || code == 90 || code == 91) {
        return "http.transport.tls";
    }
    return "http.transport.other";
}

static const char *http_status_label(long status) {
    if (status >= 200 && status <= 299) return NULL;
    if (status == 404) return "http.status.not_found";
    if (status == 401) return "http.status.unauthorized";
    if (status == 403) return "http.status.forbidden";
    if (status == 409) return "http.status.conflict";
    if (status >= 500 && status <= 599) return "http.status.server_error";
    if (status >= 300 && status <= 399) return "http.status.redirect";
    if (status >= 400 && status <= 499) return "http.status.client_error";
    return "http.status.other";
}

int flat_native_http_transport_ok(FlatNativeCtx *ctx) {
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.transportOk.id", &id);
    const char *label;
    if (!s) return -1;
    if (!s->sent) return http_fail(ctx, "http.transportOk.not_sent");
    label = http_transport_label(s->response.error_code);
    if (label) return http_fail(ctx, label);
    return http_ok(ctx);
}

int flat_native_http_status_ok(FlatNativeCtx *ctx) {
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.statusOk.id", &id);
    const char *label;
    if (!s) return -1;
    if (!s->sent) return http_fail(ctx, "http.statusOk.not_sent");
    label = http_status_label(s->response.status);
    if (label) return http_fail(ctx, label);
    return http_ok(ctx);
}

int flat_native_http_request_info(FlatNativeCtx *ctx) {
    uint32_t id, obj_id, headers_id;
    char keys[9][FLAT_OBJ_KEY_MAX + 1];
    FlatValue vals[9];
    FlatHttpRequestSlot *s = resolve(ctx, "http.requestInfo.id", &id);
    if (!s || !ctx->out) return s ? http_fail(ctx, "http.requestInfo.args") : -1;
    if (build_headers_object(ctx, s->headers, s->header_count, &headers_id) != 0)
        return http_fail(ctx, "http.requestInfo.memory");
    memset(vals, 0, sizeof vals);
    if (make_string_value(ctx, s->method ? s->method : "", s->method ? strlen(s->method) : 0, &vals[0]) != 0 ||
        make_string_value(ctx, s->url ? s->url : "", s->url ? strlen(s->url) : 0, &vals[1]) != 0 ||
        flat_value_set_object(&vals[2], headers_id) != 0 ||
        flat_value_set_number(&vals[3], (double)s->timeout_ms) != 0 ||
        flat_value_set_number(&vals[4], (double)s->max_body_bytes) != 0 ||
        flat_value_set_bool(&vals[5], s->sent ? 1 : 0) != 0 ||
        flat_value_set_number(&vals[6], (double)s->body_len) != 0 ||
        flat_value_set_number(&vals[7], (double)s->max_redirects) != 0 ||
        flat_value_set_number(&vals[8], (double)s->part_count) != 0)
        return http_fail(ctx, "http.requestInfo.memory");
    snprintf(keys[0], sizeof keys[0], "method");
    snprintf(keys[1], sizeof keys[1], "url");
    snprintf(keys[2], sizeof keys[2], "headers");
    snprintf(keys[3], sizeof keys[3], "timeoutMs");
    snprintf(keys[4], sizeof keys[4], "maxBody");
    snprintf(keys[5], sizeof keys[5], "sent");
    snprintf(keys[6], sizeof keys[6], "bodyLen");
    snprintf(keys[7], sizeof keys[7], "maxRedirects");
    snprintf(keys[8], sizeof keys[8], "partCount");
    if (flat_obj_build(ctx->coll, ctx->pool, keys, vals, 9, &obj_id) != 0)
        return http_fail(ctx, "http.requestInfo.memory");
    http_ok(ctx);
    return flat_value_set_object(ctx->out, obj_id);
}

int flat_native_http_response_info(FlatNativeCtx *ctx) {
    uint32_t id, obj_id, headers_id;
    char keys[5][FLAT_OBJ_KEY_MAX + 1];
    FlatValue vals[5];
    FlatHttpRequestSlot *s = resolve(ctx, "http.responseInfo.id", &id);
    if (!s || !ctx->out) return s ? http_fail(ctx, "http.responseInfo.args") : -1;
    if (!s->sent) return http_fail(ctx, "http.responseInfo.not_sent");
    if (build_headers_object(ctx, s->response.headers, s->response.header_count, &headers_id) != 0)
        return http_fail(ctx, "http.responseInfo.memory");
    memset(vals, 0, sizeof vals);
    if (flat_value_set_number(&vals[0], (double)s->response.status) != 0 ||
        flat_value_set_object(&vals[1], headers_id) != 0 ||
        make_string_value(ctx, s->response.error, strlen(s->response.error), &vals[2]) != 0 ||
        flat_value_set_number(&vals[3], (double)s->response.error_code) != 0 ||
        flat_value_set_number(&vals[4], (double)s->response.body_len) != 0)
        return http_fail(ctx, "http.responseInfo.memory");
    snprintf(keys[0], sizeof keys[0], "status");
    snprintf(keys[1], sizeof keys[1], "headers");
    snprintf(keys[2], sizeof keys[2], "error");
    snprintf(keys[3], sizeof keys[3], "errorCode");
    snprintf(keys[4], sizeof keys[4], "bodyLen");
    if (flat_obj_build(ctx->coll, ctx->pool, keys, vals, 5, &obj_id) != 0)
        return http_fail(ctx, "http.responseInfo.memory");
    http_ok(ctx);
    return flat_value_set_object(ctx->out, obj_id);
}

int flat_native_http_max_redirects(FlatNativeCtx *ctx) {
    char b[4096], *x = NULL, *y = NULL, *z = NULL;
    int argc;
    FlatValue v0, v1;
    uint32_t id;
    FlatHttpRequestSlot *s;
    double n;
    if (!ctx || !ctx->out || !ctx->inner || !ctx->http) return -1;
    snprintf(b, sizeof b, "%s", ctx->inner);
    argc = split_args(b, &x, &y, &z, NULL);
    if (argc == 1) {
        if (parse_value(ctx, x, &v0) != 0 || num_arg(&v0, &id) != 0 || !flat_http_request_live(ctx->http, id))
            return http_fail(ctx, "http.maxRedirects.id");
        s = &ctx->http->requests[id];
        http_ok(ctx);
        return flat_value_set_number(ctx->out, (double)s->max_redirects);
    }
    if (argc != 2 || parse_value(ctx, x, &v0) != 0 || parse_value(ctx, y, &v1) != 0 ||
        num_arg(&v0, &id) != 0 || !flat_http_request_live(ctx->http, id) || v1.tag != VAL_NUMBER) {
        return http_fail(ctx, "http.maxRedirects.args");
    }
    n = flat_number_as_f64(&v1);
    if (n != (double)(long)n || n < -1.0 || n > 1000000.0)
        return http_fail(ctx, "http.maxRedirects.range");
    ctx->http->requests[id].max_redirects = (long)n;
    return http_ok(ctx);
}

int flat_native_http_response_statuses(FlatNativeCtx *ctx) {
    uint32_t id, list_id;
    size_t i;
    FlatValue *items;
    FlatHttpRequestSlot *s = resolve(ctx, "http.responseStatuses.id", &id);
    if (!s) return -1;
    if (!s->sent) return http_fail(ctx, "http.responseStatuses.not_sent");
    if (s->response.status_chain_count == 0) {
        FlatValue one;
        memset(&one, 0, sizeof one);
        if (flat_value_set_number(&one, (double)s->response.status) != 0) return http_fail(ctx, "http.responseStatuses.memory");
        if (flat_list_build(ctx->coll, ctx->pool, &one, 1, &list_id) != 0)
            return http_fail(ctx, "http.responseStatuses.memory");
        http_ok(ctx);
        return flat_value_set_list(ctx->out, list_id);
    }
    items = (FlatValue *)calloc(s->response.status_chain_count, sizeof *items);
    if (!items) return http_fail(ctx, "http.responseStatuses.memory");
    for (i = 0; i < s->response.status_chain_count; ++i) {
        if (flat_value_set_number(&items[i], (double)s->response.status_chain[i]) != 0) {
            free(items);
            return http_fail(ctx, "http.responseStatuses.memory");
        }
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, (int)s->response.status_chain_count, &list_id) != 0) {
        free(items);
        return http_fail(ctx, "http.responseStatuses.memory");
    }
    free(items);
    http_ok(ctx);
    return flat_value_set_list(ctx->out, list_id);
}

int flat_native_http_bearer(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    char *token = NULL;
    size_t tl;
    char *auth;
    FlatHttpRequestSlot *s;
    if (get_args(ctx, a, 2) != 0 || num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &token, &tl) != 0) {
        free(token);
        return http_fail(ctx, "http.bearer.args");
    }
    s = &ctx->http->requests[id];
    auth = (char *)malloc(tl + 8);
    if (!auth) {
        free(token);
        return http_fail(ctx, "http.bearer.memory");
    }
    snprintf(auth, tl + 8, "Bearer %s", token);
    free(token);
    if (set_or_replace_header(s, "Authorization", auth) != 0) {
        free(auth);
        return http_fail(ctx, "http.bearer.value");
    }
    free(auth);
    return http_ok(ctx);
}

int flat_native_http_basic(FlatNativeCtx *ctx) {
    FlatValue a[3];
    uint32_t id;
    char *user = NULL, *pass = NULL, *pair = NULL, *b64 = NULL, *auth = NULL;
    size_t ul, pl, pair_len, b64_len;
    FlatHttpRequestSlot *s;
    if (get_args(ctx, a, 3) != 0 || num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &user, &ul) != 0 || value_string(ctx, &a[2], &pass, &pl) != 0) {
        free(user);
        free(pass);
        return http_fail(ctx, "http.basic.args");
    }
    s = &ctx->http->requests[id];
    pair_len = ul + 1 + pl;
    pair = (char *)malloc(pair_len + 1);
    if (!pair) {
        free(user);
        free(pass);
        return http_fail(ctx, "http.basic.memory");
    }
    memcpy(pair, user, ul);
    pair[ul] = ':';
    memcpy(pair + ul + 1, pass, pl);
    pair[pair_len] = '\0';
    free(user);
    free(pass);
    b64 = flat_enc_base64_encode((const unsigned char *)pair, pair_len, &b64_len);
    free(pair);
    if (!b64) return http_fail(ctx, "http.basic.memory");
    auth = (char *)malloc(b64_len + 8);
    if (!auth) {
        free(b64);
        return http_fail(ctx, "http.basic.memory");
    }
    snprintf(auth, b64_len + 8, "Basic %s", b64);
    free(b64);
    if (set_or_replace_header(s, "Authorization", auth) != 0) {
        free(auth);
        return http_fail(ctx, "http.basic.value");
    }
    free(auth);
    return http_ok(ctx);
}

static int form_value_bytes(FlatNativeCtx *ctx, const FlatValue *v, char **out, size_t *out_len) {
    char tmp[64];
    if (!v || !out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    if (v->tag == VAL_STR_INLINE || v->tag == VAL_STR_REF) return value_string(ctx, v, out, out_len);
    if (v->tag == VAL_NUMBER) {
        snprintf(tmp, sizeof tmp, "%.17g", flat_number_as_f64(v));
        *out = dup_n(tmp, strlen(tmp));
        if (!*out) return -1;
        *out_len = strlen(tmp);
        return 0;
    }
    if (v->tag == VAL_BOOL) {
        const char *t = v->u.boolean ? "true" : "false";
        *out = dup_n(t, strlen(t));
        if (!*out) return -1;
        *out_len = strlen(t);
        return 0;
    }
    if (v->tag == VAL_NULL) {
        *out = dup_n("", 0);
        return *out ? 0 : -1;
    }
    return -1;
}

int flat_native_http_form(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    FlatHttpRequestSlot *s;
    char *body = NULL;
    size_t body_len = 0, body_cap = 0;
    int n, i, first = 1, rc = -1;
    if (get_args(ctx, a, 2) != 0) return http_fail(ctx, "http.form.args");
    if (num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) || a[1].tag != VAL_OBJECT) {
        release_args(ctx, a, 2);
        return http_fail(ctx, "http.form.args");
    }
    s = &ctx->http->requests[id];
    http_parts_clear(s);
    if (flat_obj_len(ctx->coll, a[1].u.coll_id, &n) != 0) goto done;
    for (i = 0; i < n; i++) {
        const char *key = NULL;
        FlatValue val;
        char *raw = NULL;
        size_t raw_len = 0;
        if (flat_obj_entry_at(ctx->coll, a[1].u.coll_id, i, &key, &val) != 0 || !key) goto done;
        if (form_value_bytes(ctx, &val, &raw, &raw_len) != 0) {
            free(raw);
            goto done;
        }
        if (!first) {
            char *nb;
            size_t ncap = body_cap ? body_cap : 64;
            while (body_len + 2 > ncap) ncap *= 2;
            nb = (char *)realloc(body, ncap);
            if (!nb) {
                free(raw);
                goto done;
            }
            body = nb;
            body_cap = ncap;
            body[body_len++] = '&';
            body[body_len] = '\0';
        }
        first = 0;
        if (flat_enc_url_form_append(&body, &body_len, &body_cap, key, strlen(key)) != 0) {
            free(raw);
            goto done;
        }
        {
            char *nb;
            size_t ncap = body_cap ? body_cap : 64;
            while (body_len + 2 > ncap) ncap *= 2;
            nb = (char *)realloc(body, ncap);
            if (!nb) {
                free(raw);
                goto done;
            }
            body = nb;
            body_cap = ncap;
            body[body_len++] = '=';
            body[body_len] = '\0';
        }
        if (flat_enc_url_form_append(&body, &body_len, &body_cap, raw ? raw : "", raw_len) != 0) {
            free(raw);
            goto done;
        }
        free(raw);
    }
    free(s->body);
    s->body = body ? body : dup_n("", 0);
    s->body_len = body_len;
    body = NULL;
    s->sent = 0;
    if (set_or_replace_header(s, "Content-Type", "application/x-www-form-urlencoded") != 0) goto done;
    rc = http_ok(ctx);
done:
    free(body);
    release_args(ctx, a, 2);
    return rc == 0 ? 0 : http_fail(ctx, "http.form.invalid");
}

static int http_read_file_bytes(FlatNativeCtx *ctx, uint32_t fid, char **out, size_t *out_len) {
    FILE *fp;
    long pos, end;
    size_t n, got;
    char *buf;
    if (!ctx || !ctx->file_tbl || !out || !out_len) return -1;
    fp = flat_file_fp(ctx->file_tbl, fid);
    if (!fp) return -1;
    pos = ftell(fp);
    if (pos < 0) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    end = ftell(fp);
    if (end < 0 || fseek(fp, 0, SEEK_SET) != 0) return -1;
    n = (size_t)end;
    buf = (char *)malloc(n + 1);
    if (!buf) {
        fseek(fp, pos, SEEK_SET);
        return -1;
    }
    got = fread(buf, 1, n, fp);
    fseek(fp, pos, SEEK_SET);
    if (got != n) {
        free(buf);
        return -1;
    }
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 0;
}

int flat_native_http_add_part(FlatNativeCtx *ctx) {
    FlatValue a[3];
    uint32_t id;
    FlatHttpRequestSlot *s;
    FlatHttpPart part;
    char *name = NULL;
    size_t nl;
    int prc;
    memset(&part, 0, sizeof part);
    if (get_args(ctx, a, 3) != 0 || num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &name, &nl) != 0 || !name || !nl) {
        free(name);
        return http_fail(ctx, "http.addPart.args");
    }
    s = &ctx->http->requests[id];
    if (http_parts_enter(s) != 0) {
        free(name);
        return http_fail(ctx, "http.addPart.memory");
    }
    if (form_value_bytes(ctx, &a[2], &part.data, &part.data_len) != 0) {
        free(name);
        http_part_free(&part);
        return http_fail(ctx, "http.addPart.value");
    }
    part.name = name;
    name = NULL;
    part.kind = FLAT_HTTP_PART_TEXT;
    prc = http_part_push(s, &part);
    if (prc == -2) {
        http_part_free(&part);
        return http_fail(ctx, "http.addPart.limit");
    }
    if (prc != 0) {
        http_part_free(&part);
        return http_fail(ctx, "http.addPart.memory");
    }
    return http_ok(ctx);
}

int flat_native_http_add_file_part(FlatNativeCtx *ctx) {
    char buf[4096], *a0 = NULL, *a1 = NULL, *a2 = NULL, *a3 = NULL;
    int argc;
    FlatValue v[4];
    uint32_t id, fid;
    FlatHttpRequestSlot *s;
    FlatHttpPart part;
    char *name = NULL, *filename = NULL;
    size_t nl, fl = 0;
    int prc;
    memset(&part, 0, sizeof part);
    memset(v, 0, sizeof v);
    if (!ctx || !ctx->inner || !ctx->http) return -1;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    argc = split_args(buf, &a0, &a1, &a2, &a3);
    if (argc < 3 || argc > 4) return http_fail(ctx, "http.addFilePart.args");
    if (parse_value(ctx, a0, &v[0]) != 0 || parse_value(ctx, a1, &v[1]) != 0 ||
        parse_value(ctx, a2, &v[2]) != 0) {
        return http_fail(ctx, "http.addFilePart.args");
    }
    if (argc == 4 && parse_value(ctx, a3, &v[3]) != 0) {
        release_args(ctx, v, 3);
        return http_fail(ctx, "http.addFilePart.args");
    }
    if (num_arg(&v[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &v[1], &name, &nl) != 0 || !name || !nl) {
        free(name);
        release_args(ctx, v, argc);
        return http_fail(ctx, "http.addFilePart.args");
    }
    if (argc == 4) {
        if (value_string(ctx, &v[3], &filename, &fl) != 0 || !filename) {
            free(name);
            free(filename);
            release_args(ctx, v, argc);
            return http_fail(ctx, "http.addFilePart.filename");
        }
    } else {
        filename = dup_n(name, nl);
        if (!filename) {
            free(name);
            release_args(ctx, v, argc);
            return http_fail(ctx, "http.addFilePart.memory");
        }
    }
    s = &ctx->http->requests[id];
    if (http_parts_enter(s) != 0) {
        free(name);
        free(filename);
        release_args(ctx, v, argc);
        return http_fail(ctx, "http.addFilePart.memory");
    }
    if (ctx->file_tbl && flat_value_get_file_handle(&v[2], ctx->file_tbl, &fid)) {
        if (http_read_file_bytes(ctx, fid, &part.data, &part.data_len) != 0) {
            free(name);
            free(filename);
            release_args(ctx, v, argc);
            return http_fail(ctx, "http.addFilePart.file");
        }
    } else if (value_string(ctx, &v[2], &part.data, &part.data_len) != 0) {
        free(name);
        free(filename);
        release_args(ctx, v, argc);
        return http_fail(ctx, "http.addFilePart.data");
    }
    part.name = name;
    name = NULL;
    part.filename = filename;
    filename = NULL;
    part.kind = FLAT_HTTP_PART_FILE;
    prc = http_part_push(s, &part);
    release_args(ctx, v, argc);
    if (prc == -2) {
        http_part_free(&part);
        return http_fail(ctx, "http.addFilePart.limit");
    }
    if (prc != 0) {
        http_part_free(&part);
        return http_fail(ctx, "http.addFilePart.memory");
    }
    return http_ok(ctx);
}

int flat_native_http_remove_part(FlatNativeCtx *ctx) {
    FlatValue a[2];
    uint32_t id;
    FlatHttpRequestSlot *s;
    char *name = NULL;
    size_t nl, i, w = 0;
    if (get_args(ctx, a, 2) != 0 || num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &name, &nl) != 0 || !name) {
        free(name);
        return http_fail(ctx, "http.removePart.args");
    }
    s = &ctx->http->requests[id];
    for (i = 0; i < s->part_count; ++i) {
        if (s->parts[i].name && strcmp(s->parts[i].name, name) == 0) {
            http_part_free(&s->parts[i]);
        } else {
            if (w != i) s->parts[w] = s->parts[i];
            w++;
        }
    }
    s->part_count = w;
    if (w == 0) {
        free(s->parts);
        s->parts = NULL;
    }
    free(name);
    if (s->multipart_mode) http_parts_invalidate_body(s);
    return http_ok(ctx);
}

int flat_native_http_reset_parts(FlatNativeCtx *ctx) {
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.resetParts.id", &id);
    if (!s) return -1;
    http_parts_reset_keep_mode(s);
    return http_ok(ctx);
}

int flat_native_http_parts_load(FlatNativeCtx *ctx) {
    uint32_t id;
    FlatHttpRequestSlot *s = resolve(ctx, "http.partsLoad.id", &id);
    if (!s) return -1;
    if (!s->multipart_mode) {
        s->multipart_mode = 1;
        if (s->parts_max == 0) s->parts_max = FLAT_HTTP_DEFAULT_PARTS_MAX;
        s->parts_dirty = 1;
    }
    if (http_parts_load_slot(s) != 0) return http_fail(ctx, "http.partsLoad.memory");
    return http_ok(ctx);
}

int flat_native_http_parts_max(FlatNativeCtx *ctx) {
    char b[4096], *x = NULL, *y = NULL, *z = NULL;
    int argc;
    FlatValue v0, v1;
    uint32_t id;
    FlatHttpRequestSlot *s;
    double n;
    if (!ctx || !ctx->out || !ctx->inner || !ctx->http) return -1;
    snprintf(b, sizeof b, "%s", ctx->inner);
    argc = split_args(b, &x, &y, &z, NULL);
    if (argc == 1) {
        if (parse_value(ctx, x, &v0) != 0 || num_arg(&v0, &id) != 0 || !flat_http_request_live(ctx->http, id))
            return http_fail(ctx, "http.partsMax.id");
        s = &ctx->http->requests[id];
        http_ok(ctx);
        return flat_value_set_number(ctx->out, (double)(s->parts_max ? s->parts_max : FLAT_HTTP_DEFAULT_PARTS_MAX));
    }
    if (argc != 2 || parse_value(ctx, x, &v0) != 0 || parse_value(ctx, y, &v1) != 0 ||
        num_arg(&v0, &id) != 0 || !flat_http_request_live(ctx->http, id) || v1.tag != VAL_NUMBER) {
        return http_fail(ctx, "http.partsMax.args");
    }
    n = flat_number_as_f64(&v1);
    if (n != (double)(long)n || n < 1.0 || n > (double)FLAT_HTTP_HARD_PARTS_MAX)
        return http_fail(ctx, "http.partsMax.range");
    s = &ctx->http->requests[id];
    if ((size_t)n < s->part_count) return http_fail(ctx, "http.partsMax.below_count");
    s->parts_max = (size_t)n;
    return http_ok(ctx);
}

int flat_native_http_part_info(FlatNativeCtx *ctx) {
    uint32_t id, list_id;
    size_t i;
    FlatValue *items = NULL;
    FlatHttpRequestSlot *s = resolve(ctx, "http.partInfo.id", &id);
    if (!s || !ctx->out) return s ? http_fail(ctx, "http.partInfo.args") : -1;
    if (s->part_count == 0) {
        if (flat_list_build(ctx->coll, ctx->pool, NULL, 0, &list_id) != 0)
            return http_fail(ctx, "http.partInfo.memory");
        http_ok(ctx);
        return flat_value_set_list(ctx->out, list_id);
    }
    items = (FlatValue *)calloc(s->part_count, sizeof *items);
    if (!items) return http_fail(ctx, "http.partInfo.memory");
    for (i = 0; i < s->part_count; ++i) {
        FlatHttpPart *p = &s->parts[i];
        char keys[4][FLAT_OBJ_KEY_MAX + 1];
        FlatValue vals[4];
        uint32_t obj_id;
        int nkeys = 3;
        const char *kind_s = (p->kind == FLAT_HTTP_PART_FILE) ? "file" : "text";
        memset(vals, 0, sizeof vals);
        if (make_string_value(ctx, p->name ? p->name : "", p->name ? strlen(p->name) : 0, &vals[0]) != 0 ||
            make_string_value(ctx, kind_s, strlen(kind_s), &vals[1]) != 0 ||
            flat_value_set_number(&vals[2], (double)p->data_len) != 0) {
            free(items);
            return http_fail(ctx, "http.partInfo.memory");
        }
        snprintf(keys[0], sizeof keys[0], "name");
        snprintf(keys[1], sizeof keys[1], "kind");
        snprintf(keys[2], sizeof keys[2], "size");
        if (p->kind == FLAT_HTTP_PART_FILE) {
            if (make_string_value(ctx, p->filename ? p->filename : "",
                                  p->filename ? strlen(p->filename) : 0, &vals[3]) != 0) {
                free(items);
                return http_fail(ctx, "http.partInfo.memory");
            }
            snprintf(keys[3], sizeof keys[3], "filename");
            nkeys = 4;
        }
        if (flat_obj_build(ctx->coll, ctx->pool, keys, vals, nkeys, &obj_id) != 0) {
            size_t j;
            for (j = 0; j < i; ++j) flat_value_release(ctx->coll, ctx->pool, &items[j]);
            free(items);
            return http_fail(ctx, "http.partInfo.memory");
        }
        if (flat_value_set_object(&items[i], obj_id) != 0) {
            size_t j;
            for (j = 0; j < i; ++j) flat_value_release(ctx->coll, ctx->pool, &items[j]);
            free(items);
            return http_fail(ctx, "http.partInfo.memory");
        }
        /* Pin until list_build: flat_obj_empty reaps bindCount==0 slots. */
        flat_value_retain(ctx->coll, ctx->pool, &items[i]);
    }
    if (flat_list_build(ctx->coll, ctx->pool, items, (int)s->part_count, &list_id) != 0) {
        for (i = 0; i < s->part_count; ++i) flat_value_release(ctx->coll, ctx->pool, &items[i]);
        free(items);
        return http_fail(ctx, "http.partInfo.memory");
    }
    for (i = 0; i < s->part_count; ++i) flat_value_release(ctx->coll, ctx->pool, &items[i]);
    free(items);
    http_ok(ctx);
    return flat_value_set_list(ctx->out, list_id);
}
