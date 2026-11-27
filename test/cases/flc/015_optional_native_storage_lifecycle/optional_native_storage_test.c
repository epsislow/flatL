#include "fgir.h"
#include "flat_native_use.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "optional-native-storage-test: %s\n", msg); \
        return 1; \
    } \
} while (0)

/* Keep this focused harness independent of the full native registry. Match the
 * same core-module set used by flat_native_is_core_module(). */
int flat_native_is_core_module(const char *mod) {
    return mod && (strcmp(mod, "io") == 0 || strcmp(mod, "json") == 0 ||
                   strcmp(mod, "str") == 0 || strcmp(mod, "date") == 0 ||
                   strcmp(mod, "clock") == 0 || strcmp(mod, "mem") == 0 ||
                   strcmp(mod, "file") == 0);
}

static void set_decl(FgirNativeUnit *unit, int index, const char *module, const char *alias) {
    snprintf(unit->decls[index].module, sizeof unit->decls[index].module, "%s", module);
    snprintf(unit->decls[index].alias, sizeof unit->decls[index].alias, "%s", alias ? alias : "");
}

int main(void) {
    FgirProgram prog = {0};
    int i;
    int round;

    CHECK(prog.optional_natives == NULL && prog.optional_native_count == 0 &&
              prog.optional_native_capacity == 0,
          "zero-initialized optional-native state");
    CHECK(flat_fgir_reserve_optional_natives(&prog, 0) == 0, "zero reserve");
    CHECK(prog.optional_natives == NULL && prog.optional_native_capacity == 0,
          "zero reserve allocated memory");
    CHECK(flat_fgir_reserve_optional_natives(&prog, 1) == 0, "first reserve");
    CHECK(prog.optional_natives != NULL && prog.optional_native_capacity == 1,
          "first reserve must allocate one record");
    snprintf(prog.optional_natives[0].module, sizeof prog.optional_natives[0].module, "math");
    prog.optional_native_count = 1;
    CHECK(flat_fgir_reserve_optional_natives(&prog, 9) == 0, "geometric growth");
    CHECK(prog.optional_native_capacity >= 9, "capacity did not grow to nine");
    CHECK(strcmp(prog.optional_natives[0].module, "math") == 0,
          "existing record lost after realloc");
    CHECK(flat_fgir_reserve_optional_natives(&prog, FGIR_MAX_OPTIONAL_NATIVES) == 0,
          "reserve maximum");
    CHECK(prog.optional_native_capacity == FGIR_MAX_OPTIONAL_NATIVES,
          "maximum capacity mismatch");
    CHECK(flat_fgir_reserve_optional_natives(&prog, FGIR_MAX_OPTIONAL_NATIVES + 1u) != 0,
          "capacity above maximum accepted");
    CHECK(flat_fgir_reserve_optional_natives(&prog, (size_t)-1) != 0,
          "overflow-sized capacity accepted");
    CHECK(prog.optional_native_capacity == FGIR_MAX_OPTIONAL_NATIVES &&
              strcmp(prog.optional_natives[0].module, "math") == 0,
          "failed reserve damaged storage");
    flat_fgir_free(&prog);
    CHECK(prog.optional_natives == NULL && prog.optional_native_count == 0 &&
              prog.optional_native_capacity == 0,
          "free did not reset optional-native storage");

    /* Empty and core-only programs must not allocate the optional union. */
    CHECK(flat_native_use_build_fnat_union(&prog) == 0, "empty union build");
    CHECK(prog.optional_natives == NULL && prog.optional_native_count == 0 &&
              prog.optional_native_capacity == 0,
          "empty union allocated storage");
    CHECK(flat_fgir_reserve_native_units(&prog, 1) == 0, "allocate core-only unit");
    prog.native_unit_count = 1;
    prog.native_units[0].decl_count = 1;
    set_decl(&prog.native_units[0], 0, "mem", "M");
    CHECK(flat_native_use_build_fnat_union(&prog) == 0, "core-only union build");
    CHECK(prog.optional_natives == NULL && prog.optional_native_count == 0 &&
              prog.optional_native_capacity == 0,
          "core-only union allocated optional storage");
    flat_fgir_free(&prog);

    /* Core filtering, duplicate suppression, first-seen ordering and alias
     * clearing are properties of the FGIR union, not the source declarations. */
    CHECK(flat_fgir_reserve_native_units(&prog, 2) == 0, "allocate native units");
    prog.native_unit_count = 2;
    prog.native_units[0].decl_count = 4;
    set_decl(&prog.native_units[0], 0, "mem", "M");
    set_decl(&prog.native_units[0], 1, "math", "m1");
    set_decl(&prog.native_units[0], 2, "list", "l");
    set_decl(&prog.native_units[0], 3, "math", "m2");
    prog.native_units[1].decl_count = 2;
    set_decl(&prog.native_units[1], 0, "obj", "o");
    set_decl(&prog.native_units[1], 1, "list", "l2");
    CHECK(flat_native_use_build_fnat_union(&prog) == 0, "build union");
    CHECK(prog.optional_native_count == 3, "core filter or duplicate suppression failed");
    CHECK(strcmp(prog.optional_natives[0].module, "math") == 0 &&
              strcmp(prog.optional_natives[1].module, "list") == 0 &&
              strcmp(prog.optional_natives[2].module, "obj") == 0,
          "first-seen union order changed");
    for (i = 0; i < prog.optional_native_count; i++) {
        CHECK(prog.optional_natives[i].alias[0] == '\0', "union alias must remain empty");
    }
    CHECK(strcmp(prog.native_units[0].decls[1].alias, "m1") == 0 &&
              strcmp(prog.native_units[0].decls[3].alias, "m2") == 0,
          "source aliases were modified");

    /* Fill two units with 32 unique modules, then add a 33rd. The count is
     * committed only after a complete successful union build. */
    prog.native_units[0].decl_count = FGIR_MAX_OPTIONAL_NATIVES;
    for (i = 0; i < FGIR_MAX_OPTIONAL_NATIVES; i++) {
        char name[FGIR_NATIVE_MODULE_LEN];
        snprintf(name, sizeof name, "optional_%02d", i);
        set_decl(&prog.native_units[0], i, name, "");
    }
    prog.native_units[1].decl_count = 0;
    CHECK(flat_native_use_build_fnat_union(&prog) == 0, "exactly 32 modules rejected");
    CHECK(prog.optional_native_count == FGIR_MAX_OPTIONAL_NATIVES,
          "32-module union count mismatch");
    CHECK(prog.optional_native_capacity == FGIR_MAX_OPTIONAL_NATIVES,
          "32-module union capacity mismatch");
    prog.native_units[1].decl_count = 1;
    set_decl(&prog.native_units[1], 0, "optional_32", "");
    CHECK(flat_native_use_build_fnat_union(&prog) == -1, "33rd module was not rejected");
    CHECK(prog.optional_native_count == 0, "failed union committed a partial count");

    flat_fgir_free(&prog);
    CHECK(prog.optional_natives == NULL && prog.native_units == NULL,
          "free did not release both owned tables");
    for (round = 0; round < 128; round++) {
        CHECK(flat_fgir_reserve_optional_natives(&prog, 1u + (size_t)(round % FGIR_MAX_OPTIONAL_NATIVES)) == 0,
              "repeated allocate/grow");
        prog.optional_native_count = round % FGIR_MAX_OPTIONAL_NATIVES;
        flat_fgir_free(&prog);
        CHECK(prog.optional_natives == NULL && prog.optional_native_count == 0 &&
                  prog.optional_native_capacity == 0,
              "repeated free did not reset storage");
    }

    puts("optional-native-storage-test-ok");
    return 0;
}
