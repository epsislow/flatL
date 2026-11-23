#include "flat_http_transport.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

typedef struct {
    FlatHttpResponse *response;
    size_t max_body;
    int overflow;
} Transfer;


static void trim_text(char *s) {
    size_t n; while (*s && (*s==' ' || *s=='\t')) memmove(s,s+1,strlen(s));
    n=strlen(s); while(n && (s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n')) s[--n]='\0';
}
static void toml_string(char *s) {
    size_t n=strlen(s),i,o=0; char q;
    if(n<2 || !((s[0]=='"'&&s[n-1]=='"')||(s[0]=='\''&&s[n-1]=='\''))) return;
    q=s[0]; for(i=1;i<n-1;i++){ if(q=='"'&&s[i]=='\\'&&i+1<n-1){i++;switch(s[i]){case 'n':s[o++]='\n';break;case 'r':s[o++]='\r';break;case 't':s[o++]='\t';break;default:s[o++]=s[i];break;}}else s[o++]=s[i]; } s[o]='\0';
}
static int mock_append_header(FlatHttpResponse *out,const char *line) {
    char **p=realloc(out->headers,(out->header_count+1)*sizeof *p); if(!p)return -1;out->headers=p;out->headers[out->header_count]=strdup(line);if(!out->headers[out->header_count])return -1;out->header_count++;return 0;
}
static int perform_mock(const FlatHttpRequest *req, FlatHttpResponse *out) {
    const char *fixture = getenv("FLAT_HTTP_MOCK_FILE");
    FILE *f;
    char line[2048], section[64] = "";
    char method[16] = "", mock_path[512] = "", body_inline[8192] = "";
    char body_file[512] = "", error[256] = "";
    long status = 200;
    long redirect_statuses[FLAT_HTTP_STATUS_CHAIN_MAX];
    size_t redirect_status_count = 0;
    int error_code = 0;
    int show_request_body = 0;
    unsigned char *body = NULL;
    size_t body_len = 0;
    if (!fixture || !*fixture) return 0;
    f = fopen(fixture, "r");
    if (!f) {
        out->error_code = 3;
        snprintf(out->error, sizeof out->error, "HTTP mock fixture unavailable");
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        char *p = line, *eq, *key, *val;
        trim_text(p);
        if (!*p || *p == '#') continue;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (e) { *e = '\0'; snprintf(section, sizeof section, "%s", p + 1); }
            continue;
        }
        if (strcmp(section, "http.mock") != 0) continue;
        eq = strchr(p, '='); if (!eq) continue;
        *eq = '\0'; key = p; val = eq + 1; trim_text(key); trim_text(val);
        if (strcmp(key, "method") == 0) { toml_string(val); snprintf(method, sizeof method, "%s", val); }
        else if (strcmp(key, "path") == 0) { toml_string(val); snprintf(mock_path, sizeof mock_path, "%s", val); }
        else if (strcmp(key, "body") == 0) { toml_string(val); snprintf(body_inline, sizeof body_inline, "%s", val); }
        else if (strcmp(key, "body_file") == 0) { toml_string(val); snprintf(body_file, sizeof body_file, "%s", val); }
        else if (strcmp(key, "error") == 0) { toml_string(val); snprintf(error, sizeof error, "%s", val); }
        else if (strcmp(key, "status") == 0) status = strtol(val, NULL, 10);
        else if (strcmp(key, "error_code") == 0) error_code = (int)strtol(val, NULL, 10);
        else if (strcmp(key, "show_request_body") == 0) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%s", val);
            toml_string(tmp);
            show_request_body = (strcmp(tmp, "true") == 0 || strcmp(tmp, "1") == 0 ||
                                 strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        }
        else if ((strcmp(key, "redirect_statuses") == 0 || strcmp(key, "response_statuses") == 0) &&
                 val[0] == '[') {
            char *q = val + 1;
            redirect_status_count = 0;
            while (*q && redirect_status_count < FLAT_HTTP_STATUS_CHAIN_MAX) {
                char *end = NULL;
                long code;
                while (*q == ' ' || *q == '\t' || *q == ',') q++;
                if (*q == ']' || !*q) break;
                code = strtol(q, &end, 10);
                if (end == q) break;
                redirect_statuses[redirect_status_count++] = code;
                q = end;
            }
        }
        else if (strcmp(key, "headers") == 0 && val[0] == '[') {
            char *q = val + 1;
            while (*q) {
                char item[512]; size_t z = 0; char quote;
                while (*q == ' ' || *q == '\t' || *q == ',') q++;
                if (*q == ']' || !*q) break;
                if (*q != '"' && *q != '\'') break;
                quote = *q++;
                while (*q && *q != quote && z + 1 < sizeof item) {
                    if (*q == '\\' && q[1]) q++;
                    item[z++] = *q++;
                }
                if (*q == quote) q++;
                item[z] = '\0';
                if (mock_append_header(out, item) != 0) break;
                while (*q && *q != ',') q++;
            }
        } else if (strncmp(key, "header_", 7) == 0) {
            char h[512], hn[256]; size_t k;
            toml_string(val); snprintf(hn, sizeof hn, "%s", key + 7);
            for (k = 0; hn[k]; ++k) {
                if (hn[k] == '_') hn[k] = '-';
                else hn[k] = (char)tolower((unsigned char)hn[k]);
            }
            snprintf(h, sizeof h, "%s: %s", hn, val);
            if (mock_append_header(out, h) != 0) break;
        }
    }
    fclose(f);
    if (!method[0] && !mock_path[0]) {
        out->error_code = 3;
        snprintf(out->error, sizeof out->error, "HTTP mock fixture has no method/path");
        return 1;
    }
    {
        const char *u = strstr(req->url, "://"), *slash;
        const char *actual_path = "/";
        if (u) { u += 3; slash = strchr(u, '/'); if (slash) actual_path = slash; }
        if ((method[0] && strcmp(method, req->method) != 0) ||
            (mock_path[0] && strcmp(mock_path, actual_path) != 0)) {
            size_t i; for (i = 0; i < out->header_count; ++i) free(out->headers[i]);
            free(out->headers); out->headers = NULL; out->header_count = 0;
            out->error_code = 3;
            snprintf(out->error, sizeof out->error, "HTTP mock did not match method/path");
            return 1;
        }
    }
    if (show_request_body) {
        body_len = req->body ? req->body_len : 0;
        if (body_len > req->max_body_bytes) {
            out->error_code = 23;
            snprintf(out->error, sizeof out->error, "response body exceeds configured limit");
            return 1;
        }
        body = malloc(body_len + 1);
        if (!body) {
            out->error_code = 27;
            snprintf(out->error, sizeof out->error, "out of memory");
            return 1;
        }
        if (body_len && req->body) memcpy(body, req->body, body_len);
        body[body_len] = '\0';
    } else if (body_file[0]) {
        char file_path[1024]; const char *slash = strrchr(fixture, '/'); const char *backslash = strrchr(fixture, '\\'); FILE *bf;
        size_t n;
        if (backslash && (!slash || backslash > slash)) slash = backslash;
        if (slash) snprintf(file_path, sizeof file_path, "%.*s/%s", (int)(slash - fixture), fixture, body_file);
        else snprintf(file_path, sizeof file_path, "%s", body_file);
        bf = fopen(file_path, "rb");
        if (!bf) { out->error_code = 3; snprintf(out->error, sizeof out->error, "HTTP mock body file unavailable"); return 1; }
        if (fseek(bf, 0, SEEK_END) != 0) {
            size_t hi;
            fclose(bf);
            for (hi = 0; hi < out->header_count; ++hi) free(out->headers[hi]);
            free(out->headers); out->headers = NULL; out->header_count = 0;
            out->error_code = 3;
            snprintf(out->error, sizeof out->error, "HTTP mock body file seek failed");
            return 1;
        }
        { long size = ftell(bf); if (size < 0 || (unsigned long)size > req->max_body_bytes) { fclose(bf); out->error_code=23; snprintf(out->error,sizeof out->error,"response body exceeds configured limit"); return 1; } body_len=(size_t)size; }
        rewind(bf); body = malloc(body_len + 1);
        if (!body) { fclose(bf); out->error_code=27; snprintf(out->error,sizeof out->error,"out of memory"); return 1; }
        n = fread(body, 1, body_len, bf); fclose(bf);
        if (n != body_len) { free(body); out->error_code=23; snprintf(out->error,sizeof out->error,"HTTP mock body read failed"); return 1; }
        body[body_len] = '\0';
    } else {
        body_len = strlen(body_inline); body = malloc(body_len + 1);
        if (!body) { out->error_code=27; snprintf(out->error,sizeof out->error,"out of memory"); return 1; }
        memcpy(body, body_inline, body_len + 1);
        if (body_len > req->max_body_bytes) { free(body); out->error_code=23; snprintf(out->error,sizeof out->error,"response body exceeds configured limit"); return 1; }
    }
    out->error_code = error_code;
    if (error[0]) snprintf(out->error, sizeof out->error, "%s", error);
    out->body = (char *)body; out->body_len = body_len;
    if (redirect_status_count > 0) {
        size_t hops = redirect_status_count - 1u;
        size_t take;
        size_t i;
        out->redirect_limit_hit = 0;
        if (req->max_redirects == 0) {
            take = 1;
        } else if (req->max_redirects < 0) {
            take = redirect_status_count;
        } else if (hops <= (size_t)req->max_redirects) {
            take = redirect_status_count;
        } else {
            take = (size_t)req->max_redirects + 1u;
            out->redirect_limit_hit = 1;
        }
        if (take > FLAT_HTTP_STATUS_CHAIN_MAX) take = FLAT_HTTP_STATUS_CHAIN_MAX;
        for (i = 0; i < take; ++i) out->status_chain[i] = redirect_statuses[i];
        out->status_chain_count = take;
        out->status = redirect_statuses[take - 1];
        if (out->redirect_limit_hit || (take == 1 && req->max_redirects == 0)) {
            /* Intermediate stop: drop success body unless it was an error fixture. */
            if (out->error_code == 0 && out->body) {
                free(out->body);
                out->body = calloc(1, 1);
                out->body_len = 0;
            }
        }
    } else {
        out->status = status;
        out->status_chain[0] = status;
        out->status_chain_count = 1;
    }
    return 1;
}

static size_t write_body(char *data, size_t size, size_t count, void *arg) {
    Transfer *t = arg;
    size_t n, total;
    char *next;
    if (size && count > SIZE_MAX / size) return 0;
    n = size * count;
    if (n > t->max_body - t->response->body_len) { t->overflow = 1; return 0; }
    total = t->response->body_len + n;
    next = realloc(t->response->body, total + 1);
    if (!next) return 0;
    t->response->body = next;
    memcpy(next + t->response->body_len, data, n);
    next[total] = '\0';
    t->response->body_len = total;
    return n;
}

static size_t write_header(char *data, size_t size, size_t count, void *arg) {
    Transfer *t = arg;
    FlatHttpResponse *r = t->response;
    size_t n, i;
    char *line;
    char **new_headers;
    if (size && count > SIZE_MAX / size) return 0;
    n = size * count;
    if (n == 0 || n > 16384) return 0;
    if (n >= 5 && memcmp(data, "HTTP/", 5) == 0) {
        size_t j;
        const char *p = data;
        long code = 0;
        for (j = 0; j < r->header_count; ++j) free(r->headers[j]);
        free(r->headers); r->headers = NULL; r->header_count = 0;
        while (p < data + n && *p != ' ' && *p != '\t') p++;
        while (p < data + n && (*p == ' ' || *p == '\t')) p++;
        if (p < data + n && *p >= '0' && *p <= '9') {
            code = strtol(p, NULL, 10);
            if (code > 0 && r->status_chain_count < FLAT_HTTP_STATUS_CHAIN_MAX)
                r->status_chain[r->status_chain_count++] = code;
        }
        return n;
    }
    if (n < 2 || data[0] == '\r' || !memchr(data, ':', n)) return n;
    if (r->header_count >= 256) return 0;
    line = malloc(n + 1);
    if (!line) return 0;
    memcpy(line, data, n);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) --n;
    line[n] = '\0';
    for (i = 0; i < n && line[i] != ':'; ++i) {
        if (line[i] >= 'A' && line[i] <= 'Z') line[i] += 'a' - 'A';
    }
    new_headers = realloc(r->headers, (r->header_count + 1) * sizeof *new_headers);
    if (!new_headers) { free(line); return 0; }
    r->headers = new_headers;
    r->headers[r->header_count++] = line;
    return size * count;
}

void flat_http_response_free(FlatHttpResponse *out) {
    size_t i;
    if (!out) return;
    for (i = 0; i < out->header_count; ++i) free(out->headers[i]);
    free(out->headers);
    free(out->body);
    memset(out, 0, sizeof *out);
}

static void finalize_status_chain(FlatHttpResponse *out) {
    if (!out) return;
    if (out->status_chain_count == 0 && out->status > 0) {
        out->status_chain[0] = out->status;
        out->status_chain_count = 1;
    } else if (out->status_chain_count > 0) {
        out->status = out->status_chain[out->status_chain_count - 1];
    }
}

static int ensure_curl_global(FlatHttpResponse *out) {
    static int ready;
    CURLcode rc;
    if (ready) return 0;
    rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        if (out) {
            out->error_code = (int)rc;
            snprintf(out->error, sizeof out->error, "%s", curl_easy_strerror(rc));
        }
        return -1;
    }
    ready = 1;
    return 0;
}

int flat_http_perform(const FlatHttpRequest *req, FlatHttpResponse *out) {
    CURL *curl;
    CURLcode result;
    struct curl_slist *headers = NULL;
    Transfer transfer;
    size_t i;
    char curl_error[CURL_ERROR_SIZE] = {0};
    if (!req || !out || !req->url || !req->method || !req->max_body_bytes ||
        req->max_body_bytes == SIZE_MAX || req->timeout_ms <= 0) return -1;
    memset(out, 0, sizeof *out);
    { int mocked = perform_mock(req, out); if (mocked) return out->error_code == 0 ? 0 : -1; }
    if (ensure_curl_global(out) != 0) return -1;
    curl = curl_easy_init();
    if (!curl) { snprintf(out->error, sizeof out->error, "curl init failed"); return -1; }
    transfer.response = out;
    transfer.max_body = req->max_body_bytes;
    transfer.overflow = 0;
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method);
    /* Never forward credentials to a different host across redirects. */
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, req->timeout_ms);
    if (req->max_redirects == 0) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, req->max_redirects < 0 ? -1L : req->max_redirects);
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &transfer);
    if (req->body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
    }
    for (i = 0; i < req->header_count; ++i) {
        struct curl_slist *next = curl_slist_append(headers, req->headers[i]);
        if (!next) { curl_slist_free_all(headers); curl_easy_cleanup(curl); return -1; }
        headers = next;
    }
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
    out->error_code = (int)result;
    if (result == CURLE_TOO_MANY_REDIRECTS) {
        out->redirect_limit_hit = 1;
        snprintf(out->error, sizeof out->error, "%s",
                 curl_error[0] ? curl_error : curl_easy_strerror(result));
        finalize_status_chain(out);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return 0; /* response usable; native layer panics http.redirect.limit */
    }
    if (result != CURLE_OK)
        snprintf(out->error, sizeof out->error, "%s",
                 transfer.overflow ? "response body exceeds configured limit"
                                   : (curl_error[0] ? curl_error : curl_easy_strerror(result)));
    finalize_status_chain(out);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK ? 0 : -1;
}
