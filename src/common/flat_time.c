#include "flat_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

double flat_time_monotonic_ms(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int init;
    LARGE_INTEGER ctr;

    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    QueryPerformanceCounter(&ctr);
    return (double)ctr.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}

static long long flat_time_round_ms(double ms) {
    if (ms <= 0.0) {
        return 0;
    }
    return (long long)(ms + 0.5);
}

void flat_time_format_duration_display(char *buf, size_t buf_sz, double ms,
                                       FlatTimeDisplayStyle style) {
    long long t;
    long long m;
    long long ss;
    long long mmm;

    if (!buf || buf_sz == 0) {
        return;
    }
    t = flat_time_round_ms(ms);
    if (t < 60000) {
        if (style == FLAT_TIME_FMT_LABELED) {
            snprintf(buf, buf_sz, "%lld ms", (long long)t);
        } else {
            snprintf(buf, buf_sz, "%lld", (long long)t);
        }
        return;
    }
    mmm = t % 1000;
    ss = (t / 1000) % 60;
    m = t / 60000;
    snprintf(buf, buf_sz, "%lld:%02lld,%03lld", (long long)m, (long long)ss, (long long)mmm);
}

void flat_time_line_begin(char *line, size_t line_sz) {
    if (line && line_sz > 0) {
        line[0] = '\0';
    }
}

static int flat_time_cat(char *line, size_t line_sz, const char *piece) {
    size_t len;
    size_t cur;

    if (!line || !piece) {
        return -1;
    }
    cur = strlen(line);
    len = strlen(piece);
    if (cur + len + 1 >= line_sz) {
        return -1;
    }
    memcpy(line + cur, piece, len + 1);
    return 0;
}

void flat_time_append_field(char *line, size_t line_sz, const char *key, double ms) {
    char val[64];
    char piece[96];

    flat_time_format_duration_display(val, sizeof val, ms, FLAT_TIME_FMT_FIELD);
    snprintf(piece, sizeof piece, " %s=%s", key, val);
    (void)flat_time_cat(line, line_sz, piece);
}

void flat_time_append_int_field(char *line, size_t line_sz, const char *key, int value) {
    char piece[64];

    snprintf(piece, sizeof piece, " %s=%d", key, value);
    (void)flat_time_cat(line, line_sz, piece);
}

int flat_time_finish_line(char *line, size_t line_sz) {
    (void)line;
    (void)line_sz;
    return 0;
}

void flat_time_fprintf_time_ms(const char *prefix, const char *body) {
    fprintf(stderr, "%s: time_ms%s\n", prefix, body ? body : "");
}

void flat_time_print_tests_footer(double ms) {
    char val[64];

    flat_time_format_duration_display(val, sizeof val, ms, FLAT_TIME_FMT_LABELED);
    printf("Total tests time: %s\n", val);
}

static const char *find_field(const char *line, const char *key, int *out) {
    char pat[32];
    const char *p;
    char *end;

    snprintf(pat, sizeof pat, "%s=", key);
    p = strstr(line, pat);
    if (!p) {
        return NULL;
    }
    p += strlen(pat);
    *out = (int)strtol(p, &end, 10);
    if (end == p) {
        return NULL;
    }
    return p;
}

int flat_time_parse_tool_line(const char *capture, const char *tool_prefix, int *units_out,
                              int *fmod_out, int *have_units, int *have_fmod) {
    char tag[48];
    const char *line;
    int u = 0;
    int f = 0;

    if (!capture || !tool_prefix) {
        return -1;
    }
    snprintf(tag, sizeof tag, "%s: time_ms", tool_prefix);
    line = strstr(capture, tag);
    if (!line) {
        return -1;
    }
    if (have_units) {
        *have_units = find_field(line, "units", &u) != NULL;
    }
    if (have_fmod) {
        *have_fmod = find_field(line, "fmod", &f) != NULL;
    }
    if (have_units && *have_units && units_out) {
        *units_out = u;
    }
    if (have_fmod && *have_fmod && fmod_out) {
        *fmod_out = f;
    }
    return 0;
}

static int parse_duration_token(const char *p, double *ms_out) {
    const char *colon;
    char *end;
    long long whole;

    if (!p || !*p) {
        return -1;
    }
    colon = strchr(p, ':');
    if (colon) {
        long long m = strtoll(p, &end, 10);
        long long ss;
        long long mmm;

        if (end != colon) {
            return -1;
        }
        ss = strtoll(colon + 1, &end, 10);
        if (!end || *end != ',') {
            return -1;
        }
        {
            char *end2;
            mmm = strtoll(end + 1, &end2, 10);
            if (end2 == end + 1) {
                return -1;
            }
        }
        *ms_out = (double)(m * 60000 + ss * 1000 + mmm);
        return 0;
    }
    whole = strtoll(p, &end, 10);
    if (end == p) {
        return -1;
    }
    *ms_out = (double)whole;
    return 0;
}

int flat_time_parse_field_ms(const char *line, const char *key, double *ms_out) {
    char pat[32];
    const char *p;
    const char *sp;

    snprintf(pat, sizeof pat, "%s=", key);
    p = strstr(line, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    sp = p;
    while (*sp && *sp != ' ' && *sp != '\n' && *sp != '\r') {
        sp++;
    }
    {
        char buf[64];
        size_t n = (size_t)(sp - p);
        if (n >= sizeof buf) {
            return -1;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        return parse_duration_token(buf, ms_out);
    }
}

void flat_time_print_runner_suffix(const char *body) {
    printf(" time_ms%s", body ? body : "");
    fflush(stdout);
}

int flat_time_env_enabled(void) {
    const char *e = getenv("FLAT_TIME");
    return e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y');
}
