#include "fgir.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "native-unit-storage-test: %s\n", msg); \
        return 1; \
    } \
} while (0)

int main(void) {
    FgirProgram prog = {0};
    int round;

    CHECK(prog.native_units == NULL && prog.native_unit_count == 0 &&
              prog.native_unit_capacity == 0,
          "zero-initialized native-unit state");
    CHECK(flat_fgir_reserve_native_units(&prog, 0) == 0, "zero reserve");
    CHECK(prog.native_units == NULL && prog.native_unit_capacity == 0,
          "zero reserve allocated memory");

    CHECK(flat_fgir_reserve_native_units(&prog, 1) == 0, "first reserve");
    CHECK(prog.native_units != NULL && prog.native_unit_capacity == 1,
          "first reserve must allocate exactly one large record");
    prog.native_units[0].decl_count = 1;
    snprintf(prog.native_units[0].decls[0].module,
             sizeof prog.native_units[0].decls[0].module, "math");
    snprintf(prog.native_units[0].decls[0].alias,
             sizeof prog.native_units[0].decls[0].alias, "m");

    CHECK(flat_fgir_reserve_native_units(&prog, 9) == 0, "geometric growth");
    CHECK(prog.native_unit_capacity >= 9, "capacity did not grow");
    CHECK(prog.native_units[0].decl_count == 1, "decl count lost after realloc");
    CHECK(strcmp(prog.native_units[0].decls[0].module, "math") == 0,
          "module name lost after realloc");
    CHECK(strcmp(prog.native_units[0].decls[0].alias, "m") == 0,
          "module alias lost after realloc");

    CHECK(flat_fgir_reserve_native_units(&prog, FGIR_MAX_NATIVE_UNITS) == 0,
          "reserve maximum");
    CHECK(prog.native_unit_capacity == FGIR_MAX_NATIVE_UNITS,
          "maximum capacity mismatch");
    CHECK(flat_fgir_reserve_native_units(&prog, FGIR_MAX_NATIVE_UNITS + 1u) != 0,
          "capacity above language maximum accepted");
    CHECK(flat_fgir_reserve_native_units(&prog, (size_t)-1) != 0,
          "overflow-sized capacity accepted");
    CHECK(prog.native_unit_capacity == FGIR_MAX_NATIVE_UNITS,
          "failed reserve changed capacity");
    CHECK(strcmp(prog.native_units[0].decls[0].module, "math") == 0,
          "failed reserve damaged data");

    flat_fgir_free(&prog);
    CHECK(prog.native_units == NULL && prog.native_unit_count == 0 &&
              prog.native_unit_capacity == 0,
          "free did not reset native-unit storage");
    CHECK(prog.main_index == -1 && prog.func_var_slots_manual == -1,
          "free did not restore FGIR defaults");

    for (round = 0; round < 128; round++) {
        CHECK(flat_fgir_reserve_native_units(&prog, 1u + (size_t)(round % FGIR_MAX_NATIVE_UNITS)) == 0,
              "repeated allocate/grow");
        prog.native_unit_count = round % FGIR_MAX_NATIVE_UNITS;
        flat_fgir_free(&prog);
        CHECK(prog.native_units == NULL && prog.native_unit_count == 0 &&
                  prog.native_unit_capacity == 0,
              "repeated free did not reset native-unit storage");
    }

    puts("native-unit-storage-test-ok");
    return 0;
}
