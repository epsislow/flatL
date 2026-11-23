#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_http_runtime.h"
#include "flat_value.h"
#include "flat_number.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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
    if (flat_pool_alloc_copy(ctx->pool, data, len, &ref) != 0) return -1;
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
static int split_args(char *s,char **a,char **b,char **c) {
    int depth=0; char q=0; char *p; int n=0; char **dst[3]={a,b,c};
    for(p=s;*p;p++) { if(q){if(*p==q && (p==s || p[-1]!='\\')) q=0; continue;} if(*p=='\''||*p=='"'){q=*p;continue;} if(*p=='['||*p=='{'||*p=='(') depth++; else if((*p==']'||*p=='}'||*p==')')&&depth) depth--; else if(*p==','&&depth==0){*p='\0'; if(n<3)*dst[n]=flat_native_trim(s); n++; s=p+1;} }
    if (n < 3) *dst[n] = flat_native_trim(s);
    return n + 1;
}
static int get_args(FlatNativeCtx *ctx,FlatValue *v,int want) {
    char buf[4096],*a=NULL,*b=NULL,*c=NULL; int n;
    if (!ctx || !ctx->inner) return -1;
    snprintf(buf, sizeof buf, "%s", ctx->inner);
    n = split_args(buf, &a, &b, &c);
    if (n != want) return -1;
    if(parse_value(ctx,a,&v[0])!=0)return -1;
    if(want>1 && parse_value(ctx,b,&v[1])!=0){ release_args(ctx,v,1); return -1; }
    if(want>2 && parse_value(ctx,c,&v[2])!=0){ release_args(ctx,v,2); return -1; }
    return 0;
}
static int num_arg(const FlatValue *v,uint32_t *out) { double d; if(!v||v->tag!=VAL_NUMBER)return -1; d=flat_number_as_f64(v); if(d<1||d>FLAT_HTTP_MAX_REQUESTS||d!=(double)(uint32_t)d)return -1; *out=(uint32_t)d; return 0; }
int flat_http_request_live(const FlatHttpRuntime *rt,uint32_t id){return rt&&id>=1&&id<=FLAT_HTTP_MAX_REQUESTS&&rt->requests[id].live;}
static FlatHttpRequestSlot *resolve(FlatNativeCtx *ctx,const char *fn,uint32_t *id){FlatValue v;if(!ctx||!ctx->http||get_args(ctx,&v,1)!=0||num_arg(&v,id)!=0||!flat_http_request_live(ctx->http,*id)){http_fail(ctx,fn);return NULL;}return &ctx->http->requests[*id];}
void flat_http_runtime_destroy(FlatHttpRuntime *rt){uint32_t i; if(!rt)return; for(i=1;i<=FLAT_HTTP_MAX_REQUESTS;i++){FlatHttpRequestSlot *s=&rt->requests[i];size_t j;free(s->method);free(s->url);free(s->body);for(j=0;j<s->header_count;j++)free(s->headers[j]);free(s->headers);flat_http_response_free(&s->response);memset(s,0,sizeof *s);}}
static int append_header(FlatHttpRequestSlot *s,const char *name,const char *value){size_t n=strlen(name),v=strlen(value),i;char *line;char **next;if(!n||n>FLAT_OBJ_KEY_MAX||strchr(name,':')||strchr(name,'\r')||strchr(name,'\n')||strchr(value,'\r')||strchr(value,'\n'))return -1;line=malloc(n+v+3);if(!line)return -1;for(i=0;i<n;i++)line[i]=(char)tolower((unsigned char)name[i]);line[n]=':';line[n+1]=' ';memcpy(line+n+2,value,v+1);next=realloc(s->headers,(s->header_count+1)*sizeof *next);if(!next){free(line);return -1;}s->headers=next;s->headers[s->header_count++]=line;return 0;}
int flat_native_http_create(FlatNativeCtx *ctx){FlatValue a[2];char *m=NULL,*u=NULL;size_t ml,ul;uint32_t i;if(!ctx||!ctx->http||!ctx->out||get_args(ctx,a,2)!=0||value_string(ctx,&a[0],&m,&ml)!=0||value_string(ctx,&a[1],&u,&ul)!=0){free(m);free(u);return http_fail(ctx,"http.create.args");}for(i=1;i<=FLAT_HTTP_MAX_REQUESTS;i++)if(!ctx->http->requests[i].live){FlatHttpRequestSlot *s=&ctx->http->requests[i];memset(s,0,sizeof *s);s->live=1;s->method=m;s->url=u;s->timeout_ms=FLAT_HTTP_DEFAULT_TIMEOUT_MS;s->max_body_bytes=FLAT_HTTP_DEFAULT_MAX_BODY;return http_ok(ctx)+flat_value_set_number(ctx->out,(double)i);}free(m);free(u);return http_fail(ctx,"http.create.limit");}
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
    argc = split_args(b, &x, &y, &z);
    if (argc == 1) {
        FlatValue v;
        if (parse_value(ctx, x, &v) != 0 || num_arg(&v, &id) != 0 || !flat_http_request_live(ctx->http, id))
            return http_fail(ctx, "http.requestBody.id");
        s = &ctx->http->requests[id];
        return http_out_body_bytes(ctx, s->body, s->body_len, "http.requestBody.memory");
    }
    if (argc != 2 || parse_value(ctx, x, &a[0]) != 0 || parse_value(ctx, y, &a[1]) != 0 ||
        num_arg(&a[0], &id) != 0 || !flat_http_request_live(ctx->http, id) ||
        value_string(ctx, &a[1], &p, &n) != 0) {
        free(p);
        return http_fail(ctx, "http.requestBody.args");
    }
    s = &ctx->http->requests[id];
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
    return http_out_body_bytes(ctx, s->response.body, s->response.body_len, "http.responseBody.memory");
}
int flat_native_http_timeout(FlatNativeCtx *ctx){FlatValue a[2];uint32_t id;double ms;if(get_args(ctx,a,2)!=0||num_arg(&a[0],&id)!=0||!flat_http_request_live(ctx->http,id)||a[1].tag!=VAL_NUMBER)return http_fail(ctx,"http.timeout.args");ms=flat_number_as_f64(&a[1]);if(ms<1||ms>600000)return http_fail(ctx,"http.timeout.range");ctx->http->requests[id].timeout_ms=(long)ms;return http_ok(ctx);}
int flat_native_http_send(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.send.id",&id);FlatHttpRequest r;if(!s)return -1;flat_http_response_free(&s->response);memset(&r,0,sizeof r);r.method=s->method;r.url=s->url;r.body=s->body;r.body_len=s->body_len;r.headers=(const char *const *)s->headers;r.header_count=s->header_count;r.timeout_ms=s->timeout_ms;r.max_body_bytes=s->max_body_bytes;flat_http_perform(&r,&s->response);s->sent=1;return http_ok(ctx);}
int flat_native_http_status(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.status.id",&id);if(!s||!s->sent)return http_fail(ctx,"http.status.not_sent");http_ok(ctx);return flat_value_set_number(ctx->out,(double)s->response.status);}
int flat_native_http_error(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.error.id",&id);uint32_t ref;size_t n;if(!s||!s->sent)return http_fail(ctx,"http.error.not_sent");n=strlen(s->response.error);if(n<=FLAT_INLINE_STR_MAX){http_ok(ctx);return flat_value_set_str_inline(ctx->out,s->response.error,n);}if(flat_pool_alloc_copy(ctx->pool,s->response.error,n,&ref)!=0)return http_fail(ctx,"http.error.memory");http_ok(ctx);return flat_value_set_str_ref(ctx->out,ref);}
int flat_native_http_error_code(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.errorCode.id",&id);if(!s||!s->sent)return http_fail(ctx,"http.errorCode.not_sent");http_ok(ctx);return flat_value_set_number(ctx->out,(double)s->response.error_code);}
int flat_native_http_close(FlatNativeCtx *ctx){uint32_t id;FlatHttpRequestSlot *s=resolve(ctx,"http.close.id",&id);size_t i;if(!s)return -1;free(s->method);free(s->url);free(s->body);for(i=0;i<s->header_count;i++)free(s->headers[i]);free(s->headers);flat_http_response_free(&s->response);memset(s,0,sizeof *s);return http_ok(ctx);}


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
    char keys[7][FLAT_OBJ_KEY_MAX + 1];
    FlatValue vals[7];
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
        flat_value_set_number(&vals[6], (double)s->body_len) != 0)
        return http_fail(ctx, "http.requestInfo.memory");
    snprintf(keys[0], sizeof keys[0], "method");
    snprintf(keys[1], sizeof keys[1], "url");
    snprintf(keys[2], sizeof keys[2], "headers");
    snprintf(keys[3], sizeof keys[3], "timeoutMs");
    snprintf(keys[4], sizeof keys[4], "maxBody");
    snprintf(keys[5], sizeof keys[5], "sent");
    snprintf(keys[6], sizeof keys[6], "bodyLen");
    /* flat_obj_build pins vals (incl. headers object) before allocating the root. */
    if (flat_obj_build(ctx->coll, ctx->pool, keys, vals, 7, &obj_id) != 0)
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
