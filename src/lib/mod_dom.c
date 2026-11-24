#include "flat_native.h"
#include "flat_native_mod.h"
#include "flat_native_util.h"
#include "flat_dom_runtime.h"
#include "flat_value.h"
#include "flat_number.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#ifdef FLAT_DOM_BUNDLED
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../../external/lexbor/lexbor.c"
#if defined(__GNUC__)
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
    for (i = 0; i < FLAT_DOM_MAX_DOCUMENTS; ++i) dom_doc_clear(&runtime->docs[i]);
    free(runtime);
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
    lxb_css_parser_t *parser = NULL;
    lxb_selectors_t *selectors = NULL;
    lxb_css_selector_list_t *list = NULL;
    FlatDomSelectCtx state;
    lxb_status_t status;
    int rc = -1;
    if (!ctx || !ctx->dom || !ctx->out) return dom_fail(ctx, "dom.select");
    snprintf(buf, sizeof(buf), "%s", ctx->inner ? ctx->inner : "");
    if (dom_split_args(buf, args, 2, &argc) != 0 || argc != 2 ||
        dom_doc_id(ctx, args[0], &doc_id) != 0 ||
        dom_get_string(ctx, args[1], &selector, &len) != 0 || len == 0 || len > 4096)
        goto done;
    doc = dom_find_document(ctx, doc_id);
    if (!doc) goto done;
    if (flat_list_empty(ctx->coll, ctx->pool, &list_id) != 0) goto done;
    parser = lxb_css_parser_create();
    selectors = lxb_selectors_create();
    if (!parser || !selectors || lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK ||
        lxb_selectors_init(selectors) != LXB_STATUS_OK) goto done;
    list = lxb_css_selectors_parse(parser, (const lxb_char_t *)selector, len);
    if (!list) goto done;
    memset(&state, 0, sizeof(state));
    state.ctx = ctx; state.doc = doc; state.doc_id = doc_id; state.list_id = list_id;
    status = lxb_selectors_find(selectors, lxb_dom_interface_node(doc->document),
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
