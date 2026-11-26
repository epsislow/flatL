#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile the exact bundled Lexbor amalgamation used by mod_dom.c. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "lexbor.c"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define THREAD_COUNT 8
#define ITERATIONS_PER_THREAD 250
#define SERIALIZED_CAPACITY 8192

typedef struct {
    lxb_dom_node_t *found;
    unsigned count;
} SelectResult;

typedef struct {
    char data[SERIALIZED_CAPACITY];
    size_t length;
    int overflow;
} SerializeResult;

typedef struct {
    unsigned worker_id;
    atomic_int *failures;
} WorkerArgs;

static lxb_status_t select_callback(lxb_dom_node_t *node,
                                     lxb_css_selector_specificity_t specificity,
                                     void *opaque)
{
    SelectResult *result = (SelectResult *) opaque;
    (void) specificity;
    if (result == NULL) return LXB_STATUS_ERROR;
    result->found = node;
    result->count++;
    return LXB_STATUS_OK;
}

static lxb_status_t serialize_callback(const lxb_char_t *data, size_t length,
                                       void *opaque)
{
    SerializeResult *result = (SerializeResult *) opaque;
    if (result == NULL || (data == NULL && length != 0) ||
        length > SERIALIZED_CAPACITY - result->length - 1) {
        if (result != NULL) result->overflow = 1;
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }
    if (length != 0) memcpy(result->data + result->length, data, length);
    result->length += length;
    result->data[result->length] = '\0';
    return LXB_STATUS_OK;
}

static int run_one_document(unsigned worker_id, unsigned iteration)
{
    char html[512];
    char selector[128];
    char worker_text[32];
    char round_text[32];
    size_t value_length = 0;
    const lxb_char_t *value;
    lxb_html_document_t *document = NULL;
    lxb_css_parser_t *parser = NULL;
    lxb_selectors_t *selectors = NULL;
    lxb_css_selector_list_t *list = NULL;
    lxb_dom_node_t *root;
    SelectResult selected;
    SerializeResult serialized;
    lxb_status_t status;
    int rc = -1;

    snprintf(worker_text, sizeof(worker_text), "%u", worker_id);
    snprintf(round_text, sizeof(round_text), "%u", iteration);
    snprintf(html, sizeof(html),
             "<!doctype html><html><body><section class=\"card\" data-worker=\"%s\">"
             "<h1>worker-%s</h1><p data-round=\"before\">round-%s</p>"
             "</section></body></html>", worker_text, worker_text, round_text);
    snprintf(selector, sizeof(selector), "section.card[data-worker=\"%s\"]", worker_text);

    memset(&selected, 0, sizeof(selected));
    memset(&serialized, 0, sizeof(serialized));

    document = lxb_html_document_create();
    if (document == NULL) goto done;
    status = lxb_html_document_parse(document, (const lxb_char_t *) html, strlen(html));
    if (status != LXB_STATUS_OK) goto done;

    parser = lxb_css_parser_create();
    selectors = lxb_selectors_create();
    if (parser == NULL || selectors == NULL ||
        lxb_css_parser_init(parser, NULL) != LXB_STATUS_OK ||
        lxb_selectors_init(selectors) != LXB_STATUS_OK) goto done;

    list = lxb_css_selectors_parse(parser, (const lxb_char_t *) selector, strlen(selector));
    if (list == NULL) goto done;

    root = lxb_dom_interface_node(document);
    status = lxb_selectors_find(selectors, root, list, select_callback, &selected);
    if (status != LXB_STATUS_OK || selected.count != 1 || selected.found == NULL) goto done;

    if (lxb_dom_element_set_attribute(lxb_dom_interface_element(selected.found),
                                      (const lxb_char_t *) "data-round", 10,
                                      (const lxb_char_t *) round_text, strlen(round_text)) == NULL) {
        goto done;
    }
    value = lxb_dom_element_get_attribute(lxb_dom_interface_element(selected.found),
                                          (const lxb_char_t *) "data-round", 10,
                                          &value_length);
    if (value == NULL || value_length != strlen(round_text) ||
        memcmp(value, round_text, value_length) != 0) goto done;

    status = lxb_html_serialize_tree_cb(root, serialize_callback, &serialized);
    if (status != LXB_STATUS_OK || serialized.overflow ||
        strstr(serialized.data, "data-worker=") == NULL ||
        strstr(serialized.data, "data-round=") == NULL) goto done;

    rc = 0;

done:
    if (list != NULL) lxb_css_selector_list_destroy_memory(list);
    if (selectors != NULL) lxb_selectors_destroy(selectors, true);
    if (parser != NULL) lxb_css_parser_destroy(parser, true);
    if (document != NULL) lxb_html_document_destroy(document);
    return rc;
}

static void *worker_main(void *opaque)
{
    WorkerArgs *args = (WorkerArgs *) opaque;
    unsigned i;
    for (i = 0; i < ITERATIONS_PER_THREAD; ++i) {
        if (run_one_document(args->worker_id, i) != 0) {
            atomic_fetch_add_explicit(args->failures, 1, memory_order_relaxed);
            break;
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[THREAD_COUNT];
    WorkerArgs args[THREAD_COUNT];
    atomic_int failures = ATOMIC_VAR_INIT(0);
    unsigned created = 0;
    unsigned i;

    for (i = 0; i < THREAD_COUNT; ++i) {
        args[i].worker_id = i + 1;
        args[i].failures = &failures;
        if (pthread_create(&threads[i], NULL, worker_main, &args[i]) != 0) {
            atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
            break;
        }
        created++;
    }
    for (i = 0; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0)
            atomic_fetch_add_explicit(&failures, 1, memory_order_relaxed);
    }

    if (created != THREAD_COUNT ||
        atomic_load_explicit(&failures, memory_order_relaxed) != 0) {
        fprintf(stderr, "lexbor-concurrency-failed created=%u failures=%d\n",
                created, atomic_load_explicit(&failures, memory_order_relaxed));
        return 1;
    }

    printf("lexbor-concurrency-ok workers=%u documents=%u\n",
           THREAD_COUNT, THREAD_COUNT * ITERATIONS_PER_THREAD);
    return 0;
}
