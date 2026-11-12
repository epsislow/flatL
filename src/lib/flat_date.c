#include "flat_date.h"
#include "flat_number.h"
#include "flat_value.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SEC_PER_DAY 86400LL
#define MS_THRESHOLD 100000000000LL

static FlatDateRunState g_date_state;

FlatDateRunState *flat_date_global_state(void) {
    return &g_date_state;
}

static int64_t env_now_override(void) {
    const char *e = getenv("FLAT_DATE_NOW_SEC");
    char *end;
    long long v;
    if (!e || !*e) {
        return -1;
    }
    v = strtoll(e, &end, 10);
    if (end == e) {
        return -1;
    }
    return (int64_t)v;
}

void flat_date_run_begin(FlatDateRunState *st) {
    const char *off;
    if (!st) {
        return;
    }
    st->run_offset_min = 0;
    st->has_run_offset = 0;
    st->now_override_sec = env_now_override();
    off = getenv("FLAT_DATE_RUN_OFFSET");
    if (off && off[0]) {
        int m;
        if (flat_date_parse_offset_hhmm(off, &m) == 0) {
            st->run_offset_min = m;
            st->has_run_offset = 1;
        }
    }
}

int flat_date_parse_offset_hhmm(const char *s, int *out_min_east) {
    int sign = 1;
    int h = 0;
    int m = 0;
    const char *p;
    if (!s || !out_min_east) {
        return -1;
    }
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+') {
        s++;
    } else if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == 'Z' || *s == 'z') {
        if (s[1] == '\0') {
            *out_min_east = 0;
            return 0;
        }
        return -1;
    }
    p = s;
    while (*p && isdigit((unsigned char)*p)) {
        h = h * 10 + (*p - '0');
        p++;
    }
    if (p == s || h > 23) {
        return -1;
    }
    if (*p == ':') {
        p++;
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) {
            return -1;
        }
        m = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (m > 59) {
            return -1;
        }
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    *out_min_east = sign * (h * 60 + m);
    return 0;
}

static int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int mo) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12) {
        return 0;
    }
    if (mo == 2 && is_leap(y)) {
        return 29;
    }
    return d[mo - 1];
}

static void utc_parts_from_sec(int64_t sec, int *y, int *mo, int *d, int *h, int *mi, int *ss) {
    int64_t days;
    int64_t rem;
    int yy;
    int month;
    if (sec < 0) {
        int64_t adj = (-sec + SEC_PER_DAY - 1) / SEC_PER_DAY;
        sec += adj * SEC_PER_DAY;
        days = sec / SEC_PER_DAY - adj;
    } else {
        days = sec / SEC_PER_DAY;
    }
    rem = sec % SEC_PER_DAY;
    if (rem < 0) {
        rem += SEC_PER_DAY;
        days--;
    }
    *h = (int)(rem / 3600);
    rem %= 3600;
    *mi = (int)(rem / 60);
    *ss = (int)(rem % 60);
    yy = 1970;
    while (1) {
        int diy = is_leap(yy) ? 366 : 365;
        if (days < diy) {
            break;
        }
        days -= diy;
        yy++;
    }
    month = 1;
    while (month <= 12) {
        int dm = days_in_month(yy, month);
        if (days < dm) {
            break;
        }
        days -= dm;
        month++;
    }
    *y = yy;
    *mo = month;
    *d = (int)days + 1;
}

static int64_t sec_from_utc_parts(int y, int mo, int d, int h, int mi, int ss) {
    int64_t days = 0;
    int yy;
    if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo)) {
        return -1;
    }
    for (yy = 1970; yy < y; yy++) {
        days += is_leap(yy) ? 366 : 365;
    }
    {
        int m;
        for (m = 1; m < mo; m++) {
            days += days_in_month(y, m);
        }
    }
    days += d - 1;
    return days * SEC_PER_DAY + (int64_t)h * 3600 + (int64_t)mi * 60 + (int64_t)ss;
}

static void local_parts_from_utc_sec(int64_t utc_sec, int off_min, int *y, int *mo, int *d, int *h,
                                     int *mi, int *ss) {
    int64_t local = utc_sec + (int64_t)off_min * 60;
    utc_parts_from_sec(local, y, mo, d, h, mi, ss);
}

static void pad2(int n, char *buf) {
    buf[0] = (char)('0' + (n / 10) % 10);
    buf[1] = (char)('0' + n % 10);
    buf[2] = '\0';
}

static int parse_int_n(const char **pp, int n, int *out) {
    int v = 0;
    int i;
    const char *p = *pp;
    for (i = 0; i < n; i++) {
        if (!isdigit((unsigned char)p[i])) {
            return -1;
        }
        v = v * 10 + (p[i] - '0');
    }
    *pp = p + n;
    *out = v;
    return 0;
}

static int parse_iso_datetime(const char *s, int64_t *out_sec) {
    int y, mo, d, h = 0, mi = 0, ss = 0;
    int off_min = 0;
    const char *p = s;
    if (parse_int_n(&p, 4, &y) != 0 || *p++ != '-') {
        return -1;
    }
    if (parse_int_n(&p, 2, &mo) != 0 || *p++ != '-') {
        return -1;
    }
    if (parse_int_n(&p, 2, &d) != 0) {
        return -1;
    }
    if (*p == 'T' || *p == 't' || *p == ' ') {
        p++;
        if (parse_int_n(&p, 2, &h) != 0) {
            return -1;
        }
        if (*p == ':') {
            p++;
            if (parse_int_n(&p, 2, &mi) != 0) {
                return -1;
            }
            if (*p == ':') {
                p++;
                if (parse_int_n(&p, 2, &ss) != 0) {
                    return -1;
                }
            }
        }
    }
    while (*p == '.' || isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == 'Z' || *p == 'z') {
        off_min = 0;
        p++;
    } else if (*p == '+' || *p == '-') {
        int sign = (*p == '-') ? -1 : 1;
        int oh, om = 0;
        p++;
        if (parse_int_n(&p, 2, &oh) != 0) {
            return -1;
        }
        if (*p == ':') {
            p++;
            if (parse_int_n(&p, 2, &om) != 0) {
                return -1;
            }
        }
        if (oh > 23 || om > 59) {
            return -1;
        }
        off_min = sign * (oh * 60 + om);
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    {
        int64_t wall = sec_from_utc_parts(y, mo, d, h, mi, ss);
        if (wall < 0) {
            return -1;
        }
        *out_sec = wall - (int64_t)off_min * 60;
    }
    return 0;
}

static int parse_iso_day_only(const char *s, int64_t *out_sec) {
    int y, mo, d;
    const char *p = s;
    if (parse_int_n(&p, 4, &y) != 0 || *p++ != '-') {
        return -1;
    }
    if (parse_int_n(&p, 2, &mo) != 0 || *p++ != '-') {
        return -1;
    }
    if (parse_int_n(&p, 2, &d) != 0) {
        return -1;
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    {
        int64_t sec = sec_from_utc_parts(y, mo, d, 0, 0, 0);
        if (sec < 0) {
            return -1;
        }
        *out_sec = sec;
    }
    return 0;
}

int64_t flat_date_now_sec(const FlatDateRunState *st) {
    if (st && st->now_override_sec >= 0) {
        return st->now_override_sec;
    }
    return (int64_t)time(NULL);
}

static int apply_modifiers(const char *mod_str, int64_t *sec) {
    const char *p = mod_str;
    int64_t t = *sec;
    while (*p) {
        int sign = 1;
        int val = 0;
        char unit[16];
        int ui = 0;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '+') {
            p++;
        } else if (*p == '-') {
            sign = -1;
            p++;
        } else if (*p == '\0') {
            break;
        } else {
            return -1;
        }
        while (isdigit((unsigned char)*p)) {
            val = val * 10 + (*p - '0');
            p++;
        }
        while (*p && isalpha((unsigned char)*p) && ui + 1 < (int)sizeof unit) {
            unit[ui++] = (char)tolower((unsigned char)*p);
            p++;
        }
        unit[ui] = '\0';
        val *= sign;
        if (strcmp(unit, "y") == 0 || strcmp(unit, "year") == 0 || strcmp(unit, "years") == 0) {
            int y, mo, d, h, mi, ss;
            utc_parts_from_sec(t, &y, &mo, &d, &h, &mi, &ss);
            y += val;
            t = sec_from_utc_parts(y, mo, d, h, mi, ss);
        } else if (strcmp(unit, "month") == 0 || strcmp(unit, "months") == 0) {
            int y, mo, d, h, mi, ss;
            utc_parts_from_sec(t, &y, &mo, &d, &h, &mi, &ss);
            mo += val;
            while (mo > 12) {
                mo -= 12;
                y++;
            }
            while (mo < 1) {
                mo += 12;
                y--;
            }
            if (d > days_in_month(y, mo)) {
                d = days_in_month(y, mo);
            }
            t = sec_from_utc_parts(y, mo, d, h, mi, ss);
        } else if (strcmp(unit, "d") == 0 || strcmp(unit, "day") == 0 || strcmp(unit, "days") == 0) {
            t += (int64_t)val * SEC_PER_DAY;
        } else if (strcmp(unit, "h") == 0 || strcmp(unit, "hour") == 0 || strcmp(unit, "hours") == 0) {
            t += (int64_t)val * 3600;
        } else if (strcmp(unit, "m") == 0 || strcmp(unit, "min") == 0 || strcmp(unit, "minute") == 0 ||
                   strcmp(unit, "minutes") == 0) {
            t += (int64_t)val * 60;
        } else if (strcmp(unit, "s") == 0 || strcmp(unit, "sec") == 0 || strcmp(unit, "second") == 0 ||
                   strcmp(unit, "seconds") == 0 || unit[0] == '\0') {
            t += val;
        } else {
            return -1;
        }
        if (t < 0) {
            return -1;
        }
    }
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    *sec = t;
    return 0;
}

int flat_date_parse_auto(const char *s, int64_t *out_utc_sec, const FlatDateRunState *st) {
    char buf[256];
    int64_t base;
    const char *mods;

    if (!s || !out_utc_sec) {
        return -1;
    }
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return -1;
    }
    if (strcmp(s, "now") == 0 || strcmp(s, "+0") == 0 || strcmp(s, "-0") == 0) {
        *out_utc_sec = flat_date_now_sec(st);
        return 0;
    }
    if (s[0] == '+' || s[0] == '-') {
        base = flat_date_now_sec(st);
        if (apply_modifiers(s, &base) != 0) {
            return -1;
        }
        *out_utc_sec = base;
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", s);
    mods = NULL;
    if ((int)strlen(buf) > 10 && buf[4] == '-' && buf[7] == '-' &&
        (buf[10] == '+' || buf[10] == '-')) {
        char daypart[16];
        memcpy(daypart, buf, 10);
        daypart[10] = '\0';
        snprintf(buf, sizeof buf, "%s", daypart);
        mods = s + 10;
    } else {
        const char *tpos = strchr(buf, 'T');
        if (!tpos) {
            tpos = strchr(buf, 't');
        }
        if (!tpos) {
            size_t bl = strlen(buf);
            size_t i = bl;
            while (i > 0) {
                size_t j = i;
                int has_unit = 0;
                while (j > 0 && isalpha((unsigned char)buf[j - 1])) {
                    has_unit = 1;
                    j--;
                }
                while (j > 0 && isdigit((unsigned char)buf[j - 1])) {
                    j--;
                }
                if (j > 0 && (buf[j - 1] == '+' || buf[j - 1] == '-') && j - 1 > 0 &&
                    has_unit) {
                    mods = buf + j;
                    buf[j - 1] = '\0';
                    while (buf[0] && isspace((unsigned char)buf[strlen(buf) - 1])) {
                        buf[strlen(buf) - 1] = '\0';
                    }
                    break;
                }
                if (i == 0) {
                    break;
                }
                i--;
            }
        }
    }
    if (parse_iso_day_only(buf, &base) == 0) {
        if (mods && apply_modifiers(mods, &base) != 0) {
            return -1;
        }
        *out_utc_sec = base;
        return 0;
    }
    if (parse_iso_datetime(buf, &base) == 0) {
        if (mods && apply_modifiers(mods, &base) != 0) {
            return -1;
        }
        *out_utc_sec = base;
        return 0;
    }
    {
        char *end;
        double n = strtod(buf, &end);
        if (end != buf) {
            while (*end && isspace((unsigned char)*end)) {
                end++;
            }
            if (*end == '\0') {
                int64_t sec;
                if (n >= (double)MS_THRESHOLD || n <= -(double)MS_THRESHOLD) {
                    sec = (int64_t)(n / 1000.0);
                } else {
                    sec = (int64_t)n;
                }
                if (mods && apply_modifiers(mods, &sec) != 0) {
                    return -1;
                }
                *out_utc_sec = sec;
                return 0;
            }
        }
    }
    return -1;
}

int flat_date_parse_strict(const char *s, const char *fmt, int64_t *out_utc_sec,
                           const FlatDateRunState *st) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, ss = 0;
    const char *ps = s;
    const char *pf = fmt;
    int has_date = 0;
    int has_time = 0;
    (void)st;
    if (!s || !fmt || !out_utc_sec || !*fmt) {
        return -1;
    }
    while (*pf) {
        if (strncmp(pf, "YYYY", 4) == 0) {
            if (parse_int_n(&ps, 4, &y) != 0) {
                return -1;
            }
            pf += 4;
            has_date = 1;
        } else if (strncmp(pf, "DD", 2) == 0) {
            if (parse_int_n(&ps, 2, &d) != 0) {
                return -1;
            }
            pf += 2;
            has_date = 1;
        } else if (strncmp(pf, "MM", 2) == 0) {
            if (parse_int_n(&ps, 2, &mo) != 0) {
                return -1;
            }
            pf += 2;
            has_date = 1;
        } else if (strncmp(pf, "HH", 2) == 0) {
            if (parse_int_n(&ps, 2, &h) != 0) {
                return -1;
            }
            pf += 2;
            has_time = 1;
        } else if (strncmp(pf, "mm", 2) == 0) {
            if (parse_int_n(&ps, 2, &mi) != 0) {
                return -1;
            }
            pf += 2;
            has_time = 1;
        } else if (strncmp(pf, "ss", 2) == 0) {
            if (parse_int_n(&ps, 2, &ss) != 0) {
                return -1;
            }
            pf += 2;
            has_time = 1;
        } else if (*pf == *ps) {
            pf++;
            ps++;
        } else {
            return -1;
        }
    }
    while (*ps && isspace((unsigned char)*ps)) {
        ps++;
    }
    if (*ps != '\0') {
        return -1;
    }
    if (!has_date) {
        y = 1970;
        mo = 1;
        d = 1;
    }
    if (!has_time) {
        h = mi = ss = 0;
    }
    {
        int64_t sec = sec_from_utc_parts(y, mo, d, h, mi, ss);
        if (sec < 0) {
            return -1;
        }
        *out_utc_sec = sec;
    }
    return 0;
}

static int append_str(char *out, size_t out_sz, size_t *pos, const char *piece) {
    size_t n = strlen(piece);
    if (*pos + n + 1 > out_sz) {
        return -1;
    }
    memcpy(out + *pos, piece, n);
    *pos += n;
    out[*pos] = '\0';
    return 0;
}

static int append_int(char *out, size_t out_sz, size_t *pos, int v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%d", v);
    return append_str(out, out_sz, pos, tmp);
}

int flat_date_format(int64_t utc_sec, const char *fmt, char *out, size_t out_sz,
                     const FlatDateRunState *st) {
    static const char *const toks[] = {"ISO",     "YYYYU", "MMU", "DDU", "HHU", "hhU", "mmU", "ssU",
                                       "AU",      "Z",     "YYYY", "MM", "DD", "HH", "hh",  "mm",
                                       "ss",      "A"};
    int ly, lmo, ld, lh, lmi, lss;
    int uy, umo, ud, uh, umi, uss;
    int off_min;
    const char *pattern;
    size_t pi = 0;
    size_t op = 0;
    char iso[40];

    if (!out || out_sz == 0) {
        return -1;
    }
    out[0] = '\0';
    pattern = (fmt && fmt[0]) ? fmt : "YYYY-MM-DD";
    off_min = st && st->has_run_offset ? st->run_offset_min : 0;
    utc_parts_from_sec(utc_sec, &uy, &umo, &ud, &uh, &umi, &uss);
    local_parts_from_utc_sec(utc_sec, off_min, &ly, &lmo, &ld, &lh, &lmi, &lss);

    snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02d.000Z", uy, umo, ud, uh, umi, uss);

    while (pattern[pi]) {
        int matched = 0;
        size_t ti;
        for (ti = 0; ti < sizeof toks / sizeof toks[0]; ti++) {
            size_t tl = strlen(toks[ti]);
            if (strncmp(pattern + pi, toks[ti], tl) == 0) {
                char p2[8];
                int h12;
                const char *ampm;
                matched = 1;
                pi += tl;
                if (strcmp(toks[ti], "ISO") == 0) {
                    if (append_str(out, out_sz, &op, iso) != 0) {
                        return -1;
                    }
                    break;
                }
                if (strcmp(toks[ti], "Z") == 0) {
                    int z = -off_min;
                    if (append_int(out, out_sz, &op, z) != 0) {
                        return -1;
                    }
                    break;
                }
                if (strchr(toks[ti], 'U') != NULL) {
                    if (strcmp(toks[ti], "YYYYU") == 0) {
                        append_int(out, out_sz, &op, uy);
                    } else if (strcmp(toks[ti], "MMU") == 0) {
                        pad2(umo, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "DDU") == 0) {
                        pad2(ud, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "HHU") == 0) {
                        pad2(uh, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "hhU") == 0) {
                        h12 = uh % 12;
                        if (h12 == 0) {
                            h12 = 12;
                        }
                        pad2(h12, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "mmU") == 0) {
                        pad2(umi, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "ssU") == 0) {
                        pad2(uss, p2);
                        append_str(out, out_sz, &op, p2);
                    } else if (strcmp(toks[ti], "AU") == 0) {
                        ampm = uh >= 12 ? "PM" : "AM";
                        append_str(out, out_sz, &op, ampm);
                    }
                    break;
                }
                if (strcmp(toks[ti], "YYYY") == 0) {
                    append_int(out, out_sz, &op, ly);
                } else if (strcmp(toks[ti], "MM") == 0) {
                    pad2(lmo, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "DD") == 0) {
                    pad2(ld, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "HH") == 0) {
                    pad2(lh, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "hh") == 0) {
                    h12 = lh % 12;
                    if (h12 == 0) {
                        h12 = 12;
                    }
                    pad2(h12, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "mm") == 0) {
                    pad2(lmi, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "ss") == 0) {
                    pad2(lss, p2);
                    append_str(out, out_sz, &op, p2);
                } else if (strcmp(toks[ti], "A") == 0) {
                    ampm = lh >= 12 ? "PM" : "AM";
                    append_str(out, out_sz, &op, ampm);
                }
                break;
            }
        }
        if (!matched) {
            char ch[2] = {pattern[pi++], '\0'};
            if (append_str(out, out_sz, &op, ch) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int flat_date_value_to_utc_sec(const FlatValue *v, FlatStringPool *pool, int64_t *out_sec,
                               const FlatDateRunState *st) {
    if (!v || !out_sec) {
        return -1;
    }
    if (v->tag == VAL_NUMBER) {
        double n = flat_number_as_f64(v);
        if (n >= (double)MS_THRESHOLD || n <= -(double)MS_THRESHOLD) {
            *out_sec = (int64_t)(n / 1000.0);
        } else {
            *out_sec = (int64_t)n;
        }
        return 0;
    }
    if (v->tag == VAL_STR_INLINE || v->tag == VAL_STR_REF) {
        char buf[256];
        if (v->tag == VAL_STR_INLINE) {
            snprintf(buf, sizeof buf, "%s", v->u.ins.bytes);
        } else {
            size_t len = 0;
            const uint8_t *bp = flat_pool_read(pool, v->u.ref_id, &len);
            if (!bp || len >= sizeof buf) {
                return -1;
            }
            memcpy(buf, bp, len);
            buf[len] = '\0';
        }
        return flat_date_parse_auto(buf, out_sec, st);
    }
    return -1;
}

int flat_date_add_modifier(int64_t base_sec, const char *mod, int64_t *out_sec) {
    if (!mod || !out_sec) {
        return -1;
    }
    *out_sec = base_sec;
    return apply_modifiers(mod, out_sec);
}

int flat_date_str_to_date(const char *s, char *out, size_t out_sz) {
    int64_t sec;
    int y, mo, d, h, mi, ss;
    if (!s || !out || out_sz < 11) {
        return -1;
    }
    if (flat_date_parse_strict(s, "DD.MM.YYYY", &sec, NULL) == 0) {
        utc_parts_from_sec(sec, &y, &mo, &d, &h, &mi, &ss);
        snprintf(out, out_sz, "%04d-%02d-%02d", y, mo, d);
        return 0;
    }
    if (parse_iso_day_only(s, &sec) == 0) {
        utc_parts_from_sec(sec, &y, &mo, &d, &h, &mi, &ss);
        snprintf(out, out_sz, "%04d-%02d-%02d", y, mo, d);
        return 0;
    }
    return -1;
}

int flat_date_str_to_time(const char *s, char *out, size_t out_sz) {
    int64_t sec;
    int y, mo, d, h, mi, ss;
    if (!s || !out) {
        return -1;
    }
    if (flat_date_parse_strict(s, "HH:mm:ss", &sec, NULL) != 0 &&
        flat_date_parse_strict(s, "HH:mm", &sec, NULL) != 0) {
        return -1;
    }
    utc_parts_from_sec(sec, &y, &mo, &d, &h, &mi, &ss);
    snprintf(out, out_sz, "%02d:%02d:%02d", h, mi, ss);
    return 0;
}

int flat_date_str_to_datetime(const char *s, char *out, size_t out_sz) {
    int64_t sec;
    const char *p;
    if (!s || !out) {
        return -1;
    }
    p = s;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strchr(p, 'Z') || strchr(p, 'z') || strchr(p, '+') || (strchr(p, 'T') && strchr(p, '-'))) {
        const char *t = strchr(p, 'T');
        const char *dash = strrchr(p, '-');
        if ((strchr(p, 'Z') || strchr(p, 'z') || strchr(p, '+')) ||
            (t && dash && dash > t)) {
            if (parse_iso_datetime(s, &sec) != 0) {
                return -1;
            }
            return flat_date_iso_z_from_sec(sec, out, out_sz);
        }
    }
    if (parse_iso_datetime(s, &sec) == 0) {
        int y, mo, d, h, mi, ss;
        utc_parts_from_sec(sec, &y, &mo, &d, &h, &mi, &ss);
        snprintf(out, out_sz, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, ss);
        return 0;
    }
    if (flat_date_parse_auto(s, &sec, NULL) != 0) {
        return -1;
    }
    return flat_date_iso_z_from_sec(sec, out, out_sz);
}

int flat_date_iso_z_from_sec(int64_t utc_sec, char *out, size_t out_sz) {
    int y, mo, d, h, mi, ss;
    if (!out || out_sz < 21) {
        return -1;
    }
    utc_parts_from_sec(utc_sec, &y, &mo, &d, &h, &mi, &ss);
    snprintf(out, out_sz, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, h, mi, ss);
    return 0;
}

int flat_date_today_string(char *out, size_t out_sz, const FlatDateRunState *st) {
    int y, mo, d, h, mi, ss;
    int off = st && st->has_run_offset ? st->run_offset_min : 0;
    int64_t now = flat_date_now_sec(st);
    if (!out || out_sz < 11) {
        return -1;
    }
    local_parts_from_utc_sec(now, off, &y, &mo, &d, &h, &mi, &ss);
    snprintf(out, out_sz, "%04d-%02d-%02d", y, mo, d);
    return 0;
}

int flat_date_time_to_sec(const char *t, int *out_sec) {
    int64_t sec;
    int y, mo, d, h, mi, ss;
    if (!t || !out_sec) {
        return -1;
    }
    if (flat_date_parse_strict(t, "HH:mm:ss", &sec, NULL) != 0 &&
        flat_date_parse_strict(t, "HH:mm", &sec, NULL) != 0) {
        return -1;
    }
    utc_parts_from_sec(sec, &y, &mo, &d, &h, &mi, &ss);
    *out_sec = h * 3600 + mi * 60 + ss;
    return 0;
}

int flat_date_sec_to_time(int sec, char *out, size_t out_sz) {
    int h, mi, s;
    if (!out) {
        return -1;
    }
    if (sec < 0 || sec >= 86400) {
        return -1;
    }
    h = sec / 3600;
    sec %= 3600;
    mi = sec / 60;
    s = sec % 60;
    snprintf(out, out_sz, "%02d:%02d:%02d", h, mi, s);
    return 0;
}
