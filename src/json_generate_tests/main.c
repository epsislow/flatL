/*
 * Generates test/cases/json/ from catalog below.
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
} JsonCase;

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
        return -1;
    }
    if (body && fputs(body, f) == EOF) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int emit_case(const char *json_dir, const JsonCase *c) {
    char slug[128];
    char dir[512];
    char prog_path[560];
    char toml_path[560];

    slug_from_desc(c->desc, slug, (int)sizeof slug);
    snprintf(dir, sizeof dir, "%s/%03d_%s", json_dir, c->id, slug);
    snprintf(prog_path, sizeof prog_path, "%s/program.fl", dir);
    snprintf(toml_path, sizeof toml_path, "%s/case.toml", dir);

    if (MKDIR(dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "json_generate_tests: mkdir %s: %s\n", dir, strerror(errno));
        return -1;
    }
    if (write_file(prog_path, c->prog) != 0) {
        return -1;
    }
    {
        FILE *f = fopen(toml_path, "wb");
        if (!f) {
            return -1;
        }
        fprintf(f, "suite = \"json\"\n");
        fprintf(f, "id = %d\n", c->id);
        fprintf(f, "desc = \"%s\"\n", c->desc);
        fprintf(f, "kind = \"pipeline\"\n\n");
        fprintf(f, "source = \"program.fl\"\n");
        fprintf(f, "compile = \"{root}/bin/flc -o {tmp}/out.fb {src}\"\n");
        fprintf(f, "run = \"%s\"\n", c->run);
        fprintf(f, "expect_exit = %d\n", c->expect_exit);
        if (c->stdout_contains && c->stdout_contains[0] != '\0') {
            fprintf(f, "stdout_contains = \"%s\"\n", c->stdout_contains);
        }
        if (fclose(f) != 0) {
            return -1;
        }
    }
    return 0;
}

static const JsonCase CASES[] = {
    {1, "parse list simple numbers", "#main\n  xs = json.parse_list('1,2,0')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "1,2,0"},
    {2, "null literal and guard", "#main\n  io.outln(null)\n  x = null\n  x == null\n  OK\n",
     "printf '' | " VM, 0, "null"},
    {3, "parse object json", "#main\n  o = json.parse_object('{\"a\":1}')\n  io.outln(o)\n  OK\n",
     "printf '' | " VM, 0, ":1"},
    {4, "in list enter empty", "#main\n  io.in_list() -> xs\n  io.outln(xs)\n  OK\n",
     "printf '\\n' | " VM, 0, ""},
    {5, "in list trailing comma", "#main\n  io.in_list('P:') -> xs\n  io.outln(xs)\n  OK\n",
     "printf '1,2,\\n' | " VM, 0, "P:1,2,0"},
    {6, "in object enter empty", "#main\n  io.in_object() -> o\n  io.outln(o)\n  OK\n",
     "printf '\\n' | " VM, 0, "{}"},
    {7, "parse list all strings", "#main\n  xs = json.parse_list('Mark, Joe')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "Mark"},
    {8, "parse bools empty slots", "#main\n  bs = json.parse_bools('yes,,no')\n  io.outln(bs)\n  OK\n",
     "printf '' | " VM, 0, "true,false,false"},
    {9, "parse empty is null", "#main\n  x = json.parse('')\n  x == null\n  OK\n",
     "printf '' | " VM, 0, ""},
    {10, "parse list empty string", "#main\n  xs = json.parse_list('')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, ""},
    {11, "comma two zeros", "#main\n  xs = json.parse_list(',')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "0,0"},
    {12, "comma three zeros", "#main\n  xs = json.parse_list(',,')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "0,0,0"},
    {13, "single number token", "#main\n  xs = json.parse_list('32')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "32"},
    {14, "json array mode", "#main\n  xs = json.parse_list('[1, 2, 3]')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "1,2,3"},
    {15, "in list json array", "#main\n  io.in_list() -> xs\n  io.outln(xs)\n  OK\n",
     "printf '[9,8]\\n' | " VM, 0, "9,8"},
    {16, "in object json line", "#main\n  io.in_object('J:') -> o\n  io.outln(o)\n  OK\n",
     "printf '{\"k\":1}\\n' | " VM, 0, "J:"},
    {17, "mixed forces strings", "#main\n  xs = json.parse_list('1, TV')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "\"1\""},
    {18, "parse list fail silent", "#main\n  n = 5\n  n = json.parse_list('[')\n  n == 5\n  OK\n",
     "printf '' | " VM, 0, ""},
    {19, "stringify sorted keys", "#main\n  o = json.parse_object('{\"b\":2,\"a\":1}')\n  io.outln(o)\n  OK\n",
     "printf '' | " VM, 0, "a"},
    {20, "out empty list blank", "#main\n  xs = json.parse_list('')\n  io.outln(xs)\n  io.out('x')\n  OK\n",
     "printf '' | " VM, 0, "x"},
    {21, "null in object value", "#main\n  o = json.parse_object('{\"x\":null}')\n  io.outln(o)\n  OK\n",
     "printf '' | " VM, 0, "null"},
    {22, "in bools enter empty", "#main\n  io.in_bools() -> bs\n  io.outln(bs)\n  OK\n",
     "printf '\\n' | " VM, 0, ""},
    {23, "in bools simple", "#main\n  io.in_bools() -> bs\n  io.outln(bs)\n  OK\n",
     "printf 'yes,no\\n' | " VM, 0, "true,false"},
    {24, "in bools cancel recovered",
     "#main\n  io.in_bools() -> bs\n  io.outln('bad')\n  OK\n\n#panic.any\n  io.outln('cancelled')\n  OK\n",
     "printf 'q\\n' | " VM, 0, "cancelled"},
    {25, "in list parse panic recovered",
     "#main\n  io.in_list() -> xs\n  io.outln('bad')\n  OK\n\n#panic.any\n  io.outln('parse')\n  OK\n",
     "printf '[1,,2]\\n' | " VM, 0, "parse"},
    {26, "parse object invalid silent", "#main\n  n = 1\n  n = json.parse_object('[]')\n  n == 1\n  OK\n",
     "printf '' | " VM, 0, ""},
    {27, "parse null keyword", "#main\n  x = json.parse('null')\n  x == null\n  OK\n",
     "printf '' | " VM, 0, ""},
    {28, "leading comma list", "#main\n  xs = json.parse_list(',1')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "0,1"},
    {29, "string empty slot", "#main\n  xs = json.parse_list('a,,b')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "\"\""},
    {30, "parse floats list", "#main\n  xs = json.parse_list('1.5,-2')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "1.5"},
    {31, "guard null fails", "#main\n  x = 1\n  x == null\n  FAIL\n  OK\n",
     "printf '' | " VM, 2, ""},
    {32, "parse bools json array", "#main\n  bs = json.parse_bools('[true,false]')\n  io.outln(bs)\n  OK\n",
     "printf '' | " VM, 0, "true,false"},
    {33, "stringify roundtrip list",
     "#main\n  xs = json.parse_list('7,8')\n  s = json.stringify(xs)\n  io.outln(s)\n  OK\n",
     "printf '' | " VM, 0, "[7,8]"},
    {34, "in list one comma input", "#main\n  io.in_list() -> xs\n  io.outln(xs)\n  OK\n",
     "printf ',\\n' | " VM, 0, "0,0"},
    {35, "object via stringify parse",
     "#main\n  o = json.parse_object('{\"n\":42}')\n  s = json.stringify(o)\n  o2 = json.parse_object(s)\n  io.outln(o2)\n  OK\n",
     "printf '' | " VM, 0, "42"},
    {36, "parse list Mark mixed nums",
     "#main\n  xs = json.parse_list('Mark, Joe, -3')\n  io.outln(xs)\n  OK\n",
     "printf '' | " VM, 0, "-3"},
    {37, "in bools trailing comma", "#main\n  io.in_bools() -> bs\n  io.outln(bs)\n  OK\n",
     "printf 'true,\\n' | " VM, 0, "true,false"},
    {38, "json parse number", "#main\n  x = json.parse('99')\n  io.outln(x)\n  OK\n",
     "printf '' | " VM, 0, "99"},
    {39, "json parse bool true", "#main\n  x = json.parse('true')\n  io.outln(x)\n  OK\n",
     "printf '' | " VM, 0, "true"},
    {40, "foreach on parsed list",
     "#main\n  xs = json.parse_list('a,b')\n  foreach(xs, k, '#loop')\n  OK\n#loop\n  io.out(k)\n  return\n",
     "printf '' | " VM, 0, "ab"},
};

int main(void) {
    char root[512];
    char json_dir[560];
    size_t n = sizeof CASES / sizeof CASES[0];

    if (flat_find_repo_root(root, (int)sizeof root) != 0) {
        fprintf(stderr, "json_generate_tests: run from flatL repo\n");
        return 1;
    }
    snprintf(json_dir, sizeof json_dir, "%s/test/cases/json", root);

    for (size_t i = 0; i < n; i++) {
        if (emit_case(json_dir, &CASES[i]) != 0) {
            return 1;
        }
    }
    printf("Generated %zu json cases under %s\n", n, json_dir);
    return 0;
}
