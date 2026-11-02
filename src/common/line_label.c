#include "flat_line_label.h"
#include "flat_guard.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_line_ident(const char *s) {
    size_t n;
    size_t i;

    if (!s || !s[0]) {
        return 0;
    }
    if (!isalpha((unsigned char)s[0])) {
        return 0;
    }
    n = strlen(s);
    if (n >= 64) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_') {
            return 0;
        }
    }
    return 1;
}

int flat_line_label_suffix_is_index(const char *suffix) {
    const char *p;

    if (!suffix || !suffix[0]) {
        return 0;
    }
    for (p = suffix; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

int flat_line_label_strip_eol(char *line, char *label_out, size_t label_sz, const char *path,
                              int line_no, char *err, size_t err_size) {
    char *dot;
    char *ident;
    char *end;

    if (label_out && label_sz > 0) {
        label_out[0] = '\0';
    }
    if (!line) {
        return 0;
    }
    end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    dot = strrchr(line, '#');
    if (!dot || dot[1] != '.') {
        return 0;
    }
    if (dot > line && !isspace((unsigned char)dot[-1])) {
        return 0;
    }
    ident = dot + 2;
    if (!valid_line_ident(ident)) {
        if (ident[0] == '\0') {
            snprintf(err, err_size, "%s:%d: invalid line label token", path, line_no);
        } else {
            snprintf(err, err_size, "%s:%d: invalid line label token", path, line_no);
        }
        return -1;
    }
    if (label_out && label_sz > 0) {
        snprintf(label_out, label_sz, "%s", ident);
    }
    *dot = '\0';
    end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return 1;
}

static int find_block(const FgirProgram *prog, const char *label) {
    int i;

    for (i = 0; i < prog->block_count; i++) {
        if (strcmp(prog->blocks[i].label, label) == 0) {
            return i;
        }
    }
    return -1;
}

int flat_line_label_instr_index(const FgirProgram *prog, const char *block_label,
                                const char *line_ident) {
    int bi;
    int j;

    if (!prog || !block_label || !line_ident || !line_ident[0]) {
        return -1;
    }
    bi = find_block(prog, block_label);
    if (bi < 0) {
        return -1;
    }
    for (j = 0; j < prog->blocks[bi].instr_count; j++) {
        const FgirInstr *ins = &prog->blocks[bi].instr[j];
        if (ins->line_label[0] && strcmp(ins->line_label, line_ident) == 0) {
            return ins->index;
        }
    }
    return -1;
}

int flat_line_label_validate_unit(const FgirProgram *unit, const char *path, char *err,
                                  size_t err_size) {
    int bi;

    if (!unit) {
        return -1;
    }
    for (bi = 0; bi < unit->block_count; bi++) {
        const FgirBlock *b = &unit->blocks[bi];
        int j;
        for (j = 0; j < b->instr_count; j++) {
            const char *lab = b->instr[j].line_label;
            int k;

            if (!lab[0]) {
                continue;
            }
            for (k = j + 1; k < b->instr_count; k++) {
                if (b->instr[k].line_label[0] &&
                    strcmp(b->instr[k].line_label, lab) == 0) {
                    snprintf(err, err_size, "%s: duplicate line label #.%s in %s", path, lab,
                             b->label);
                    return -1;
                }
            }
        }
    }
    return 0;
}

int flat_line_label_resolve_handler(const FgirProgram *prog, const char *handler_label,
                                    char *canonical_out, size_t canonical_sz, char *err,
                                    size_t err_size) {
    const char *rest;
    const char *dot;
    char target[FGIR_MAX_LABEL];
    const char *suffix;
    int idx;

    if (!prog || !handler_label || !handler_label[0]) {
        return 0;
    }
    rest = flat_or_handler_body(handler_label);
    if (rest) {
        /* or handler */
    } else if (strncmp(handler_label, "#panic.", 7) == 0) {
        rest = handler_label + 7;
    } else {
        return 0;
    }
    dot = strrchr(rest, '.');
    if (!dot || dot == rest) {
        return 0;
    }
    snprintf(target, sizeof target, "%.*s", (int)(dot - rest), rest);
    suffix = dot + 1;
    if (flat_line_label_suffix_is_index(suffix)) {
        return 0;
    }
    idx = flat_line_label_instr_index(prog, target, suffix);
    if (idx < 0) {
        snprintf(err, err_size, "unknown line label %s in %s", suffix, target);
        return -1;
    }
    if (flat_or_handler_body(handler_label) != NULL) {
        snprintf(canonical_out, canonical_sz, FLAT_OR_HANDLER_PREFIX "%.*s.%d", (int)(dot - rest),
                 rest, idx);
    } else {
        snprintf(canonical_out, canonical_sz, "#panic.%.*s.%d", (int)(dot - rest), rest, idx);
    }
    return 1;
}
