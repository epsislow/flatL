#include "fgir.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "function-storage-test: %s\n", msg); \
        return 1; \
    } \
} while (0)

int main(void) {
    FgirProgram prog = {0};
    int round;

    CHECK(prog.functions == NULL && prog.function_count == 0 && prog.function_capacity == 0,
          "zero-initialized function state");
    CHECK(flat_fgir_reserve_functions(&prog, 0) == 0, "zero reserve");
    CHECK(prog.functions == NULL && prog.function_capacity == 0, "zero reserve allocated memory");
    CHECK(flat_fgir_reserve_functions(&prog, 1) == 0, "initial reserve");
    CHECK(prog.functions != NULL && prog.function_capacity >= 1, "initial allocation missing");

    snprintf(prog.functions[0].name, sizeof prog.functions[0].name, "first");
    snprintf(prog.functions[0].impl_label, sizeof prog.functions[0].impl_label, "#first");
    prog.functions[0].param_count = 3;

    CHECK(flat_fgir_reserve_functions(&prog, 9) == 0, "growth past first capacity");
    CHECK(prog.function_capacity >= 9, "capacity did not grow");
    CHECK(strcmp(prog.functions[0].name, "first") == 0, "function name lost after realloc");
    CHECK(strcmp(prog.functions[0].impl_label, "#first") == 0, "implementation label lost after realloc");
    CHECK(prog.functions[0].param_count == 3, "function metadata lost after realloc");

    CHECK(flat_fgir_reserve_functions(&prog, FGIR_MAX_FUNCTIONS) == 0, "reserve maximum");
    CHECK(prog.function_capacity == FGIR_MAX_FUNCTIONS, "maximum capacity mismatch");
    CHECK(flat_fgir_reserve_functions(&prog, FGIR_MAX_FUNCTIONS + 1u) != 0,
          "capacity above language maximum accepted");
    CHECK(flat_fgir_reserve_functions(&prog, (size_t)-1) != 0,
          "overflow-sized capacity accepted");
    CHECK(prog.function_capacity == FGIR_MAX_FUNCTIONS, "failed reserve changed capacity");
    CHECK(strcmp(prog.functions[0].name, "first") == 0, "failed reserve damaged data");

    flat_fgir_free(&prog);
    CHECK(prog.functions == NULL && prog.function_count == 0 && prog.function_capacity == 0,
          "free did not reset function storage");
    CHECK(prog.main_index == -1 && prog.func_var_slots_manual == -1,
          "free did not restore FGIR defaults");

    for (round = 0; round < 128; round++) {
        CHECK(flat_fgir_reserve_functions(&prog, 1u + (size_t)(round % FGIR_MAX_FUNCTIONS)) == 0,
              "repeated allocate/grow");
        prog.function_count = round % FGIR_MAX_FUNCTIONS;
        flat_fgir_free(&prog);
        CHECK(prog.functions == NULL && prog.function_count == 0 && prog.function_capacity == 0,
              "repeated free did not reset function storage");
    }

    puts("function-storage-test-ok");
    return 0;
}
