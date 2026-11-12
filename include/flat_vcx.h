#ifndef FLAT_VCX_H
#define FLAT_VCX_H

#include "flat_value.h"
#include "flat_slot_type.h"

#include <stddef.h>
#include <stdint.h>

#define FLAT_VCX_MAX_CONTEXTS 64
#define FLAT_VCX_AUTO_DEFAULT_SEED 4u
/* createAuto: double while capacity < this, then add FLAT_VCX_AUTO_GROW_STEP per step */
#define FLAT_VCX_AUTO_GROW_DOUBLE_CAP 2048u
#define FLAT_VCX_AUTO_GROW_STEP 2048u

typedef struct FlatVcxVar {
    char name[FLAT_VAR_NAME_MAX + 1];
    FlatValue val;
    FlatSlotType slot_type;
    uint8_t has_slot_type;
    uint8_t slot_dynamic; /* bind via name:/expr */
    char slot_dynamic_expr[FLAT_VAR_NAME_MAX + 1];
    uint8_t slot_frozen; /* freeze(name) — no assign/path until free */
    uint8_t slot_const;  /* const name:type = — no reassign until free */
} FlatVcxVar;

#define FLAT_VCX_DSCHEMA_LIST_MAX 32

typedef struct FlatFileTable FlatFileTable;
typedef struct FlatMemLiveStats FlatMemLiveStats;
typedef struct FlatVcxRuntime FlatVcxRuntime;

typedef struct FlatVcxVmBind {
    FlatVcxVar **vars;
    int *var_count;
    uint32_t *var_slots_max;
} FlatVcxVmBind;

FlatVcxRuntime *flat_vcx_runtime_create(uint32_t global_capacity);
void flat_vcx_runtime_destroy(FlatVcxRuntime *rt, FlatCollectionPool *coll, FlatStringPool *pool,
                             FlatFileTable *files);

void flat_vcx_runtime_attach_vm(FlatVcxRuntime *rt, FlatVcxVmBind bind);
void flat_vcx_runtime_sync_bind_var_count(FlatVcxRuntime *rt);

void flat_vcx_runtime_set_active(FlatVcxRuntime *rt);
FlatVcxRuntime *flat_vcx_runtime_active(void);

uint32_t flat_vcx_current_id(const FlatVcxRuntime *rt);

int flat_vcx_is_active(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_active,
                       char *panic_detail, size_t panic_sz);

int flat_vcx_set(FlatVcxRuntime *rt, uint32_t ctx_id, char *panic_detail, size_t panic_sz);
int flat_vcx_create(FlatVcxRuntime *rt, uint32_t capacity, uint32_t *out_id, char *panic_detail,
                    size_t panic_sz);
int flat_vcx_create_auto(FlatVcxRuntime *rt, uint32_t initial_capacity, uint32_t *out_id,
                         char *panic_detail, size_t panic_sz);

/* Before binding a new name on the active context (VM assign path). */
int flat_vcx_prepare_active_new_bind(FlatVcxRuntime *rt, char *panic_detail, size_t panic_sz);

int flat_vcx_get_var(const FlatVcxRuntime *rt, uint32_t ctx_id, const char *name, FlatValue *out,
                     FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                     char *panic_detail, size_t panic_sz);
int flat_vcx_has_var(const FlatVcxRuntime *rt, uint32_t ctx_id, const char *name, int *out_ok,
                     char *panic_detail, size_t panic_sz);

int flat_vcx_vars_used(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n, char *panic_detail,
                       size_t panic_sz);
int flat_vcx_vars_max(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n, char *panic_detail,
                      size_t panic_sz);
int flat_vcx_vars_avail(const FlatVcxRuntime *rt, uint32_t ctx_id, int *out_n,
                          char *panic_detail, size_t panic_sz);

int flat_vcx_vars_copy(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                       FlatCollectionPool *coll, FlatStringPool *pool, FlatFileTable *files,
                       FlatMemLiveStats *live, char *panic_detail, size_t panic_sz);
int flat_vcx_vars_copy_like(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                            const char *pattern, FlatCollectionPool *coll, FlatStringPool *pool,
                            FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                            size_t panic_sz);

int flat_vcx_destroy_ctx(FlatVcxRuntime *rt, uint32_t ctx_id, FlatCollectionPool *coll,
                         FlatStringPool *pool, FlatFileTable *files, char *panic_detail,
                         size_t panic_sz);

int flat_vcx_vars_copy_by_names(FlatVcxRuntime *rt, uint32_t src_id, uint32_t dst_id,
                                const char *const *src_names, const char *const *dst_names, int n,
                                FlatCollectionPool *coll, FlatStringPool *pool,
                                FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                                size_t panic_sz);

int flat_vcx_deep_copy_value(const FlatValue *src, FlatValue *dst, FlatCollectionPool *coll,
                             FlatStringPool *pool, FlatFileTable *files);

/* Toolchain / VM: set a variable on a VCX context (AM-1 call path). */
int flat_vcx_set_named_var(FlatVcxRuntime *rt, uint32_t ctx_id, const char *name,
                            const FlatValue *val, FlatCollectionPool *coll, FlatStringPool *pool,
                            FlatFileTable *files, FlatMemLiveStats *live, char *panic_detail,
                            size_t panic_sz);

typedef uint32_t (*FlatVcxCallInstrResolver)(void);

void flat_vcx_set_call_instr_resolver(FlatVcxCallInstrResolver fn);
uint32_t flat_vcx_call_instr_ctx(void);

/* Strategy A nested calls: stack of instr VCX ids when __call_instr_vcx is assigned. */
void flat_vm_instr_stack_push(uint32_t id);
int flat_vm_instr_stack_pop(uint32_t id);
uint32_t flat_vm_instr_stack_peek(void);
void flat_vm_instr_stack_clear(void);

int flat_vcx_dschema_allow(FlatVcxRuntime *rt, uint32_t ctx_id, int allow, char *panic_detail,
                           size_t panic_sz);
int flat_vcx_dschema_list(FlatVcxRuntime *rt, uint32_t ctx_id, const FlatValue *list_val,
                          FlatCollectionPool *coll, FlatStringPool *pool, char *panic_detail,
                          size_t panic_sz);

/* Active ctx: schema name in type string (e.g. stock from "<stock>"). */
int flat_vcx_dynamic_schema_permit(FlatVcxRuntime *rt, const char *schema_name, char *panic,
                                   size_t panic_sz);

#endif
