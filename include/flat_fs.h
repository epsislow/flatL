#ifndef FLAT_FS_H
#define FLAT_FS_H

#include <stddef.h>

#define FLAT_FS_NAME_MAX 512u
#define FLAT_FS_LIST_MAX 512u

typedef enum {
    FLAT_FS_KIND_MISSING = 0,
    FLAT_FS_KIND_FILE,
    FLAT_FS_KIND_DIR,
    FLAT_FS_KIND_OTHER
} FlatFsKind;

FlatFsKind flat_fs_kind(const char *path);

int flat_fs_exists(const char *path); /* 1/0 */
int flat_fs_is_file(const char *path);
int flat_fs_is_dir(const char *path);

/* Create directory (one level). 0 ok; -1 fail (*why set). Idempotent if already a dir. */
int flat_fs_mkdir(const char *path, const char **why);

/* Remove file or empty directory. 0 ok; -1 fail. */
int flat_fs_remove(const char *path, const char **why);

/*
 * List directory entries (no "." / ".."). Names written into names[i] (caller provides
 * names[max][FLAT_FS_NAME_MAX]). Sorted lexicographically. *out_n = count.
 */
int flat_fs_list_dir(const char *path, char names[][FLAT_FS_NAME_MAX], int max, int *out_n,
                     const char **why);

#endif
