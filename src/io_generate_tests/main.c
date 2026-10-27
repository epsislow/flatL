/*
 * Portable generator for test/cases/io/ (same catalog as legacy gen_cases.sh).
 * No shell required — only a C compiler to build this tool.
 */
#include "repo.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define VM "{root}/bin/flvm {tmp}/out.fb"

typedef struct {
    int id;
    const char *desc;
    const char *prog;
    const char *run;
    int expect_exit;
    const char *stdout_contains;
    const char *stderr_contains;
} IoCase;

static void slug_from_desc(const char *desc, char *out, int out_size) {
    int j = 0;
    int prev_us = 0;
    for (int i = 0; desc[i] && j < out_size - 1; i++) {
        unsigned char c = (unsigned char)desc[i];
        if (c == ' ') {
            if (j > 0 && !prev_us) {
                out[j++] = '_';
                prev_us = 1;
            }
            continue;
        }
        if (isupper(c)) {
            c = (unsigned char)tolower(c);
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            out[j++] = (char)c;
            prev_us = 0;
        }
    }
    while (j > 0 && out[j - 1] == '_') {
        j--;
    }
    out[j] = '\0';
}

static int write_file(const char *path, const char *body) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "io_generate_tests: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (body && fputs(body, f) == EOF) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int emit_case(const char *io_dir, const IoCase *c) {
    char slug[128];
    char dir[512];
    char prog_path[560];
    char toml_path[560];

    slug_from_desc(c->desc, slug, (int)sizeof slug);
    snprintf(dir, sizeof dir, "%s/%03d_%s", io_dir, c->id, slug);
    snprintf(prog_path, sizeof prog_path, "%s/program.fl", dir);
    snprintf(toml_path, sizeof toml_path, "%s/case.toml", dir);

    if (MKDIR(dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "io_generate_tests: mkdir %s: %s\n", dir, strerror(errno));
        return -1;
    }
    if (write_file(prog_path, c->prog) != 0) {
        return -1;
    }

    {
        FILE *f = fopen(toml_path, "wb");
        if (!f) {
            fprintf(stderr, "io_generate_tests: cannot write %s\n", toml_path);
            return -1;
        }
        fprintf(f, "suite = \"io\"\n");
        fprintf(f, "id = %d\n", c->id);
        fprintf(f, "desc = \"%s\"\n", c->desc);
        fprintf(f, "kind = \"pipeline\"\n\n");
        fprintf(f, "source = \"program.fl\"\n");
        fprintf(f, "compile = \"{root}/bin/flc -o {tmp}/out.fb {src}\"\n");
        fprintf(f, "run = \"%s\"\n", c->run);
        fprintf(f, "expect_exit = %d\n", c->expect_exit);
        if (c->stdout_contains) {
            fprintf(f, "stdout_contains = \"%s\"\n", c->stdout_contains);
        }
        if (c->stderr_contains) {
            fprintf(f, "stderr_contains = \"%s\"\n", c->stderr_contains);
        }
        if (fclose(f) != 0) {
            return -1;
        }
    }
    return 0;
}

static const IoCase CASES[] = {
    {1, "outln string", "#main\n  io.outln('hi')\n  OK\n", "printf '' | " VM, 0, "hi", NULL},
    {2, "out no newline", "#main\n  io.out('ab')\n  io.out('cd')\n  io.outln('')\n  OK\n",
     "printf '' | " VM, 0, "abcd", NULL},
    {3, "outvar number", "#main\n  n = 42\n  io.outvar(n)\n  OK\n", "printf '' | " VM, 0, "42", NULL},
    {4, "out bool true", "#main\n  ok = true\n  io.outln(ok)\n  OK\n", "printf '' | " VM, 0, "true",
     NULL},
    {5, "out list", "#main\n  xs = [1, 2]\n  io.outln(xs)\n  OK\n", "printf '' | " VM, 0, "1,2", NULL},
    {6, "out object", "#main\n  o = { a: 1, b: 'x' }\n  io.outln(o)\n  OK\n", "printf '' | " VM, 0,
     "\"a\":1", NULL},
    {7, "inln arrow", "#main\n  io.inln() -> line\n  io.outln(line)\n  OK\n", "printf 'hello\\n' | " VM,
     0, "hello", NULL},
    {8, "inln assign", "#main\n  line = io.inln()\n  io.outln(line)\n  OK\n", "printf 'world\\n' | " VM,
     0, "world", NULL},
    {9, "inln prompt", "#main\n  io.inln('Name:') -> name\n  io.outln(name)\n  OK\n",
     "printf 'Ann\\n' | " VM, 0, "Name:Ann", NULL},
    {10, "in_num", "#main\n  io.in_num() -> n\n  io.outln(n)\n  OK\n", "printf '3.5\\n' | " VM, 0,
     "3.5", NULL},
    {11, "in_bool yes", "#main\n  io.in_bool() -> ok\n  io.outln(ok)\n  OK\n", "printf 'yes\\n' | " VM,
     0, "true", NULL},
    {12, "in_bool n", "#main\n  io.in_bool() -> ok\n  io.outln(ok)\n  OK\n", "printf 'n\\n' | " VM, 0,
     "false", NULL},
    {13, "in_bool cancel q",
     "#main\n  io.in_bool() -> ok\n  io.outln('bad')\n  OK\n\n#panic.any\n  io.outln('cancelled')\n  OK\n",
     "printf 'q\\n' | " VM, 0, "cancelled", NULL},
    {14, "inln empty cancel",
     "#main\n  io.inln() -> x\n  OK\n\n#panic.any\n  io.outln('empty')\n  OK\n", "printf '\\n' | " VM, 0,
     "empty", NULL},
    {15, "str to num arrow", "#main\n  raw = '12'\n  str.to_num(raw) -> n\n  io.outln(n)\n  OK\n",
     "printf '' | " VM, 0, "12", NULL},
    {16, "str to bool assign", "#main\n  b = str.to_bool('on')\n  io.outln(b)\n  OK\n",
     "printf '' | " VM, 0, "true", NULL},
    {17, "str to num invalid assign fail",
     "#main\n  n = 0\n  n = str.to_num('not-a-num')\n  n == 0\n  OK\n", "printf '' | " VM, 0, "", NULL},
    {18, "in_num invalid panic",
     "#main\n  io.in_num() -> n\n  OK\n\n#panic.any\n  io.outln('parse')\n  OK\n", "printf 'xx\\n' | " VM,
     0, "parse", NULL},
    {19, "foreach out",
     "#main\n  xs = ['a', 'b']\n  foreach(xs, k, '#loop')\n  OK\n#loop\n  io.out(k)\n  return",
     "printf '' | " VM, 0, "ab", NULL},
    {20, "unhandled panic stderr", "#main\n  io.inln() -> x\n  OK\n", "printf '\\n' | " VM, 1,
     "unhandled panic", NULL},
    {21, "bool tokens false off", "#main\n  b = str.to_bool('OFF')\n  io.outln(b)\n  OK\n",
     "printf '' | " VM, 0, "false", NULL},
    {22, "bool token t", "#main\n  b = str.to_bool('T')\n  io.outln(b)\n  OK\n", "printf '' | " VM, 0,
     "true", NULL},
    {23, "in_num prompt", "#main\n  io.in_num('Price:') -> p\n  io.outln(p)\n  OK\n",
     "printf '9\\n' | " VM, 0, "Price:9", NULL},
    {24, "assign arrow in_num", "#main\n  io.in_num() -> x\n  x == 2\n  OK\n", "printf '2\\n' | " VM, 0,
     "", NULL},
    {25, "out number no ln", "#main\n  io.out(1)\n  io.out(2)\n  io.outln(3)\n  OK\n",
     "printf '' | " VM, 0, "123", NULL},
    {26, "inln cancel word is data", "#main\n  io.inln() -> s\n  io.outln(s)\n  OK\n",
     "printf 'cancel\\n' | " VM, 0, "cancel", NULL},
    {27, "panic eof", "#main\n  io.inln() -> x\n  OK\n\n#panic.any\n  io.outln('eof')\n  OK\n",
     "printf '' | " VM, 0, "eof", NULL},
    {28, "str to bool invalid", "#main\n  ok = false\n  ok = str.to_bool('maybe')\n  ok == false\n  OK\n",
     "printf '' | " VM, 0, "", NULL},
    {29, "values outvar bool", "#main\n  ok = false\n  io.outvar(ok)\n  OK\n", "printf '' | " VM, 0,
     "false", NULL},
    {30, "mixed out and guard", "#main\n  io.outln('start')\n  1 == 1\n  io.outln('end')\n  OK\n",
     "printf '' | " VM, 0, "start", NULL},
};

int main(void) {
    char root[512];
    char io_dir[560];
    size_t n = sizeof CASES / sizeof CASES[0];

    if (flat_find_repo_root(root, (int)sizeof root) != 0) {
        fprintf(stderr, "io_generate_tests: run from inside the flatL repo (need Makefile + include/flat.h)\n");
        return 1;
    }
    snprintf(io_dir, sizeof io_dir, "%s/test/cases/io", root);

    for (size_t i = 0; i < n; i++) {
        if (emit_case(io_dir, &CASES[i]) != 0) {
            return 1;
        }
    }
    printf("Generated %zu io cases under %s\n", n, io_dir);
    return 0;
}
