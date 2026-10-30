#include "gen_doc_site.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int fprint_json_string(FILE *f, const char *s) {
    const unsigned char *p;
    if (fputc('"', f) == EOF) {
        return -1;
    }
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') {
            if (fputc('\\', f) == EOF) {
                return -1;
            }
            if (fputc((char)*p, f) == EOF) {
                return -1;
            }
        } else if (*p == '\n') {
            if (fputs("\\n", f) == EOF) {
                return -1;
            }
        } else if (*p == '\r') {
            if (fputs("\\r", f) == EOF) {
                return -1;
            }
        } else if (*p == '\t') {
            if (fputs("\\t", f) == EOF) {
                return -1;
            }
        } else if (*p < 0x20) {
            if (fprintf(f, "\\u%04x", (unsigned)*p) < 0) {
                return -1;
            }
        } else {
            if (fputc((char)*p, f) == EOF) {
                return -1;
            }
        }
    }
    return fputc('"', f) == EOF ? -1 : 0;
}

int gen_doc_emit_js(const char *out_path, const GenDocIndex *idx, char *err, size_t err_size) {
    FILE *f;
    time_t now;
    struct tm tm_buf;
    char iso[64];
    int i;

    if (!out_path || !idx) {
        snprintf(err, err_size, "invalid emit arguments");
        return -1;
    }

    f = fopen(out_path, "wb");
    if (!f) {
        snprintf(err, err_size, "cannot write: %s", out_path);
        return -1;
    }

    now = time(NULL);
#ifdef _WIN32
    if (gmtime_s(&tm_buf, &now) != 0) {
        snprintf(iso, sizeof iso, "1970-01-01T00:00:00Z");
    } else {
        strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    }
#else
    {
        const struct tm *gt = gmtime(&now);
        if (!gt) {
            snprintf(iso, sizeof iso, "1970-01-01T00:00:00Z");
        } else {
            tm_buf = *gt;
            strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        }
    }
#endif

    fputs("/** GENERATED — do not edit. Run: bin/gen_doc_site or make docs */\n", f);
    fputs("(function (root) {\n", f);
    fputs("  'use strict';\n", f);
    fputs("  root.FlatDocData = {\n", f);
    fprintf(f, "    \"title\": ");
    if (fprint_json_string(f, idx->title) != 0) {
        fclose(f);
        return -1;
    }
    fputs(",\n", f);
    fprintf(f, "    \"generatedAt\": ");
    if (fprint_json_string(f, iso) != 0) {
        fclose(f);
        return -1;
    }
    fputs(",\n    \"sections\": [\n", f);

    for (i = 0; i < idx->section_count; i++) {
        const GenDocSection *sec = &idx->sections[i];
        fputs("      {\n", f);
        fprintf(f, "        \"id\": ");
        if (fprint_json_string(f, sec->id) != 0) {
            fclose(f);
            return -1;
        }
        fputs(",\n", f);
        fprintf(f, "        \"label\": ");
        if (fprint_json_string(f, sec->label) != 0) {
            fclose(f);
            return -1;
        }
        fputs(",\n", f);
        fprintf(f, "        \"file\": ");
        if (fprint_json_string(f, sec->file) != 0) {
            fclose(f);
            return -1;
        }
        fputs(",\n", f);
        fprintf(f, "        \"markdown\": ");
        if (fprint_json_string(f, sec->markdown ? sec->markdown : "") != 0) {
            fclose(f);
            return -1;
        }
        fputs(i + 1 < idx->section_count ? "\n      },\n" : "\n      }\n", f);
    }

    fputs("    ]\n  };\n", f);
    fputs("})(typeof globalThis !== 'undefined' ? globalThis : this);\n", f);

    if (fclose(f) != 0) {
        snprintf(err, err_size, "write failed: %s", out_path);
        return -1;
    }
    return 0;
}
