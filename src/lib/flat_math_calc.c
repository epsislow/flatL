#include "flat_math_calc.h"
#include "flat_native_util.h"
#include "flat_obj_path.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const FlatValue *vars;
    int var_count;
    const char (*var_names)[64];
    FlatStringPool *pool;
    FlatCollectionPool *coll;
    FlatMathCalcErr err;
} CalcParser;

static void set_err(CalcParser *cp, FlatMathCalcErr e) {
    if (cp && cp->err == FLAT_MATH_CALC_OK) {
        cp->err = e;
    }
}

static void skip_ws(CalcParser *cp) {
    while (cp->p && *cp->p != '\0' && isspace((unsigned char)*cp->p)) {
        cp->p++;
    }
}

static int at_end(CalcParser *cp) {
    skip_ws(cp);
    return cp->p[0] == '\0';
}

static double parse_expr(CalcParser *cp);
static double parse_unary(CalcParser *cp);
static double parse_pow(CalcParser *cp);

static int parse_number(CalcParser *cp, double *out) {
    char *end = NULL;
    double v;

    skip_ws(cp);
    if (*cp->p == '\0') {
        set_err(cp, FLAT_MATH_CALC_SYNTAX);
        return -1;
    }
    v = strtod(cp->p, &end);
    if (end == cp->p) {
        set_err(cp, FLAT_MATH_CALC_SYNTAX);
        return -1;
    }
    cp->p = end;
    *out = v;
    return 0;
}

static int is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int parse_ident(CalcParser *cp, char *name, size_t name_sz) {
    size_t n = 0;

    skip_ws(cp);
    if (!is_ident_start(*cp->p)) {
        set_err(cp, FLAT_MATH_CALC_SYNTAX);
        return -1;
    }
    while (is_ident_char(cp->p[n])) {
        n++;
    }
    if (n + 1 > name_sz) {
        set_err(cp, FLAT_MATH_CALC_SYNTAX);
        return -1;
    }
    memcpy(name, cp->p, n);
    name[n] = '\0';
    cp->p += n;
    return 0;
}

static double floor_mod(double a, double b) {
    return a - floor(a / b) * b;
}

static int finite_result(double x, CalcParser *cp) {
    if (isnan(x) || isinf(x)) {
        set_err(cp, FLAT_MATH_CALC_RANGE);
        return -1;
    }
    return 0;
}

static double parse_atom(CalcParser *cp) {
    double v;
    char name[64];
    int vi;

    skip_ws(cp);
    if (*cp->p == '(') {
        cp->p++;
        v = parse_expr(cp);
        skip_ws(cp);
        if (*cp->p != ')') {
            set_err(cp, FLAT_MATH_CALC_SYNTAX);
            return 0.0;
        }
        cp->p++;
        return v;
    }
    if (is_ident_start(*cp->p)) {
        if (parse_ident(cp, name, sizeof name) != 0) {
            return 0.0;
        }
        vi = flat_native_find_var(name, cp->var_count, cp->var_names);
        if (vi < 0) {
            set_err(cp, FLAT_MATH_CALC_UNKNOWN_VAR);
            return 0.0;
        }
        skip_ws(cp);
        if (*cp->p == '.' || *cp->p == '[') {
            FlatPathErr pe = FLAT_PATH_OK;
            double v = flat_path_resolve_number(name, &cp->p, cp->coll, cp->pool, cp->vars,
                                                cp->var_count, cp->var_names, &pe);
            if (pe != FLAT_PATH_OK) {
                set_err(cp, FLAT_MATH_CALC_TYPE);
                return 0.0;
            }
            return v;
        }
        if (cp->vars[vi].tag != VAL_NUMBER) {
            set_err(cp, FLAT_MATH_CALC_TYPE);
            return 0.0;
        }
        return cp->vars[vi].u.number;
    }
    if (parse_number(cp, &v) != 0) {
        return 0.0;
    }
    return v;
}

/* Right-associative power; operands are atoms (numbers, vars, parens). */
static double parse_pow(CalcParser *cp) {
    double left;
    double right;

    left = parse_atom(cp);
    if (cp->err != FLAT_MATH_CALC_OK) {
        return 0.0;
    }
    for (;;) {
        skip_ws(cp);
        if (*cp->p != '^') {
            break;
        }
        cp->p++;
        right = parse_unary(cp);
        if (cp->err != FLAT_MATH_CALC_OK) {
            return 0.0;
        }
        left = pow(left, right);
        if (finite_result(left, cp) != 0) {
            return 0.0;
        }
    }
    return left;
}

/* Unary +/- binds below ^: -2^2 → -(2^2). */
static double parse_unary(CalcParser *cp) {
    skip_ws(cp);
    if (*cp->p == '-' || *cp->p == '+') {
        int neg = (*cp->p == '-');
        cp->p++;
        return neg ? -parse_pow(cp) : parse_pow(cp);
    }
    return parse_pow(cp);
}

static double parse_mul(CalcParser *cp) {
    double left;
    double right;
    int op;

    left = parse_unary(cp);
    if (cp->err != FLAT_MATH_CALC_OK) {
        return 0.0;
    }
    for (;;) {
        skip_ws(cp);
        if (*cp->p == '%' && cp->p[1] == '%') {
            op = 2;
            cp->p += 2;
        } else if (*cp->p == '%') {
            op = 1;
            cp->p++;
        } else if (*cp->p == '*') {
            op = 0;
            cp->p++;
        } else if (*cp->p == '/') {
            op = 3;
            cp->p++;
        } else {
            break;
        }
        right = parse_unary(cp);
        if (cp->err != FLAT_MATH_CALC_OK) {
            return 0.0;
        }
        if (op == 3 || op == 1 || op == 2) {
            if (right == 0.0) {
                set_err(cp, FLAT_MATH_CALC_DIVZERO);
                return 0.0;
            }
        }
        if (op == 0) {
            left = left * right;
        } else if (op == 3) {
            left = left / right;
        } else if (op == 1) {
            left = fmod(left, right);
        } else {
            left = floor_mod(left, right);
        }
        if (finite_result(left, cp) != 0) {
            return 0.0;
        }
    }
    return left;
}

static double parse_add(CalcParser *cp) {
    double left;
    double right;

    left = parse_mul(cp);
    if (cp->err != FLAT_MATH_CALC_OK) {
        return 0.0;
    }
    for (;;) {
        skip_ws(cp);
        if (*cp->p == '+') {
            cp->p++;
        } else if (*cp->p == '-') {
            cp->p++;
            right = parse_mul(cp);
            if (cp->err != FLAT_MATH_CALC_OK) {
                return 0.0;
            }
            left = left - right;
            if (finite_result(left, cp) != 0) {
                return 0.0;
            }
            continue;
        } else {
            break;
        }
        right = parse_mul(cp);
        if (cp->err != FLAT_MATH_CALC_OK) {
            return 0.0;
        }
        left = left + right;
        if (finite_result(left, cp) != 0) {
            return 0.0;
        }
    }
    return left;
}

static double parse_expr(CalcParser *cp) {
    return parse_add(cp);
}

const char *flat_math_calc_reason(FlatMathCalcErr err, char *buf, size_t buf_sz) {
    const char *sub = "math.calc";

    switch (err) {
    case FLAT_MATH_CALC_SYNTAX:
        sub = "math.calc.syntax";
        break;
    case FLAT_MATH_CALC_UNKNOWN_VAR:
        sub = "math.calc.unknown_var";
        break;
    case FLAT_MATH_CALC_TYPE:
        sub = "math.calc.type";
        break;
    case FLAT_MATH_CALC_DIVZERO:
        sub = "math.calc.divzero";
        break;
    case FLAT_MATH_CALC_RANGE:
        sub = "math.calc.range";
        break;
    default:
        sub = "math.calc";
        break;
    }
    if (buf && buf_sz > 0) {
        snprintf(buf, buf_sz, "%s", sub);
    }
    return sub;
}

FlatMathCalcErr flat_math_calc_eval(const char *expr, const FlatValue *vars, int var_count,
                                    const char (*var_names)[64], FlatStringPool *pool,
                                    FlatCollectionPool *coll, double *out) {
    CalcParser cp;
    double r;

    if (!expr || !out) {
        return FLAT_MATH_CALC_SYNTAX;
    }
    cp.p = expr;
    cp.vars = vars;
    cp.var_count = var_count;
    cp.var_names = var_names;
    cp.pool = pool;
    cp.coll = coll;
    cp.err = FLAT_MATH_CALC_OK;
    r = parse_expr(&cp);
    if (cp.err != FLAT_MATH_CALC_OK) {
        return cp.err;
    }
    if (!at_end(&cp)) {
        return FLAT_MATH_CALC_SYNTAX;
    }
    if (finite_result(r, &cp) != 0) {
        return cp.err;
    }
    *out = r;
    return FLAT_MATH_CALC_OK;
}
