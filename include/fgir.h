#ifndef FLAT_FGIR_H
#define FLAT_FGIR_H

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#define FGIR_MAX_LABEL 128
#define FGIR_MAX_FUNCTIONS 256
#define FGIR_MAX_FUNC_PARAMS 16
#define FGIR_MAX_FUNC_RETURNS 8
#define FGIR_MAX_FUNC_NAME 64
#define FGIR_MAX_LINE 512
#define FGIR_MAX_BLOCKS 1024
#define FGIR_MAX_INSTR 128
#define FGIR_MAX_PARAMS 8
#define FGIR_MAX_OPTIONAL_NATIVES 32
#define FGIR_MAX_NATIVE_UNITS 32
#define FGIR_NATIVE_MODULE_LEN 64
#define FGIR_NATIVE_ALIAS_LEN 64

#define FGIR_SCHEMA_MAX 64
#define FGIR_SCHEMA_NAME_MAX 63
#define FGIR_SCHEMA_FIELD_MAX 128
#define FGIR_SCHEMA_TYPE_SPEC_MAX 63
#define FGIR_OBJ_KEY_MAX 127

typedef struct {
    char name[FGIR_SCHEMA_NAME_MAX + 1];
    int field_count;
    char field_keys[FGIR_SCHEMA_FIELD_MAX][FGIR_OBJ_KEY_MAX + 1];
    char field_specs[FGIR_SCHEMA_FIELD_MAX][FGIR_SCHEMA_TYPE_SPEC_MAX + 1];
} FgirSchemaDecl;

typedef struct {
    char module[FGIR_NATIVE_MODULE_LEN];
    char alias[FGIR_NATIVE_ALIAS_LEN];
} FlatOptionalNativeDecl;

typedef enum {
    FGIR_OK,
    FGIR_FAIL,
    FGIR_RETURN,
    FGIR_NEXT,
    FGIR_ASSIGN,
    FGIR_GUARD,
    FGIR_CALL,
    FGIR_CALL_BLOCK,
    FGIR_FOREACH,
    FGIR_FOR,
    FGIR_RAISE_PANIC,
    FGIR_IO,
    FGIR_FREE,
    FGIR_FREEZE,
    FGIR_PANIC_RESET,
    FGIR_PANIC_REASON,
    FGIR_JUMP,
    FGIR_FUNC_CALL,
    FGIR_FUNC_CALL_VM
} FgirInstrKind;

typedef enum {
    FGIR_LINE_MOD_NONE = 0,
    FGIR_LINE_MOD_Q = 1,
    FGIR_LINE_MOD_TILDE = 2
} FgirLineMod;

typedef struct {
    FgirInstrKind kind;
    char text[FGIR_MAX_LINE];
    FgirLineMod line_mod;
    int index; /* 1-based within block */
    char call_target[FGIR_MAX_LABEL];
    char call_out[64];
    char foreach_item[64];
    char foreach_block[FGIR_MAX_LABEL];
    char for_limit[64];
    char line_label[64];
    char func_name[FGIR_MAX_FUNC_NAME];
    char func_args[FGIR_MAX_LINE];
    char func_ret[FGIR_MAX_LINE];
    int func_ret_count;
    char func_rets[FGIR_MAX_FUNC_RETURNS][64];
} FgirInstr;

typedef struct {
    FlatOptionalNativeDecl decls[FGIR_MAX_OPTIONAL_NATIVES];
    int decl_count;
} FgirNativeUnit;

typedef struct {
    char label[FGIR_MAX_LABEL];
    char params[FGIR_MAX_PARAMS][64];
    int param_count;
    FgirInstr instr[FGIR_MAX_INSTR];
    int instr_count;
    int native_unit_index;
} FgirBlock;

typedef struct {
    char name[FGIR_MAX_FUNC_NAME];
    char impl_label[FGIR_MAX_LABEL];
    char return_name[64];
    int return_count;
    char return_names[FGIR_MAX_FUNC_RETURNS][64];
    char params[FGIR_MAX_FUNC_PARAMS][64];
    int param_count;
    uint8_t param_has_type[FGIR_MAX_FUNC_PARAMS];
    char param_type_spec[FGIR_MAX_FUNC_PARAMS][48];
    uint8_t return_has_type;
    char return_type_spec[48];
    uint8_t return_has_type_arr[FGIR_MAX_FUNC_RETURNS];
    char return_type_spec_arr[FGIR_MAX_FUNC_RETURNS][48];
    char executor_label[FGIR_MAX_LABEL];
    uint32_t func_vcx_slots;
    int is_global_executor;
    int call_via_vm; /* 1 = OP_CALL_FUNCTION at call sites (func:callVM 1 zone) */
} FgirFunction;

typedef struct {
    FgirBlock blocks[FGIR_MAX_BLOCKS];
    int block_count;
    int main_index;
    FgirNativeUnit native_units[FGIR_MAX_NATIVE_UNITS];
    int native_unit_count;
    FlatOptionalNativeDecl optional_natives[FGIR_MAX_OPTIONAL_NATIVES];
    int optional_native_count;
    FgirFunction functions[FGIR_MAX_FUNCTIONS];
    int function_count;
    char default_executor[FGIR_MAX_LABEL];
    int func_var_slots_manual; /* -1 = auto */
    int func_call_vm; /* header func:callVM for following defs */
    int mod_id_set;
    char mod_id[64];
    int export_count;
    char exports[FGIR_MAX_FUNCTIONS][FGIR_MAX_FUNC_NAME];
    FgirSchemaDecl schemas[FGIR_SCHEMA_MAX];
    int schema_count;
} FgirProgram;

int flat_parse_free_varname(const char *line, char *var_out, size_t var_sz);
int flat_parse_freeze_varname(const char *line, char *var_out, size_t var_sz);
int flat_parse_panic_reset_line(const char *line);
int flat_parse_panic_reason_bind(const char *line, char *var_out, size_t var_sz);
int flat_parse_unit(const char *path, int allow_main, FgirProgram *out, char *err, size_t err_size);
int flat_parse_file(const char *path, FgirProgram *out, char *err, size_t err_size);
void flat_fgir_dump(const FgirProgram *prog, FILE *out);
int flat_compile_fgir(const FgirProgram *prog, const char *out_path, char *err, size_t err_size);

#endif
