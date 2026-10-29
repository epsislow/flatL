#include "fgir.h"
#include "flat_native.h"
#include "flat_str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) {
        *e-- = '\0';
    }
    return s;
}

static int line_starts_with(const char *p, const char *prefix) {
    return strncmp(p, prefix, strlen(prefix)) == 0;
}

static int line_is_block_comment_open(const char *p) {
    return line_starts_with(p, "--<");
}

static int line_is_block_comment_close(const char *p) {
    return line_starts_with(p, "-->");
}

static int line_is_line_comment(const char *p) {
    return line_starts_with(p, "--") && !line_is_block_comment_open(p);
}

static int starts_with_kw(const char *s, const char *kw) {
    size_t n = strlen(kw);
    return strncmp(s, kw, n) == 0 &&
           (s[n] == '\0' || isspace((unsigned char)s[n]) || s[n] == '(');
}

static FgirInstrKind classify(const char *line) {
    if (strcmp(line, "OK") == 0) {
        return FGIR_OK;
    }
    if (strcmp(line, "FAIL") == 0) {
        return FGIR_FAIL;
    }
    if (strcmp(line, "PANIC") == 0) {
        return FGIR_RAISE_PANIC;
    }
    if (starts_with_kw(line, "return")) {
        return FGIR_RETURN;
    }
    if (starts_with_kw(line, "foreach")) {
        return FGIR_FOREACH;
    }
    if (starts_with_kw(line, "call")) {
        return FGIR_CALL;
    }
    if (flat_native_is_call_line(line)) {
        return FGIR_IO;
    }
    if (strstr(line, "==") != NULL) {
        return FGIR_GUARD;
    }
    if (strchr(line, '=') != NULL) {
        return FGIR_ASSIGN;
    }
    return FGIR_ASSIGN;
}

static int parse_block_header(char *line, FgirBlock *b) {
    char *p = line + 1;
    char *paren = strchr(p, '(');
    b->param_count = 0;
    if (paren) {
        *paren = '\0';
        snprintf(b->label, sizeof b->label, "#%s", trim(p));
        {
            char args[256];
            char *end = strchr(paren + 1, ')');
            if (!end) {
                return -1;
            }
            *end = '\0';
            snprintf(args, sizeof args, "%s", paren + 1);
            {
                char *ap = args;
                char *tok;
                while (b->param_count < FGIR_MAX_PARAMS &&
                       (tok = flat_str_next(&ap, ',')) != NULL) {
                    snprintf(b->params[b->param_count], 64, "%s", trim(tok));
                    b->param_count++;
                }
            }
        }
    } else {
        snprintf(b->label, sizeof b->label, "#%s", trim(p));
    }
    return 0;
}

static void parse_call_extras(FgirInstr *ins) {
    const char *s = ins->text;
    const char *hash = strstr(s, "#");
    const char *arrow = strstr(s, "->");
    if (hash) {
        const char *start = hash;
        const char *end = start + 1;
        while (*end && *end != '(' && *end != ' ' && *end != '\t') {
            end++;
        }
        snprintf(ins->call_target, sizeof ins->call_target, "%.*s", (int)(end - start), start);
    }
    if (arrow) {
        snprintf(ins->call_out, sizeof ins->call_out, "%s", trim((char *)(arrow + 2)));
    }
}

static void parse_io_bind(FgirInstr *ins) {
    char *arrow = strstr(ins->text, "->");
    if (!arrow) {
        ins->call_out[0] = '\0';
        return;
    }
    {
        char *bind = trim(arrow + 2);
        *arrow = '\0';
        trim(ins->text);
        snprintf(ins->call_out, sizeof ins->call_out, "%s", bind);
    }
}

static void parse_foreach_extras(FgirInstr *ins) {
    /* foreach(coll, item, '#block') */
    const char *s = ins->text;
    const char *lp = strchr(s, '(');
    const char *rp = strrchr(s, ')');
    if (!lp || !rp || rp <= lp) {
        return;
    }
    {
        char inner[FGIR_MAX_LINE];
        char *ip;
        char *tok;
        int part = 0;
        snprintf(inner, sizeof inner, "%.*s", (int)(rp - lp - 1), lp + 1);
        ip = inner;
        while ((tok = flat_str_next(&ip, ',')) != NULL) {
            char *t = trim(tok);
            if (part == 1) {
                snprintf(ins->foreach_item, sizeof ins->foreach_item, "%s", t);
            } else if (part == 2) {
                if (t[0] == '\'' || t[0] == '"') {
                    t[strlen(t) - 1] = '\0';
                    t++;
                }
                snprintf(ins->foreach_block, sizeof ins->foreach_block, "%s", t);
            }
            part++;
        }
    }
}

int flat_parse_file(const char *path, FgirProgram *out, char *err, size_t err_size) {
    FILE *f;
    char line[FGIR_MAX_LINE + 8];
    int line_no = 0;
    int cur = -1;
    int in_block_comment = 0;

    if (!path || !out) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->main_index = -1;

    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "cannot open %s", path);
        return -1;
    }

    while (fgets(line, sizeof line, f)) {
        char *p;
        line_no++;
        p = trim(line);
        if (in_block_comment) {
            if (line_is_block_comment_close(p)) {
                in_block_comment = 0;
                continue;
            }
            if (line_is_block_comment_open(p)) {
                snprintf(err, err_size, "%s:%d: nested block comment", path, line_no);
                fclose(f);
                return -1;
            }
            continue;
        }
        if (*p == '\0') {
            continue;
        }
        if (line_is_block_comment_open(p)) {
            in_block_comment = 1;
            continue;
        }
        if (line_is_line_comment(p)) {
            continue;
        }
        if (*p == '#') {
            if (out->block_count >= FGIR_MAX_BLOCKS) {
                snprintf(err, err_size, "%s:%d: too many blocks", path, line_no);
                fclose(f);
                return -1;
            }
            cur = out->block_count++;
            if (parse_block_header(p, &out->blocks[cur]) != 0) {
                snprintf(err, err_size, "%s:%d: bad block header", path, line_no);
                fclose(f);
                return -1;
            }
            if (strcmp(out->blocks[cur].label, "#main") == 0) {
                if (out->main_index >= 0) {
                    snprintf(err, err_size, "%s:%d: duplicate #main", path, line_no);
                    fclose(f);
                    return -1;
                }
                out->main_index = cur;
            }
            continue;
        }
        if (cur < 0) {
            snprintf(err, err_size, "%s:%d: instruction outside block", path, line_no);
            fclose(f);
            return -1;
        }
        {
            FgirBlock *b = &out->blocks[cur];
            FgirInstr *ins;
            if (b->instr_count >= FGIR_MAX_INSTR) {
                snprintf(err, err_size, "%s:%d: too many instructions", path, line_no);
                fclose(f);
                return -1;
            }
            ins = &b->instr[b->instr_count];
            memset(ins, 0, sizeof *ins);
            snprintf(ins->text, sizeof ins->text, "%s", p);
            ins->kind = classify(p);
            ins->index = b->instr_count + 1;
            if (ins->kind == FGIR_CALL) {
                parse_call_extras(ins);
            }
            if (ins->kind == FGIR_FOREACH) {
                parse_foreach_extras(ins);
            }
            if (ins->kind == FGIR_IO) {
                parse_io_bind(ins);
            }
            b->instr_count++;
        }
    }
    fclose(f);

    if (in_block_comment) {
        snprintf(err, err_size, "%s: unclosed block comment", path);
        return -1;
    }

    if (out->main_index < 0) {
        snprintf(err, err_size, "%s: missing #main", path);
        return -1;
    }
    return 0;
}

void flat_fgir_dump(const FgirProgram *prog, FILE *out) {
    int i, j;
    fprintf(out, "FGIR blocks=%d main_index=%d\n", prog->block_count, prog->main_index);
    for (i = 0; i < prog->block_count; i++) {
        const FgirBlock *b = &prog->blocks[i];
        fprintf(out, "block %s (%d instr)\n", b->label, b->instr_count);
        for (j = 0; j < b->instr_count; j++) {
            fprintf(out, "  %d: %s\n", b->instr[j].index, b->instr[j].text);
        }
    }
}
