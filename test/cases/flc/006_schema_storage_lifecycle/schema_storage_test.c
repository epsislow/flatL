#include "fgir.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "schema-storage-test: %s\n", msg);                                  \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    FgirProgram prog = {0};
    int round;

    CHECK(prog.schemas == NULL && prog.schema_count == 0 && prog.schema_capacity == 0,
          "zero-initialized schema state");
    CHECK(flat_fgir_reserve_schemas(&prog, 0) == 0, "zero reserve");
    CHECK(prog.schemas == NULL && prog.schema_capacity == 0, "zero reserve allocated memory");
    CHECK(flat_fgir_reserve_schemas(&prog, 1) == 0, "initial reserve");
    CHECK(prog.schemas != NULL && prog.schema_capacity >= 1, "initial allocation missing");

    snprintf(prog.schemas[0].name, sizeof prog.schemas[0].name, "first");
    prog.schemas[0].field_count = 1;
    snprintf(prog.schemas[0].field_keys[0], sizeof prog.schemas[0].field_keys[0], "id");
    snprintf(prog.schemas[0].field_specs[0], sizeof prog.schemas[0].field_specs[0], "u32");

    CHECK(flat_fgir_reserve_schemas(&prog, 9) == 0, "growth past first capacity");
    CHECK(prog.schema_capacity >= 9, "capacity did not grow");
    CHECK(strcmp(prog.schemas[0].name, "first") == 0, "schema name lost after realloc");
    CHECK(strcmp(prog.schemas[0].field_keys[0], "id") == 0, "schema field lost after realloc");
    CHECK(strcmp(prog.schemas[0].field_specs[0], "u32") == 0, "schema type lost after realloc");

    CHECK(flat_fgir_reserve_schemas(&prog, FGIR_SCHEMA_MAX) == 0, "reserve maximum");
    CHECK(prog.schema_capacity == FGIR_SCHEMA_MAX, "maximum capacity mismatch");
    CHECK(flat_fgir_reserve_schemas(&prog, FGIR_SCHEMA_MAX + 1u) != 0,
          "capacity above language maximum accepted");
    CHECK(flat_fgir_reserve_schemas(&prog, (size_t)-1) != 0,
          "overflow-sized capacity accepted");
    CHECK(prog.schema_capacity == FGIR_SCHEMA_MAX, "failed reserve changed capacity");
    CHECK(strcmp(prog.schemas[0].name, "first") == 0, "failed reserve damaged data");

    flat_fgir_free(&prog);
    CHECK(prog.schemas == NULL && prog.schema_count == 0 && prog.schema_capacity == 0,
          "free did not reset schema storage");
    CHECK(prog.main_index == -1 && prog.func_var_slots_manual == -1,
          "free did not restore FGIR defaults");

    for (round = 0; round < 128; round++) {
        CHECK(flat_fgir_reserve_schemas(&prog, 1u + (size_t)(round % FGIR_SCHEMA_MAX)) == 0,
              "repeated allocate/grow");
        prog.schema_count = round % FGIR_SCHEMA_MAX;
        flat_fgir_free(&prog);
        CHECK(prog.schemas == NULL && prog.schema_count == 0 && prog.schema_capacity == 0,
              "repeated free did not reset schema storage");
    }

    puts("schema-storage-test-ok");
    return 0;
}
