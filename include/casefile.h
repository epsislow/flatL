#ifndef FLAT_CASEFILE_H
#define FLAT_CASEFILE_H

#include <stddef.h>

#define FLAT_CASE_MAX_PATH 512
#define FLAT_CASE_MAX_CMD 1024
#define FLAT_CASE_MAX_CHECKS 16
#define FLAT_CASE_MAX_STDOUT_EXTRA 7
#define FLAT_CASE_MAX_DESC 256
#define FLAT_CASE_SUITE_LEN 64
#define FLAT_CASE_CATEG_LEN 32
#define FLAT_CASE_CATEG_DEFAULT "default"

typedef enum {
    CASE_KIND_FILES,
    CASE_KIND_EXEC,
    CASE_KIND_PIPELINE
} CaseKind;

typedef struct {
    char suite[FLAT_CASE_SUITE_LEN];
    int id;
    char desc[FLAT_CASE_MAX_DESC];
    char categ[FLAT_CASE_CATEG_LEN];
    CaseKind kind;
    char case_dir[FLAT_CASE_MAX_PATH];

    char path_checks[FLAT_CASE_MAX_CHECKS][FLAT_CASE_MAX_PATH];
    int path_check_count;

    char cmd[FLAT_CASE_MAX_CMD];
    int expect_exit;
    char stdout_contains[FLAT_CASE_MAX_DESC];
    char stdout_contains_extra[FLAT_CASE_MAX_STDOUT_EXTRA][FLAT_CASE_MAX_DESC];
    int stdout_contains_extra_count;
    char stdout_must_not_contain[FLAT_CASE_MAX_DESC];
    char stderr_contains[FLAT_CASE_MAX_DESC];

    char pipeline_compile[FLAT_CASE_MAX_CMD];
    char pipeline_run[FLAT_CASE_MAX_CMD];
    char pipeline_source[FLAT_CASE_MAX_PATH];

    int flatl_section_seen;
    char flatl_crun[FLAT_CASE_MAX_CMD];
    char flatl_compile[FLAT_CASE_MAX_CMD];
    char flatl_run[FLAT_CASE_MAX_CMD];

    /* Assert-uri pentru --flatl când există [flatl]: fără moștenire de la root. */
    int flatl_expect_exit;
    int flatl_expect_exit_set;
    char flatl_stdout_contains[FLAT_CASE_MAX_DESC];
    char flatl_stdout_contains_extra[FLAT_CASE_MAX_STDOUT_EXTRA][FLAT_CASE_MAX_DESC];
    int flatl_stdout_contains_extra_count;
    int flatl_stdout_contains_set;
    char flatl_stdout_must_not_contain[FLAT_CASE_MAX_DESC];
    int flatl_stdout_must_not_contain_set;
    char flatl_stderr_contains[FLAT_CASE_MAX_DESC];
    int flatl_stderr_contains_set;
} FlatCase;

typedef struct {
    int valid;
    double prep_ms;
    double total_ms;
    double flc_ms;
    double flvm_ms;
    double compile_ms;
    double run_ms;
    double exec_ms;
    int have_flc;
    int have_flvm;
    int have_compile;
    int have_run;
    int have_exec;
    int have_units;
    int units;
    int have_fmod;
    int fmod;
} FlatCaseTiming;

typedef struct {
    int inject_tool_time;
    int measure;
} FlatCaseRunOptions;

int flat_case_load(const char *case_toml_path, FlatCase *out);
int flat_case_run(const FlatCase *c, const char *repo_root, int use_flatl, char *err, int err_size);
int flat_case_run_ex(const FlatCase *c, const char *repo_root, int use_flatl,
                     const FlatCaseRunOptions *opts, FlatCaseTiming *timing, char *err, int err_size);
void flat_case_format_time_suffix(const FlatCase *c, int use_flatl, const FlatCaseTiming *t,
                                  char *buf, size_t buf_sz);

#endif
