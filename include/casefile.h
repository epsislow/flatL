#ifndef FLAT_CASEFILE_H
#define FLAT_CASEFILE_H

#define FLAT_CASE_MAX_PATH 512
#define FLAT_CASE_MAX_CMD 1024
#define FLAT_CASE_MAX_CHECKS 16
#define FLAT_CASE_MAX_DESC 256
#define FLAT_CASE_SUITE_LEN 64

typedef enum {
    CASE_KIND_FILES,
    CASE_KIND_EXEC,
    CASE_KIND_PIPELINE
} CaseKind;

typedef struct {
    char suite[FLAT_CASE_SUITE_LEN];
    int id;
    char desc[FLAT_CASE_MAX_DESC];
    CaseKind kind;
    char case_dir[FLAT_CASE_MAX_PATH];

    char path_checks[FLAT_CASE_MAX_CHECKS][FLAT_CASE_MAX_PATH];
    int path_check_count;

    char cmd[FLAT_CASE_MAX_CMD];
    int expect_exit;
    char stdout_contains[FLAT_CASE_MAX_DESC];
    char stderr_contains[FLAT_CASE_MAX_DESC];

    char pipeline_compile[FLAT_CASE_MAX_CMD];
    char pipeline_run[FLAT_CASE_MAX_CMD];
    char pipeline_source[FLAT_CASE_MAX_PATH];
} FlatCase;

int flat_case_load(const char *case_toml_path, FlatCase *out);
int flat_case_run(const FlatCase *c, const char *repo_root, char *err, int err_size);

#endif
