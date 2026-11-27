#include "fgir.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "export-storage-test: %s\n", msg); \
        return 1; \
    } \
} while (0)

int main(void) {
    FgirProgram prog = {0};
    int round;

    CHECK(prog.exports == NULL && prog.export_count == 0 && prog.export_capacity == 0,
          "zero-initialized export state");
    CHECK(flat_fgir_reserve_exports(&prog, 0) == 0, "zero reserve");
    CHECK(prog.exports == NULL && prog.export_capacity == 0,
          "zero reserve allocated memory");

    CHECK(flat_fgir_reserve_exports(&prog, 1) == 0, "first reserve");
    CHECK(prog.exports != NULL && prog.export_capacity == 1,
          "first reserve must allocate exactly one export record");
    snprintf(prog.exports[0], FGIR_MAX_FUNC_NAME, "first_export");
    prog.export_count = 1;

    CHECK(flat_fgir_reserve_exports(&prog, 2) == 0, "second reserve");
    CHECK(prog.export_capacity == 2, "capacity did not grow to two");
    CHECK(flat_fgir_reserve_exports(&prog, 9) == 0, "geometric growth");
    CHECK(prog.export_capacity >= 9, "capacity did not grow to nine");
    CHECK(strcmp(prog.exports[0], "first_export") == 0,
          "existing export lost after realloc");

    CHECK(flat_fgir_reserve_exports(&prog, FGIR_MAX_FUNCTIONS) == 0,
          "reserve maximum");
    CHECK(prog.export_capacity == FGIR_MAX_FUNCTIONS,
          "maximum capacity mismatch");
    CHECK(flat_fgir_reserve_exports(&prog, FGIR_MAX_FUNCTIONS + 1u) != 0,
          "capacity above language maximum accepted");
    CHECK(flat_fgir_reserve_exports(&prog, (size_t)-1) != 0,
          "overflow-sized capacity accepted");
    CHECK(prog.export_capacity == FGIR_MAX_FUNCTIONS,
          "failed reserve changed capacity");
    CHECK(strcmp(prog.exports[0], "first_export") == 0,
          "failed reserve damaged data");

    flat_fgir_free(&prog);
    CHECK(prog.exports == NULL && prog.export_count == 0 && prog.export_capacity == 0,
          "free did not reset export storage");
    CHECK(prog.main_index == -1 && prog.func_var_slots_manual == -1,
          "free did not restore FGIR defaults");

    for (round = 0; round < 128; round++) {
        CHECK(flat_fgir_reserve_exports(&prog, 1u + (size_t)(round % FGIR_MAX_FUNCTIONS)) == 0,
              "repeated allocate/grow");
        prog.export_count = round % FGIR_MAX_FUNCTIONS;
        flat_fgir_free(&prog);
        CHECK(prog.exports == NULL && prog.export_count == 0 && prog.export_capacity == 0,
              "repeated free did not reset export storage");
    }

    puts("export-storage-test-ok");
    return 0;
}
