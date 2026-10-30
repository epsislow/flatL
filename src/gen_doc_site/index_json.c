#include "gen_doc_index.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int read_json_string(const char **pp, char *out, size_t out_sz) {
    const char *p = skip_ws(*pp);
    size_t i = 0;

    if (*p != '"') {
        return -1;
    }
    p++;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (*p == '\0') {
                return -1;
            }
            c = *p++;
            if (c == 'n') {
                c = '\n';
            } else if (c == 'r') {
                c = '\r';
            } else if (c == 't') {
                c = '\t';
            } else if (c == '"') {
                c = '"';
            } else if (c == '\\') {
                c = '\\';
            }
        }
        if (i + 1 >= out_sz) {
            return -1;
        }
        out[i++] = c;
    }
    if (*p != '"') {
        return -1;
    }
    out[i] = '\0';
    *pp = p + 1;
    return 0;
}

static const char *find_key_string(const char *json, const char *key, char *out, size_t out_sz) {
    char pattern[128];
    const char *p;

    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p) {
        return NULL;
    }
    p += strlen(pattern);
    p = skip_ws(p);
    if (*p != ':') {
        return NULL;
    }
    p = skip_ws(p + 1);
    if (read_json_string(&p, out, out_sz) != 0) {
        return NULL;
    }
    return p;
}

static int parse_section_object(const char *obj, GenDocSection *sec) {
    const char *p = obj;

    if (find_key_string(p, "id", sec->id, sizeof sec->id) == NULL) {
        return -1;
    }
    if (find_key_string(p, "label", sec->label, sizeof sec->label) == NULL) {
        return -1;
    }
    if (find_key_string(p, "file", sec->file, sizeof sec->file) == NULL) {
        return -1;
    }
    return 0;
}

static int load_markdown_for_section(const char *docs_dir, GenDocSection *sec, char *err,
                                     size_t err_size) {
    char path[1024];
    FILE *f;
    long sz;
    char *buf;

    snprintf(path, sizeof path, "%s/%s", docs_dir, sec->file);
    f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_size, "missing markdown: %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, err_size, "read failed: %s", path);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0 || sz > 8 * 1024 * 1024) {
        fclose(f);
        snprintf(err, err_size, "markdown too large: %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        snprintf(err, err_size, "read failed: %s", path);
        return -1;
    }
    buf[sz] = '\0';
    fclose(f);
    sec->markdown = buf;
    return 0;
}

int gen_doc_index_load(const char *docs_dir, const char *index_path, GenDocIndex *out,
                       char *err, size_t err_size) {
    FILE *f;
    char *json;
    long json_len;
    const char *sections;
    const char *p;
    int n = 0;
    int i;

    if (!index_path || !out) {
        snprintf(err, err_size, "invalid arguments");
        return -1;
    }
    memset(out, 0, sizeof *out);

    f = fopen(index_path, "rb");
    if (!f) {
        snprintf(err, err_size, "cannot open index: %s", index_path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        snprintf(err, err_size, "read index failed");
        return -1;
    }
    json_len = ftell(f);
    if (json_len < 0 || json_len > 1024 * 1024) {
        fclose(f);
        snprintf(err, err_size, "index too large");
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    json = (char *)malloc((size_t)json_len + 1);
    if (!json) {
        fclose(f);
        snprintf(err, err_size, "out of memory");
        return -1;
    }
    if (fread(json, 1, (size_t)json_len, f) != (size_t)json_len) {
        free(json);
        fclose(f);
        snprintf(err, err_size, "read index failed");
        return -1;
    }
    json[json_len] = '\0';
    fclose(f);

    if (find_key_string(json, "title", out->title, sizeof out->title) == NULL) {
        snprintf(out->title, sizeof out->title, "flatL documentation");
    }

    sections = strstr(json, "\"sections\"");
    if (!sections) {
        free(json);
        snprintf(err, err_size, "index missing sections");
        return -1;
    }
    p = strchr(sections, '[');
    if (!p) {
        free(json);
        snprintf(err, err_size, "index sections not array");
        return -1;
    }
    p++;

    while (n < GEN_DOC_MAX_SECTIONS) {
        GenDocSection sec;
        const char *obj_start;
        const char *obj_end;
        char obj_buf[4096];
        size_t obj_len;

        p = skip_ws(p);
        if (*p == ']') {
            break;
        }
        if (*p != '{') {
            free(json);
            snprintf(err, err_size, "invalid section object");
            return -1;
        }
        obj_start = p;
        obj_end = strchr(obj_start, '}');
        if (!obj_end) {
            free(json);
            snprintf(err, err_size, "unclosed section object");
            return -1;
        }
        obj_len = (size_t)(obj_end - obj_start + 1);
        if (obj_len >= sizeof obj_buf) {
            free(json);
            snprintf(err, err_size, "section object too large");
            return -1;
        }
        memcpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';
        memset(&sec, 0, sizeof sec);
        if (parse_section_object(obj_buf, &sec) != 0) {
            free(json);
            snprintf(err, err_size, "section missing id/label/file");
            return -1;
        }
        for (i = 0; i < n; i++) {
            if (strcmp(out->sections[i].id, sec.id) == 0) {
                free(json);
                snprintf(err, err_size, "duplicate section id: %s", sec.id);
                return -1;
            }
        }
        out->sections[n] = sec;
        n++;
        p = obj_end + 1;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }
    out->section_count = n;
    free(json);

    if (n == 0) {
        snprintf(err, err_size, "index has no sections");
        return -1;
    }

    for (i = 0; i < out->section_count; i++) {
        if (load_markdown_for_section(docs_dir, &out->sections[i], err, err_size) != 0) {
            gen_doc_index_free(out);
            return -1;
        }
    }
    return 0;
}

void gen_doc_index_free(GenDocIndex *idx) {
    int i;
    if (!idx) {
        return;
    }
    for (i = 0; i < idx->section_count; i++) {
        free(idx->sections[i].markdown);
        idx->sections[i].markdown = NULL;
    }
    idx->section_count = 0;
}
