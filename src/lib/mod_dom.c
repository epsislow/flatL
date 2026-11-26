#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_dom_runtime.h"
#include "flat_native_capability.h"
#include "flat_value.h"
#include "flat_number.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#ifdef FLAT_DOM_BUNDLED
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
/* Upstream amalgamation: ptrdiff_t vs unsigned (e.g. end - pos < rep_len). */
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../../external/lexbor/lexbor.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#else
#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/selectors/selectors.h>
#include <lexbor/tag/tag.h>
#endif

#define FLAT_DOM_MAX_DOCUMENTS 64u
#define FLAT_DOM_NODE_SHIFT 20u
#define FLAT_DOM_NODE_MASK ((1u << FLAT_DOM_NODE_SHIFT) - 1u)
#define FLAT_DOM_MAX_NODES 100000u
#define FLAT_DOM_MAX_INPUT (16u * 1024u * 1024u)

typedef struct {
    lxb_html_document_t *document;
    lxb_dom_node_t **nodes;
    size_t node_count;
    size_t node_cap;
    uint32_t token;
    int live;
} FlatDomDocument;

struct FlatDomRuntime {
    FlatDomDocument docs[FLAT_DOM_MAX_DOCUMENTS];
    uint32_t next_token;
};

static int dom_fail(FlatNativeCtx *ctx, const char *why) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_ERR;
    if (ctx && why) snprintf(ctx->panic_detail, sizeof ctx->panic_detail, "%s", why);
    return -1;
}
static int dom_ok(FlatNativeCtx *ctx) {
    if (ctx && ctx->io_status) *ctx->io_status = FLAT_IO_OK;
    return 0;
}
struct FlatDomRuntime *flat_dom_runtime_create(void) {
    return (struct FlatDomRuntime *)calloc(1, sizeof(struct FlatDomRuntime));
}
static void dom_doc_clear(FlatDomDocument *doc) {
    if (!doc) return;
    if (doc->document) lxb_html_document_destroy(doc->document);
    free(doc->nodes);
    memset(doc, 0, sizeof(*doc));
}
void flat_dom_runtime_destroy(struct FlatDomRuntime *runtime) {
    size_t i;
    if (!runtime) return;
    /* DOM handlers and VM teardown share the same process-wide Lexbor gate. */
    if (flat_native_module_gate_enter("dom") != 0) return;
    for (i = 0; i < FLAT_DOM_MAX_DOCUMENTS; ++i) dom_doc_clear(&runtime->docs[i]);
    free(runtime);
    (void)flat_native_module_gate_leave("dom");
}
static int dom_split_args(char *s, char **out, int max, int *argc) {
    int n = 0, depth = 0;
    char quote = 0;
    char *start = s, *p;
    if (!s || !out || !argc) return -1;
    for (p = s; ; ++p) {
        char c = *p;
        if (quote) {
            if (c == quote && (p == s || p[-1] != '\\')) quote = 0;
        } else if (c == '\'' || c == '"') quote = c;
        else if (c == '(' || c == '[' || c == '{') depth++;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0) depth--;
        else if ((c == ',' && depth == 0) || c == '\0') {
            if (n >= max) return -1;
            if (c) *p = '\0';
            out[n++] = flat_native_trim(start);
            if (!c) break;
            start = p + 1;
        }
        if (!c) break;
    }
    *argc = n;
    return 0;
}
static int dom_get_string(FlatNativeCtx *ctx, const char *expr, char **out, size_t *len) {
    FlatValue v;
    char *buf;
    size_t cap = FLAT_DOM_MAX_INPUT + 1;
    if (!ctx || !expr || !out || !len || flat_native_parse_rhs(ctx, expr, &v) != 0) return -1;
    buf = (char *)malloc(cap);
    if (!buf) { flat_native_parse_rhs_release(ctx, &v); return -1; }
    if (flat_value_get_cstr(&v, ctx->pool, buf, cap) != 0) {
        free(buf);
        flat_native_parse_rhs_release(ctx, &v);
        return -1;
    }
    *len = strlen(buf);
    *out = buf;
    flat_native_parse_rhs_release(ctx, &v);
    return 0;
}
static int dom_make_string(FlatNativeCtx *ctx, const lxb_char_t *data, size_t len) {
    uint32_t ref;
    if (!ctx || !ctx->out || (!data && len)) return -1;
    if (len <= FLAT_INLINE_STR_MAX)
        return flat_value_set_str_inline(ctx->out, data ? (const char *)data : "", len);
    if (flat_pool_alloc_copy(ctx->pool, data, len, &ref) != 0) return -1;
    return flat_value_set_str_ref(ctx->out, ref);
}
static FlatDomDocument *dom_find_document(FlatNativeCtx *ctx, uint32_t token) {
    size_t i;
    if (!ctx || !ctx->dom || token == 0) return NULL;
    for (i = 0; i < FLAT_DOM_MAX_DOCUMENTS; ++i) {
        if (ctx->dom->docs[i].live && ctx->dom->docs[i].token == token)
            return &ctx->dom->docs[i];
    }
    return NULL;
}
static int dom_doc_id(FlatNativeCtx *ctx, const char *expr, uint32_t *out) {
    FlatValue v;
    double d;
    uint32_t token;
    if (flat_native_parse_rhs(ctx, expr, &v) != 0) return -1;
    if (v.tag != VAL_NUMBER) { flat_native_parse_rhs_release(ctx, &v); return -1; }
    d = flat_number_as_f64(&v);
    flat_native_parse_rhs_release(ctx, &v);
    if (d < 1.0 || d > FLAT_DOM_NODE_MASK || d != (double)(uint32_t)d) return -1;
    token = (uint32_t)d;
    if (!dom_find_document(ctx, token)) return -1;
    *out = token;
    return 0;
}
static int dom_node_ref(FlatNativeCtx *ctx, const char *expr, lxb_dom_node_t **out) {
    FlatValue v;
    double d;
    uint64_t handle;
    uint32_t token, index;
    FlatDomDocument *doc;
    if (flat_native_parse_rhs(ctx, expr, &v) != 0) return -1;
    if (v.tag != VAL_NUMBER) { flat_native_parse_rhs_release(ctx, &v); return -1; }
    d = flat_number_as_f64(&v);
    flat_native_parse_rhs_release(ctx, &v);
    if (d < 1.0 || d > 1099511627775.0 || d != (double)(uint64_t)d) return -1;
    handle = (uint64_t)d;
    token = (uint32_t)(handle >> FLAT_DOM_NODE_SHIFT);
    index = (uint32_t)(handle & FLAT_DOM_NODE_MASK);
    if (token == 0 || index == 0) return -1;
    doc = dom_find_document(ctx, token);
    if (!doc || (size_t)index > doc->node_count) return -1;
    *out = doc->nodes[index - 1];
    return *out ? 0 : -1;
}
static int dom_node_handle(FlatDomDocument *doc, uint32_t doc_id,
                           lxb_dom_node_t *node, uint64_t *handle) {
    size_t i;
    lxb_dom_node_t **next;
    size_t cap;
    for (i = 0; i < doc->node_count; ++i) {
        if (doc->nodes[i] == node) {
            *handle = ((uint64_t)doc_id << FLAT_DOM_NODE_SHIFT) | (uint64_t)(i + 1);
            return 0;
        }
    }
    if (doc->node_count >= FLAT_DOM_MAX_NODES || doc->node_count >= FLAT_DOM_NODE_MASK) return -1;
    if (doc->node_count == doc->node_cap) {
        cap = doc->node_cap ? doc->node_cap * 2 : 64;
        if (cap > FLAT_DOM_MAX_NODES) cap = FLAT_DOM_MAX_NODES;
        next = (lxb_dom_node_t **)realloc(doc->nodes, cap * sizeof(*next));
        if (!next) return -1;
        doc->nodes = next;
        doc->node_cap = cap;
    }
    doc->nodes[doc->node_count] = node;
    doc->node_count++;
    *handle = ((uint64_t)doc_id << FLAT_DOM_NODE_SHIFT) | (uint64_t)doc->node_count;
    return 0;
}
typedef struct {
    FlatNativeCtx *ctx;
    FlatDomDocument *doc;
    uint32_t doc_id;
    uint32_t list_id;
    int failed;
} FlatDomSelectCtx;
static lxb_status_t dom_select_cb(lxb_dom_node_t *node,
                                  lxb_css_selector_specificity_t specificity,
                                  void *opaque) {
    FlatDomSelectCtx *state = (FlatDomSelectCtx *)opaque;
    uint64_t handle;
    uint32_t next_id;
    FlatValue value;
    (void)specificity;
    if (!state || state->failed || dom_node_handle(state->doc, state->doc_id, node, &handle) != 0 ||
        flat_value_set_number(&value, (double)handle) != 0 ||
        flat_list_push_copy(state->ctx->coll, state->ctx->pool, state->list_id,
                            &value, &next_id) != 0) {
        if (state) state->failed = 1;
        return LXB_STATUS_ERROR;
    }
    state->list_id = next_id;
    return LXB_STATUS_OK;
}
int flat_native_dom_parse_html(FlatNativeCtx *ctx) {
    char buf[4096], *args[1], *html = NULL;
    int argc;
    size_t len, i;
    FlatDomDocument *doc = NULL;
    lxb_status_t status;
    if (!ctx || !ctx->dom || !ctx->out) return dom_fail(ctx, "dom.parseHtml");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 1, &argc) != 0 || argc != 1 ||
        dom_get_string(ctx, args[0], &html, &len) != 0 || len > FLAT_DOM_MAX_INPUT) {
        free(html);
        return dom_fail(ctx, "dom.parseHtml.args");
    }
    for (i = 0; i < FLAT_DOM_MAX_DOCUMENTS; ++i) {
        if (!ctx->dom->docs[i].live) { doc = &ctx->dom->docs[i]; break; }
    }
    if (!doc) { free(html); return dom_fail(ctx, "dom.parseHtml.limit"); }
    doc->document = lxb_html_document_create();
    if (!doc->document) { free(html); return dom_fail(ctx, "dom.parseHtml.alloc"); }
    status = lxb_html_document_parse(doc->document, (const lxb_char_t *)html, len);
    free(html);
    if (status != LXB_STATUS_OK) {
        dom_doc_clear(doc);
        return dom_fail(ctx, "dom.parseHtml.parse");
    }
    doc->live = 1;
    ctx->dom->next_token = (ctx->dom->next_token % FLAT_DOM_NODE_MASK) + 1;
    doc->token = ctx->dom->next_token;
    return dom_ok(ctx) + flat_value_set_number(ctx->out, (double)doc->token);
}
int flat_native_dom_select(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *selector = NULL;
    int argc;
    size_t len;
    uint32_t doc_id, list_id;
    FlatDomDocument *doc;
    lxb_dom_node_t *root = NULL;
    FlatValue target_value;
    double target_number;
    uint64_t target_handle;
    lxb_css_parser_t *parser = NULL;
    lxb_selectors_t *selectors = NULL;
    lxb_css_selector_list_t *list = NULL;
    FlatDomSelectCtx state;
    lxb_status_t status;
    int rc = -1;
    if (!ctx || !ctx->dom || !ctx->out) return dom_fail(ctx, "dom.select");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        flat_native_parse_rhs(ctx, args[0], &target_value) != 0)
        goto done;
    if (target_value.tag != VAL_NUMBER) { flat_native_parse_rhs_release(ctx, &target_value); goto done; }
    if (dom_get_string(ctx, args[1], &selector, &len) != 0 || len == 0 || len > 4096) {
        flat_native_parse_rhs_release(ctx, &target_value); goto done;
    }
    target_number = flat_number_as_f64(&target_value);
    flat_native_parse_rhs_release(ctx, &target_value);
    if (target_number < 1.0 || target_number != (double)(uint64_t)target_number) goto done;
    target_handle = (uint64_t)target_number;
    if (target_handle <= FLAT_DOM_NODE_MASK) {
        doc_id = (uint32_t)target_handle;
        doc = dom_find_document(ctx, doc_id);
        if (!doc) goto done;
        root = lxb_dom_interface_node(doc->document);
    } else {
        uint32_t token = (uint32_t)(target_handle >> FLAT_DOM_NODE_SHIFT);
        doc = dom_find_document(ctx, token);
        if (!doc || dom_node_ref(ctx, args[0], &root) != 0) goto done;
        doc_id = token;
    }
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0) goto done;
    parser = lxb_css_parser_create();
    selectors = lxb_selectors_create();
    if (!parser || !selectors || lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK ||
        lxb_selectors_init(selectors) != LXB_STATUS_OK) goto done;
    list = lxb_css_selectors_parse(parser, (const lxb_char_t *)selector, len);
    if (!list) goto done;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx; state.doc = doc; state.doc_id = doc_id; state.list_id = list_id;
    status = lxb_selectors_find(selectors, root,
                                list, dom_select_cb, &state);
    if (status != LXB_STATUS_OK || state.failed) goto done;
    rc = flat_value_set_list(ctx->out, state.list_id);
done:
    if (list) lxb_css_selector_list_destroy_memory(list);
    if (selectors) lxb_selectors_destroy(selectors, true);
    if (parser) lxb_css_parser_destroy(parser, true);
    free(selector);
    if (rc != 0) return dom_fail(ctx, "dom.select");
    return dom_ok(ctx);
}
int flat_native_dom_text(FlatNativeCtx *ctx) {
    char buf[1024], *args[1];
    int argc;
    lxb_dom_node_t *node;
    lxb_char_t *text;
    size_t len = 0;
    int rc;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.text");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 1, &argc) != 0 || argc != 1 ||
        dom_node_ref(ctx, args[0], &node) != 0) return dom_fail(ctx, "dom.text.args");
    text = lxb_dom_node_text_content(node, &len);
    if (!text && len) return dom_fail(ctx, "dom.text");
    rc = dom_make_string(ctx, text, len);
    if (rc != 0) return dom_fail(ctx, "dom.text.alloc");
    return dom_ok(ctx);
}
static int dom_args(FlatNativeCtx *ctx, char *buf, char **args, int max, int wanted);
static int dom_attr_value(lxb_dom_node_t *node, const char *name,
                          const lxb_char_t **value, size_t *len);
static int dom_has_attr(lxb_dom_node_t *node, const char *name);
static int dom_node_named(lxb_dom_node_t *node, const char *name);

int flat_native_dom_text_content(FlatNativeCtx *ctx) {
    /* textContent is intentionally an alias of dom.text(): both return all
       descendant text, without visibility filtering or whitespace changes. */
    return flat_native_dom_text(ctx);
}
int flat_native_dom_id(FlatNativeCtx *ctx) {
    char buf[4096], *args[1];
    lxb_dom_node_t *node;
    const lxb_char_t *value = NULL;
    size_t len = 0;
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.id.args");
    value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                          (const lxb_char_t *)"id", 2, &len);
    if (dom_make_string(ctx, value, value ? len : 0) != 0)
        return dom_fail(ctx, "dom.id.alloc");
    return dom_ok(ctx);
}

typedef struct {
    lxb_char_t *data;
    size_t len;
    size_t cap;
    int pending_space;
    int pending_newline;
} FlatDomInnerText;
static int dom_inner_text_block(const lxb_dom_node_t *node) {
    static const char *const names[] = {
        "address", "article", "aside", "blockquote", "br", "dd", "div",
        "dl", "dt", "fieldset", "figcaption", "figure", "footer", "form",
        "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li",
        "main", "nav", "ol", "p", "pre", "section", "table", "tbody",
        "td", "tfoot", "th", "thead", "tr", "ul"
    };
    size_t i;
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return 0;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (dom_node_named((lxb_dom_node_t *)node, names[i])) return 1;
    return 0;
}
static int dom_inner_text_hidden(lxb_dom_node_t *node) {
    const lxb_char_t *type = NULL;
    size_t len = 0;
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return 0;
    if (dom_has_attr(node, "hidden")) return 1;
    if (dom_node_named(node, "script") || dom_node_named(node, "style") ||
        dom_node_named(node, "template") || dom_node_named(node, "noscript")) return 1;
    if (dom_node_named(node, "input")) {
        (void)dom_attr_value(node, "type", &type, &len);
        if (type && len == 6 && strncasecmp((const char *)type, "hidden", 6) == 0) return 1;
    }
    return 0;
}
static int dom_inner_text_reserve(FlatDomInnerText *out, size_t extra) {
    size_t need, cap;
    lxb_char_t *next;
    if (extra > SIZE_MAX - out->len - 1) return -1;
    need = out->len + extra + 1;
    if (need <= out->cap) return 0;
    cap = out->cap ? out->cap : 128;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) { cap = need; break; }
        cap *= 2;
    }
    next = (lxb_char_t *)realloc(out->data, cap);
    if (!next) return -1;
    out->data = next; out->cap = cap;
    return 0;
}
static int dom_inner_text_emit_space(FlatDomInnerText *out, int newline) {
    if (out->len == 0) return 0;
    if (newline) {
        while (out->len && (out->data[out->len - 1] == ' ' || out->data[out->len - 1] == '\t')) out->len--;
        if (out->len && out->data[out->len - 1] != '\n') {
            if (dom_inner_text_reserve(out, 1) != 0) return -1;
            out->data[out->len++] = '\n';
        }
    } else if (out->data[out->len - 1] != ' ' && out->data[out->len - 1] != '\n') {
        if (dom_inner_text_reserve(out, 1) != 0) return -1;
        out->data[out->len++] = ' ';
    }
    return 0;
}
static int dom_inner_text_walk(lxb_dom_node_t *node, FlatDomInnerText *out) {
    lxb_dom_node_t *child;
    if (!node) return 0;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_char_t *text;
        size_t len = 0, i;
        text = lxb_dom_node_text_content(node, &len);
        if (!text && len) return -1;
        for (i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)text[i];
            if (isspace(c)) { out->pending_space = 1; continue; }
            if (out->pending_newline) {
                if (dom_inner_text_emit_space(out, 1) != 0) return -1;
                out->pending_newline = 0;
            } else if (out->pending_space) {
                if (dom_inner_text_emit_space(out, 0) != 0) return -1;
            }
            out->pending_space = 0;
            if (dom_inner_text_reserve(out, 1) != 0) return -1;
            out->data[out->len++] = (lxb_char_t)c;
        }
        return 0;
    }
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT && node->type != LXB_DOM_NODE_TYPE_DOCUMENT) return 0;
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && dom_inner_text_hidden(node)) return 0;
    if (dom_inner_text_block(node)) out->pending_newline = 1;
    for (child = node->first_child; child; child = child->next)
        if (dom_inner_text_walk(child, out) != 0) return -1;
    if (dom_inner_text_block(node)) { out->pending_space = 0; out->pending_newline = 1; }
    return 0;
}
int flat_native_dom_inner_text(FlatNativeCtx *ctx) {
    char buf[4096], *args[1];
    lxb_dom_node_t *node;
    FlatDomInnerText out;
    int rc;
    memset(&out, 0, sizeof(out));
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.innerText.args");
    rc = dom_inner_text_walk(node, &out);
    while (out.len && (out.data[out.len - 1] == ' ' || out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\t')) out.len--;
    if (rc == 0) rc = dom_make_string(ctx, out.data, out.len);
    free(out.data);
    if (rc != 0) return dom_fail(ctx, "dom.innerText");
    return dom_ok(ctx);
}

typedef struct {
    lxb_char_t *data;
    size_t len;
    size_t cap;
    int failed;
} FlatDomSerializeBuffer;

static lxb_status_t dom_serialize_append(const lxb_char_t *data, size_t len, void *opaque) {
    FlatDomSerializeBuffer *out = (FlatDomSerializeBuffer *)opaque;
    lxb_char_t *next;
    size_t need, cap;
    if (!out || (!data && len) || len > SIZE_MAX - out->len - 1) return LXB_STATUS_ERROR;
    need = out->len + len + 1;
    if (need > out->cap) {
        cap = out->cap ? out->cap : 256;
        while (cap < need) {
            if (cap > SIZE_MAX / 2) { cap = need; break; }
            cap *= 2;
        }
        next = (lxb_char_t *)realloc(out->data, cap);
        if (!next) { out->failed = 1; return LXB_STATUS_ERROR_MEMORY_ALLOCATION; }
        out->data = next;
        out->cap = cap;
    }
    if (len) memcpy(out->data + out->len, data, len);
    out->len += len;
    out->data[out->len] = '\0';
    return LXB_STATUS_OK;
}

static int dom_serialize_node(FlatNativeCtx *ctx, lxb_dom_node_t *node, int inner) {
    FlatDomSerializeBuffer out;
    lxb_dom_node_t *child;
    lxb_status_t status = LXB_STATUS_OK;
    memset(&out, 0, sizeof(out));
    if (inner) {
        for (child = node->first_child; child; child = child->next) {
            status = lxb_html_serialize_tree_cb(child, dom_serialize_append, &out);
            if (status != LXB_STATUS_OK) break;
        }
    } else {
        status = lxb_html_serialize_tree_cb(node, dom_serialize_append, &out);
    }
    if (status == LXB_STATUS_OK && !out.failed)
        status = (lxb_status_t)(dom_make_string(ctx, out.data, out.len) == 0 ? LXB_STATUS_OK : LXB_STATUS_ERROR_MEMORY_ALLOCATION);
    free(out.data);
    if (status != LXB_STATUS_OK) return dom_fail(ctx, inner ? "dom.html" : "dom.outerHtml");
    return dom_ok(ctx);
}

int flat_native_dom_html(FlatNativeCtx *ctx) {
    char buf[4096], *args[1];
    lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.html.args");
    return dom_serialize_node(ctx, node, 1);
}

int flat_native_dom_outer_html(FlatNativeCtx *ctx) {
    char buf[4096], *args[1];
    lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.outerHtml.args");
    return dom_serialize_node(ctx, node, 0);
}

int flat_native_dom_attr(FlatNativeCtx *ctx) {
    char buf[2048], *args[2], *name = NULL;
    int argc;
    size_t name_len, value_len = 0;
    lxb_dom_node_t *node;
    const lxb_char_t *value;
    int rc;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.attr");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_node_ref(ctx, args[0], &node) != 0 ||
        dom_get_string(ctx, args[1], &name, &name_len) != 0 || name_len == 0 || name_len > 1024 ||
        node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
        free(name);
        return dom_fail(ctx, "dom.attr.args");
    }
    value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                          (const lxb_char_t *)name, name_len, &value_len);
    free(name);
    rc = dom_make_string(ctx, value, value ? value_len : 0);
    if (rc != 0) return dom_fail(ctx, "dom.attr.alloc");
    return dom_ok(ctx);
}

static int dom_value_string(FlatNativeCtx *ctx, FlatValue *out,
                            const lxb_char_t *data, size_t len) {
    uint32_t ref;
    if (len <= FLAT_INLINE_STR_MAX)
        return flat_value_set_str_inline(out, data ? (const char *)data : "", len);
    if (flat_pool_alloc_copy(ctx->pool, data, len, &ref) != 0) return -1;
    return flat_value_set_str_ref(out, ref);
}
static int dom_attr_value(lxb_dom_node_t *node, const char *name,
                          const lxb_char_t **value, size_t *len) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return -1;
    *value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                           (const lxb_char_t *)name, strlen(name), len);
    return 0;
}
static int dom_has_attr(lxb_dom_node_t *node, const char *name) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return 0;
    return lxb_dom_element_has_attribute(lxb_dom_interface_element(node),
                                         (const lxb_char_t *)name, strlen(name));
}
static int dom_node_named(lxb_dom_node_t *node, const char *name) {
    size_t len = 0;
    const lxb_char_t *actual;
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return 0;
    actual = lxb_tag_name_by_id(lxb_dom_element_tag_id(lxb_dom_interface_element(node)), &len);
    return actual && len == strlen(name) && memcmp(actual, name, len) == 0;
}
static int dom_make_option_value(FlatNativeCtx *ctx, lxb_dom_node_t *option,
                                 FlatValue *out) {
    const lxb_char_t *value = NULL;
    size_t len = 0;
    if (dom_attr_value(option, "value", &value, &len) != 0) return -1;
    if (!value) {
        value = lxb_dom_node_text_content(option, &len);
    }
    return dom_value_string(ctx, out, value, value ? len : 0);
}
static int dom_add_field(FlatNativeCtx *ctx,
                         char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1],
                         FlatValue values[FLAT_OBJ_MAX_KEYS],
                         int *count, const char *name, const FlatValue *value) {
    int i;
    uint32_t list_id, next_id;
    FlatValue list_value;
    for (i = 0; i < *count; ++i) {
        if (strcmp(keys[i], name) == 0) {
            if (values[i].tag == VAL_LIST) {
                list_id = values[i].u.coll_id;
            } else {
                if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0 ||
                    flat_list_push_copy(ctx->coll, ctx->pool, list_id, &values[i], &next_id) != 0)
                    return -1;
                list_id = next_id;
            }
            if (flat_list_push_copy(ctx->coll, ctx->pool, list_id, value, &next_id) != 0 ||
                flat_value_set_list(&list_value, next_id) != 0) return -1;
            values[i] = list_value;
            return 0;
        }
    }
    if (*count >= FLAT_OBJ_MAX_KEYS || strlen(name) > FLAT_OBJ_KEY_MAX) return -1;
    strcpy(keys[*count], name);
    values[*count] = *value;
    (*count)++;
    return 0;
}
typedef struct {
    FlatNativeCtx *ctx;
    int multiple;
    int has_selected;
    int has_fallback;
    uint32_t list_id;
    FlatValue selected_value;
    FlatValue fallback_value;
} FlatDomOptionState;
static int dom_form_options_walk(FlatDomOptionState *state, lxb_dom_node_t *root) {
    lxb_dom_node_t *node;
    for (node = root ? root->first_child : NULL; node; node = node->next) {
        if (dom_has_attr(node, "disabled")) continue;
        if (dom_node_named(node, "option")) {
            FlatValue item;
            int selected = dom_has_attr(node, "selected");
            if (dom_make_option_value(state->ctx, node, &item) != 0) return -1;
            if (state->multiple) {
                if (selected) {
                    uint32_t next_id;
                    if (flat_list_push_copy(state->ctx->coll, state->ctx->pool,
                                            state->list_id, &item, &next_id) != 0) return -1;
                    state->list_id = next_id;
                }
            } else if (selected) {
                state->selected_value = item;
                state->has_selected = 1;
            } else if (!state->has_fallback) {
                state->fallback_value = item;
                state->has_fallback = 1;
            }
        } else if (dom_form_options_walk(state, node) != 0) {
            return -1;
        }
    }
    return 0;
}
static int dom_form_option_fields(FlatNativeCtx *ctx, lxb_dom_node_t *select,
                                  FlatValue *out) {
    FlatDomOptionState state;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx;
    state.multiple = dom_has_attr(select, "multiple");
    if (state.multiple && flat_list_empty(ctx->coll, ctx->pool, &state.list_id) != 0) return -1;
    if (dom_form_options_walk(&state, select) != 0) return -1;
    if (state.multiple) return flat_value_set_list(out, state.list_id);
    if (state.has_selected) { *out = state.selected_value; return 0; }
    if (state.has_fallback) { *out = state.fallback_value; return 0; }
    return flat_value_set_str_inline(out, "", 0);
}
static int dom_form_walk(FlatNativeCtx *ctx, lxb_dom_node_t *root,
                         char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1],
                         FlatValue values[FLAT_OBJ_MAX_KEYS], int *count) {
    lxb_dom_node_t *node;
    for (node = root ? root->first_child : NULL; node; node = node->next) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && dom_has_attr(node, "disabled") &&
            dom_node_named(node, "fieldset")) {
            continue;
        }
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && !dom_has_attr(node, "disabled")) {
            const lxb_char_t *name_data = NULL;
            size_t name_len = 0;
            char name[FLAT_OBJ_KEY_MAX + 1];
            FlatValue value;
            int include = 1;
            if (dom_node_named(node, "input")) {
                const lxb_char_t *type_data = NULL;
                size_t type_len = 0;
                char type[32] = "text";
                (void)dom_attr_value(node, "name", &name_data, &name_len);
                if (dom_attr_value(node, "type", &type_data, &type_len) == 0 && type_data) {
                    size_t n = type_len < sizeof(type) - 1 ? type_len : sizeof(type) - 1;
                    for (size_t ti = 0; ti < n; ++ti) {
                        type[ti] = (char)tolower((unsigned char)type_data[ti]);
                    }
                    type[n] = '\0';
                }
                if (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)
                    include = dom_has_attr(node, "checked");
                if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 ||
                    strcmp(type, "reset") == 0 || strcmp(type, "image") == 0)
                    include = 0;
                if (include) {
                    const lxb_char_t *data = NULL; size_t len = 0;
                    (void)dom_attr_value(node, "value", &data, &len);
                    if (!data && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) {
                        data = (const lxb_char_t *)"on";
                        len = 2;
                    }
                    if (dom_value_string(ctx, &value, data, data ? len : 0) != 0) return -1;
                }
            } else if (dom_node_named(node, "textarea")) {
                (void)dom_attr_value(node, "name", &name_data, &name_len);
                size_t text_len = 0;
                lxb_char_t *data = lxb_dom_node_text_content(node, &text_len);
                if (dom_value_string(ctx, &value, data, data ? text_len : 0) != 0) return -1;
            } else if (dom_node_named(node, "select")) {
                (void)dom_attr_value(node, "name", &name_data, &name_len);
                if (dom_form_option_fields(ctx, node, &value) != 0) return -1;
            } else {
                include = 0;
            }
            if (include && name_data && name_len > 0) {
                if (name_len > FLAT_OBJ_KEY_MAX) return -1;
                memcpy(name, name_data, name_len); name[name_len] = '\0';
                if (dom_add_field(ctx, keys, values, count, name, &value) != 0) return -1;
            }
        }
        if (dom_form_walk(ctx, node, keys, values, count) != 0) return -1;
    }
    return 0;
}
int flat_native_dom_form_fields(FlatNativeCtx *ctx) {
    char buf[1024], *args[1];
    int argc, count = 0;
    lxb_dom_node_t *form;
    char keys[FLAT_OBJ_MAX_KEYS][FLAT_OBJ_KEY_MAX + 1];
    FlatValue values[FLAT_OBJ_MAX_KEYS];
    uint32_t object_id;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.formFields");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 1, &argc) != 0 || argc != 1 ||
        dom_node_ref(ctx, args[0], &form) != 0 || form->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.formFields.args");
    if (dom_form_walk(ctx, form, keys, values, &count) != 0 ||
        flat_obj_build(ctx->coll, ctx->pool, keys, values, count, &object_id) != 0)
        return dom_fail(ctx, "dom.formFields");
    return dom_ok(ctx) + flat_value_set_object(ctx->out, object_id);
}

int flat_native_dom_close(FlatNativeCtx *ctx) {
    char buf[1024], *args[1];
    int argc;
    uint32_t id;
    if (!ctx || !ctx->dom) return dom_fail(ctx, "dom.close");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 1, &argc) != 0 || argc != 1 ||
        dom_doc_id(ctx, args[0], &id) != 0) return dom_fail(ctx, "dom.close.args");
    dom_doc_clear(&ctx->dom->docs[id - 1]);
    return dom_ok(ctx) + flat_value_set_bool(ctx->out, 1);
}

/* DOM navigation and element metadata. */
static int dom_args(FlatNativeCtx *ctx, char *buf, char **args, int max, int wanted) {
    int argc = 0;
    snprintf(buf, 4096, "%s", ctx && ctx->inner ? ctx->inner : "");
    return dom_split_args(buf, args, max, &argc) == 0 && argc == wanted ? 0 : -1;
}
static int dom_node_result(FlatNativeCtx *ctx, lxb_dom_node_t *node) {
    uint64_t handle;
    FlatDomDocument *doc = NULL;
    lxb_dom_node_t *ancestor;
    size_t i;
    if (!node) return flat_value_set_null(ctx->out);
    for (ancestor = node; ancestor && !doc; ancestor = ancestor->parent) {
        for (i = 0; i < FLAT_DOM_MAX_DOCUMENTS && !doc; ++i) {
            FlatDomDocument *candidate = &ctx->dom->docs[i];
            size_t j;
            if (!candidate->live) continue;
            for (j = 0; j < candidate->node_count; ++j) {
                if (candidate->nodes[j] == ancestor) { doc = candidate; break; }
            }
        }
    }
    if (!doc || dom_node_handle(doc, doc->token, node, &handle) != 0) return -1;
    return flat_value_set_number(ctx->out, (double)handle);
}
static int dom_element_arg(FlatNativeCtx *ctx, char *buf, char **args,
                           int max, int wanted, lxb_dom_node_t **node) {
    return dom_args(ctx, buf, args, max, wanted) == 0 &&
           dom_node_ref(ctx, args[0], node) == 0 &&
           (*node)->type == LXB_DOM_NODE_TYPE_ELEMENT ? 0 : -1;
}
int flat_native_dom_tag(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node;
    const lxb_char_t *name; size_t len = 0;
    if (!ctx || !ctx->out || dom_element_arg(ctx, buf, args, 1, 1, &node) != 0)
        return dom_fail(ctx, "dom.tag.args");
    name = lxb_tag_name_by_id(lxb_dom_element_tag_id(lxb_dom_interface_element(node)), &len);
    if (!name || dom_make_string(ctx, name, len) != 0) return dom_fail(ctx, "dom.tag");
    return dom_ok(ctx);
}
int flat_native_dom_has_attr(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *name = NULL; int argc;
    size_t len; lxb_dom_node_t *node;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.hasAttr");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        dom_get_string(ctx, args[1], &name, &len) != 0 || len == 0 || len > 1024) {
        free(name); return dom_fail(ctx, "dom.hasAttr.args");
    }
    { int found = lxb_dom_element_has_attribute(lxb_dom_interface_element(node),
                                                 (const lxb_char_t *)name, len);
      free(name); return dom_ok(ctx) + flat_value_set_bool(ctx->out, found); }
}
int flat_native_dom_classes(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node;
    const lxb_char_t *value; size_t len = 0, i = 0; uint32_t list_id, next_id;
    if (!ctx || !ctx->out || dom_element_arg(ctx, buf, args, 1, 1, &node) != 0)
        return dom_fail(ctx, "dom.classes.args");
    value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                          (const lxb_char_t *)"class", 5, &len);
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0) return dom_fail(ctx, "dom.classes");
    while (value && i < len) {
        size_t start, n; FlatValue item; uint32_t ref;
        while (i < len && isspace((unsigned char)value[i])) i++;
        start = i;
        while (i < len && !isspace((unsigned char)value[i])) i++;
        n = i - start;
        if (!n) continue;
        if (n <= FLAT_INLINE_STR_MAX) {
            if (flat_value_set_str_inline(&item, (const char *)value + start, n) != 0) return dom_fail(ctx, "dom.classes");
        } else {
            if (flat_pool_alloc_copy(ctx->pool, value + start, n, &ref) != 0 ||
                flat_value_set_str_ref(&item, ref) != 0) return dom_fail(ctx, "dom.classes");
        }
        if (flat_list_push_copy(ctx->coll, ctx->pool, list_id, &item, &next_id) != 0) return dom_fail(ctx, "dom.classes");
        list_id = next_id;
    }
    return dom_ok(ctx) + flat_value_set_list(ctx->out, list_id);
}
int flat_native_dom_has_class(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *wanted = NULL; int argc, found = 0;
    size_t wanted_len, len = 0, i = 0; lxb_dom_node_t *node;
    const lxb_char_t *value;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.hasClass");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        dom_get_string(ctx, args[1], &wanted, &wanted_len) != 0 || wanted_len == 0) {
        free(wanted); return dom_fail(ctx, "dom.hasClass.args");
    }
    value = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
                                          (const lxb_char_t *)"class", 5, &len);
    while (value && i < len) {
        size_t start, n;
        while (i < len && isspace((unsigned char)value[i])) i++;
        start = i;
        while (i < len && !isspace((unsigned char)value[i])) i++;
        n = i - start;
        if (n == wanted_len && memcmp(value + start, wanted, n) == 0) { found = 1; break; }
    }
    free(wanted);
    return dom_ok(ctx) + flat_value_set_bool(ctx->out, found);
}
static int dom_sibling_result(FlatNativeCtx *ctx, int which) {
    char buf[4096], *args[1]; lxb_dom_node_t *node, *result = NULL;
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 || dom_node_ref(ctx, args[0], &node) != 0)
        return dom_fail(ctx, "dom.navigation.args");
    if (which == 0) result = node->parent;
    else if (which == 1) result = node->next;
    else result = node->prev;
    if (result && result->type != LXB_DOM_NODE_TYPE_ELEMENT) result = NULL;
    if (dom_node_result(ctx, result) != 0) return dom_fail(ctx, "dom.navigation");
    return dom_ok(ctx);
}
int flat_native_dom_parent(FlatNativeCtx *ctx) { return dom_sibling_result(ctx, 0); }
int flat_native_dom_next(FlatNativeCtx *ctx) { return dom_sibling_result(ctx, 1); }
int flat_native_dom_prev(FlatNativeCtx *ctx) { return dom_sibling_result(ctx, 2); }
int flat_native_dom_children(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node, *child;
    uint32_t list_id, next_id; FlatValue item;
    if (!ctx || !ctx->out || dom_args(ctx, buf, args, 1, 1) != 0 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT)
        return dom_fail(ctx, "dom.children.args");
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0) return dom_fail(ctx, "dom.children");
    for (child = node->first_child; child; child = child->next) {
        if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        if (dom_node_result(ctx, child) != 0) return dom_fail(ctx, "dom.children");
        item = *ctx->out;
        if (flat_list_push_copy(ctx->coll, ctx->pool, list_id, &item, &next_id) != 0)
            return dom_fail(ctx, "dom.children");
        list_id = next_id;
    }
    return dom_ok(ctx) + flat_value_set_list(ctx->out, list_id);
}
typedef struct { int matched; } FlatDomMatchState;
static lxb_status_t dom_match_cb(lxb_dom_node_t *node, lxb_css_selector_specificity_t specificity, void *opaque) {
    FlatDomMatchState *state = (FlatDomMatchState *)opaque;
    (void)node; (void)specificity; if (state) state->matched = 1; return LXB_STATUS_OK;
}
int flat_native_dom_closest(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *selector = NULL; int argc, rc = -1;
    size_t len; lxb_dom_node_t *node; lxb_css_parser_t *parser = NULL;
    lxb_selectors_t *selectors = NULL; lxb_css_selector_list_t *list = NULL;
    FlatDomMatchState state; lxb_status_t status;
    if (!ctx || !ctx->dom || !ctx->out) return dom_fail(ctx, "dom.closest");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        dom_get_string(ctx, args[1], &selector, &len) != 0 || len == 0 || len > 4096) goto done;
    parser = lxb_css_parser_create(); selectors = lxb_selectors_create();
    if (!parser || !selectors || lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK ||
        lxb_selectors_init(selectors) != LXB_STATUS_OK) goto done;
    list = lxb_css_selectors_parse(parser, (const lxb_char_t *)selector, len);
    if (!list) goto done;
    for (; node && node->type == LXB_DOM_NODE_TYPE_ELEMENT; node = node->parent) {
        memset(&state, 0, sizeof(state));
        status = lxb_selectors_match_node(selectors, node, list, dom_match_cb, &state);
        if (status != LXB_STATUS_OK) goto done;
        if (state.matched) { if (dom_node_result(ctx, node) != 0) goto done; rc = 0; break; }
    }
    if (rc != 0) { if (flat_value_set_null(ctx->out) != 0) goto done; rc = 0; }
done:
    if (list) lxb_css_selector_list_destroy_memory(list);
    if (selectors) lxb_selectors_destroy(selectors, true);
    if (parser) lxb_css_parser_destroy(parser, true);
    free(selector);
    if (rc != 0) return dom_fail(ctx, "dom.closest");
    return dom_ok(ctx);
}

/* Form-control inspection and data-* helpers. These expose the parsed HTML
 * attributes/default state; there is no browser layout or live interaction. */
static int dom_control_disabled(lxb_dom_node_t *node) {
    lxb_dom_node_t *p;
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return 0;
    if (dom_has_attr(node, "disabled")) return 1;
    /* Disabled optgroups disable their option descendants. */
    if (dom_node_named(node, "option")) {
        for (p = node->parent; p; p = p->parent) {
            if (dom_node_named(p, "optgroup")) return dom_has_attr(p, "disabled");
            if (dom_node_named(p, "select")) break;
        }
    }
    /* A disabled fieldset disables descendants, except descendants of its
       first legend. */
    for (p = node->parent; p; p = p->parent) {
        if (dom_node_named(p, "fieldset") && dom_has_attr(p, "disabled")) {
            lxb_dom_node_t *child;
            for (child = p->first_child; child; child = child->next) {
                if (dom_node_named(child, "legend")) {
                    lxb_dom_node_t *a;
                    for (a = node; a && a != p; a = a->parent) if (a == child) break;
                    if (a == child) return 0;
                    break;
                }
            }
            return 1;
        }
    }
    return 0;
}
static int dom_input_type_value(lxb_dom_node_t *node, char out[32]) {
    const lxb_char_t *data = NULL; size_t len = 0, i, n;
    if (!dom_node_named(node, "input")) return -1;
    (void)dom_attr_value(node, "type", &data, &len);
    if (!data || len == 0) { strcpy(out, "text"); return 0; }
    n = len < 31 ? len : 31;
    for (i = 0; i < n; ++i) out[i] = (char)tolower((unsigned char)data[i]);
    out[n] = '\0';
    return 0;
}
static int dom_option_list_walk(FlatNativeCtx *ctx, lxb_dom_node_t *root,
                                uint32_t *list_id, int selected_only,
                                int *has_selected, int *has_fallback,
                                lxb_dom_node_t **fallback) {
    lxb_dom_node_t *node;
    for (node = root ? root->first_child : NULL; node; node = node->next) {
        if (dom_node_named(node, "option")) {
            int selected = dom_has_attr(node, "selected");
            if (selected) *has_selected = 1;
            if (!*has_fallback && !dom_control_disabled(node)) { *has_fallback = 1; *fallback = node; }
            if (!selected_only || selected) {
                FlatValue item; uint32_t next_id;
                if (dom_node_result(ctx, node) != 0) return -1;
                item = *ctx->out;
                if (flat_list_push_copy(ctx->coll, ctx->pool, *list_id, &item, &next_id) != 0) return -1;
                *list_id = next_id;
            }
        } else if (dom_option_list_walk(ctx, node, list_id, selected_only,
                                        has_selected, has_fallback, fallback) != 0) return -1;
    }
    return 0;
}
static int dom_control_arg(FlatNativeCtx *ctx, char *buf, char **args, int wanted,
                           lxb_dom_node_t **node) {
    return dom_args(ctx, buf, args, wanted, wanted) == 0 &&
           dom_node_ref(ctx, args[0], node) == 0 &&
           (*node)->type == LXB_DOM_NODE_TYPE_ELEMENT ? 0 : -1;
}
int flat_native_dom_data(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *name = NULL, attr[1029]; int argc;
    size_t len = 0, i; lxb_dom_node_t *node; const lxb_char_t *value = NULL;
    if (!ctx || !ctx->out) return dom_fail(ctx, "dom.data");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_node_ref(ctx, args[0], &node) != 0 || node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        dom_get_string(ctx, args[1], &name, &len) != 0 || len == 0 || len > 1024) {
        free(name); return dom_fail(ctx, "dom.data.args");
    }
    if (len >= 5 && memcmp(name, "data-", 5) == 0) snprintf(attr, sizeof(attr), "%s", name);
    else snprintf(attr, sizeof(attr), "data-%s", name);
    for (i = 0; attr[i]; ++i) attr[i] = (char)tolower((unsigned char)attr[i]);
    (void)dom_attr_value(node, attr, &value, &len);
    free(name);
    if (dom_make_string(ctx, value, value ? len : 0) != 0) return dom_fail(ctx, "dom.data.alloc");
    return dom_ok(ctx);
}
int flat_native_dom_input_type(FlatNativeCtx *ctx) {
    char buf[4096], *args[1], type[32]; lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0 ||
        dom_input_type_value(node, type) != 0) return dom_fail(ctx, "dom.inputType.args");
    if (dom_make_string(ctx, (const lxb_char_t *)type, strlen(type)) != 0) return dom_fail(ctx, "dom.inputType");
    return dom_ok(ctx);
}
int flat_native_dom_checked(FlatNativeCtx *ctx) {
    char buf[4096], *args[1], type[32]; lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0 ||
        dom_input_type_value(node, type) != 0 ||
        (strcmp(type, "checkbox") != 0 && strcmp(type, "radio") != 0)) return dom_fail(ctx, "dom.checked.args");
    return dom_ok(ctx) + flat_value_set_bool(ctx->out, dom_has_attr(node, "checked"));
}
int flat_native_dom_disabled(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0) return dom_fail(ctx, "dom.disabled.args");
    return dom_ok(ctx) + flat_value_set_bool(ctx->out, dom_control_disabled(node));
}
int flat_native_dom_required(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0) return dom_fail(ctx, "dom.required.args");
    return dom_ok(ctx) + flat_value_set_bool(ctx->out, dom_has_attr(node, "required"));
}
int flat_native_dom_options(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node, *fallback = NULL;
    uint32_t list_id; int selected = 0, has_fallback = 0;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0 || !dom_node_named(node, "select"))
        return dom_fail(ctx, "dom.options.args");
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0 ||
        dom_option_list_walk(ctx, node, &list_id, 0, &selected, &has_fallback, &fallback) != 0)
        return dom_fail(ctx, "dom.options");
    return dom_ok(ctx) + flat_value_set_list(ctx->out, list_id);
}
int flat_native_dom_selected_options(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node, *fallback = NULL;
    uint32_t list_id; int selected = 0, has_fallback = 0;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0 || !dom_node_named(node, "select"))
        return dom_fail(ctx, "dom.selectedOptions.args");
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0 ||
        dom_option_list_walk(ctx, node, &list_id, 1, &selected, &has_fallback, &fallback) != 0)
        return dom_fail(ctx, "dom.selectedOptions");
    if (!selected && !dom_has_attr(node, "multiple") && fallback) {
        uint32_t next_id;
        if (dom_node_result(ctx, fallback) != 0 ||
            flat_list_push_copy(ctx->coll, ctx->pool, list_id, ctx->out, &next_id) != 0)
            return dom_fail(ctx, "dom.selectedOptions");
        list_id = next_id;
    }
    return dom_ok(ctx) + flat_value_set_list(ctx->out, list_id);
}
static lxb_dom_node_t *dom_find_option(lxb_dom_node_t *root, int selected_only) {
    lxb_dom_node_t *node;
    for (node = root ? root->first_child : NULL; node; node = node->next) {
        if (dom_node_named(node, "option")) {
            if ((!selected_only || dom_has_attr(node, "selected")) &&
                (selected_only || !dom_control_disabled(node))) return node;
        } else {
            lxb_dom_node_t *found = dom_find_option(node, selected_only);
            if (found) return found;
        }
    }
    return NULL;
}
int flat_native_dom_value(FlatNativeCtx *ctx) {
    char buf[4096], *args[1]; lxb_dom_node_t *node, *option;
    const lxb_char_t *data = NULL; size_t len = 0; FlatValue value;
    if (!ctx || !ctx->out || dom_control_arg(ctx, buf, args, 1, &node) != 0)
        return dom_fail(ctx, "dom.value.args");
    if (dom_node_named(node, "textarea")) {
        data = lxb_dom_node_text_content(node, &len);
    } else if (dom_node_named(node, "select")) {
        option = dom_find_option(node, 1);
        if (!option) option = dom_find_option(node, 0);
        if (option && dom_make_option_value(ctx, option, &value) != 0)
            return dom_fail(ctx, "dom.value");
        if (option) { *ctx->out = value; return dom_ok(ctx); }
        return dom_ok(ctx) + flat_value_set_str_inline(ctx->out, "", 0);
    } else if (dom_node_named(node, "input") || dom_node_named(node, "button")) {
        (void)dom_attr_value(node, "value", &data, &len);
        if (!data && dom_node_named(node, "input")) {
            char type[32];
            if (dom_input_type_value(node, type) == 0 &&
                (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) {
                data = (const lxb_char_t *)"on"; len = 2;
            }
        }
    } else return dom_fail(ctx, "dom.value.type");
    if (dom_make_string(ctx, data, data ? len : 0) != 0) return dom_fail(ctx, "dom.value.alloc");
    return dom_ok(ctx);
}


/* A deliberately small, documented subset of browser-style DOM properties.
 * Unknown properties return NULL; this is not a JavaScript DOM implementation. */
int flat_native_dom_prop(FlatNativeCtx *ctx) {
    char buf[4096], *args[2], *name = NULL, type[32];
    int argc, rc = -1;
    size_t name_len = 0, len = 0;
    lxb_dom_node_t *node = NULL, *option;
    const lxb_char_t *value = NULL;
    if (!ctx || !ctx->out || (snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : ""),
         dom_split_args(buf, args, 2, &argc)) != 0 ||
        argc != 2 || dom_node_ref(ctx, args[0], &node) != 0 ||
        node->type != LXB_DOM_NODE_TYPE_ELEMENT ||
        dom_get_string(ctx, args[1], &name, &name_len) != 0 ||
        name_len == 0 || name_len > 64) goto done;

    if (strcmp(name, "tagName") == 0) {
        const lxb_char_t *tag = lxb_tag_name_by_id(
            lxb_dom_element_tag_id(lxb_dom_interface_element(node)), &len);
        char upper[128]; size_t i, n = len < sizeof(upper) ? len : sizeof(upper);
        if (!tag) goto done;
        for (i = 0; i < n; ++i) upper[i] = (char)toupper((unsigned char)tag[i]);
        rc = dom_make_string(ctx, (const lxb_char_t *)upper, n);
    } else if (strcmp(name, "checked") == 0) {
        if (!dom_node_named(node, "input")) goto unknown;
        if (dom_input_type_value(node, type) != 0 ||
            (strcmp(type, "checkbox") != 0 && strcmp(type, "radio") != 0)) goto unknown;
        rc = flat_value_set_bool(ctx->out, dom_has_attr(node, "checked"));
    } else if (strcmp(name, "disabled") == 0 || strcmp(name, "required") == 0) {
        rc = flat_value_set_bool(ctx->out, strcmp(name, "disabled") == 0
            ? dom_control_disabled(node) : dom_has_attr(node, "required"));
    } else if (strcmp(name, "type") == 0 && dom_node_named(node, "input")) {
        if (dom_input_type_value(node, type) != 0) goto done;
        rc = dom_make_string(ctx, (const lxb_char_t *)type, strlen(type));
    } else if (strcmp(name, "value") == 0) {
        if (dom_node_named(node, "textarea")) {
            value = lxb_dom_node_text_content(node, &len);
        } else if (dom_node_named(node, "select")) {
            option = dom_find_option(node, 1);
            if (!option) option = dom_find_option(node, 0);
            if (option) {
                FlatValue v;
                if (dom_make_option_value(ctx, option, &v) != 0) goto done;
                *ctx->out = v; rc = 0; goto done;
            }
        } else {
            (void)dom_attr_value(node, "value", &value, &len);
            if (!value && dom_node_named(node, "input") &&
                dom_input_type_value(node, type) == 0 &&
                (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) {
                value = (const lxb_char_t *)"on"; len = 2;
            }
        }
        rc = dom_make_string(ctx, value, value ? len : 0);
    } else {
        const char *attr = NULL;
        if (strcmp(name, "id") == 0) attr = "id";
        else if (strcmp(name, "className") == 0) attr = "class";
        else if (strcmp(name, "name") == 0) attr = "name";
        else if (strcmp(name, "href") == 0) attr = "href";
        else if (strcmp(name, "src") == 0) attr = "src";
        else if (strcmp(name, "title") == 0) attr = "title";
        if (attr) {
            (void)dom_attr_value(node, attr, &value, &len);
            rc = dom_make_string(ctx, value, value ? len : 0);
        } else goto unknown;
    }
    goto done;
unknown:
    rc = flat_value_set_null(ctx->out);
done:
    free(name);
    if (rc != 0) return dom_fail(ctx, "dom.prop");
    return dom_ok(ctx);
}
