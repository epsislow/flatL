#include "flat_fs.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static void set_why(const char **why, const char *w) {
    if (why) *why = w;
}

FlatFsKind flat_fs_kind(const char *path) {
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
    struct _stat st;
    if (!path || !path[0]) return FLAT_FS_KIND_MISSING;
    if (_stat(path, &st) != 0) return FLAT_FS_KIND_MISSING;
    if ((st.st_mode & _S_IFDIR) != 0) return FLAT_FS_KIND_DIR;
    if ((st.st_mode & _S_IFREG) != 0) return FLAT_FS_KIND_FILE;
    return FLAT_FS_KIND_OTHER;
#else
    struct stat st;
    if (!path || !path[0]) return FLAT_FS_KIND_MISSING;
    if (stat(path, &st) != 0) return FLAT_FS_KIND_MISSING;
    if (S_ISDIR(st.st_mode)) return FLAT_FS_KIND_DIR;
    if (S_ISREG(st.st_mode)) return FLAT_FS_KIND_FILE;
    return FLAT_FS_KIND_OTHER;
#endif
}

int flat_fs_exists(const char *path) {
    return flat_fs_kind(path) != FLAT_FS_KIND_MISSING;
}

int flat_fs_is_file(const char *path) {
    return flat_fs_kind(path) == FLAT_FS_KIND_FILE;
}

int flat_fs_is_dir(const char *path) {
    return flat_fs_kind(path) == FLAT_FS_KIND_DIR;
}

int flat_fs_mkdir(const char *path, const char **why) {
    FlatFsKind k;
    set_why(why, NULL);
    if (!path || !path[0]) {
        set_why(why, "fs.mkdir.empty");
        return -1;
    }
    k = flat_fs_kind(path);
    if (k == FLAT_FS_KIND_DIR) return 0;
    if (k == FLAT_FS_KIND_FILE || k == FLAT_FS_KIND_OTHER) {
        set_why(why, "fs.mkdir.exists");
        return -1;
    }
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
    if (_mkdir(path) != 0) {
#else
    if (mkdir(path, 0755) != 0) {
#endif
        if (errno == EEXIST && flat_fs_is_dir(path)) return 0;
        set_why(why, "fs.mkdir");
        return -1;
    }
    return 0;
}

int flat_fs_remove(const char *path, const char **why) {
    FlatFsKind k;
    set_why(why, NULL);
    if (!path || !path[0]) {
        set_why(why, "fs.remove.empty");
        return -1;
    }
    k = flat_fs_kind(path);
    if (k == FLAT_FS_KIND_MISSING) {
        set_why(why, "fs.remove.missing");
        return -1;
    }
    if (k == FLAT_FS_KIND_DIR) {
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
        if (_rmdir(path) != 0) {
#else
        if (rmdir(path) != 0) {
#endif
#ifdef ENOTEMPTY
            if (errno == ENOTEMPTY) {
                set_why(why, "fs.remove.not_empty");
                return -1;
            }
#endif
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
            if (errno == EEXIST || errno == ENOTEMPTY) {
                set_why(why, "fs.remove.not_empty");
                return -1;
            }
#endif
            set_why(why, "fs.remove");
            return -1;
        }
        return 0;
    }
    if (k == FLAT_FS_KIND_FILE) {
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
        if (_unlink(path) != 0) {
#else
        if (unlink(path) != 0) {
#endif
            set_why(why, "fs.remove");
            return -1;
        }
        return 0;
    }
    set_why(why, "fs.remove");
    return -1;
}

static void sort_names(char names[][FLAT_FS_NAME_MAX], int n) {
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (strcmp(names[j], names[i]) < 0) {
                char tmp[FLAT_FS_NAME_MAX];
                memcpy(tmp, names[i], FLAT_FS_NAME_MAX);
                memcpy(names[i], names[j], FLAT_FS_NAME_MAX);
                memcpy(names[j], tmp, FLAT_FS_NAME_MAX);
            }
        }
    }
}

int flat_fs_list_dir(const char *path, char names[][FLAT_FS_NAME_MAX], int max, int *out_n,
                     const char **why) {
    int n = 0;
    set_why(why, NULL);
    if (!path || !path[0] || !names || !out_n || max <= 0) {
        set_why(why, "fs.listDir.args");
        return -1;
    }
    if (!flat_fs_is_dir(path)) {
        if (flat_fs_kind(path) == FLAT_FS_KIND_MISSING) {
            set_why(why, "fs.listDir.missing");
        } else {
            set_why(why, "fs.listDir.not_dir");
        }
        return -1;
    }
#if defined(_WIN32) && !defined(FLAT_WASM_BUILD)
    {
        char pattern[FLAT_FS_NAME_MAX + 8];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (snprintf(pattern, sizeof pattern, "%s\\*", path) >= (int)sizeof pattern) {
            set_why(why, "fs.listDir");
            return -1;
        }
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) {
            set_why(why, "fs.listDir");
            return -1;
        }
        do {
            const char *name = fd.cFileName;
            if (name[0] == '.' &&
                (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
                continue;
            }
            if (n >= max) {
                FindClose(h);
                set_why(why, "fs.listDir.limit");
                return -1;
            }
            snprintf(names[n], FLAT_FS_NAME_MAX, "%s", name);
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(path);
        struct dirent *de;
        if (!d) {
            set_why(why, "fs.listDir");
            return -1;
        }
        while ((de = readdir(d)) != NULL) {
            const char *name = de->d_name;
            if (name[0] == '.' &&
                (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
                continue;
            }
            if (n >= max) {
                closedir(d);
                set_why(why, "fs.listDir.limit");
                return -1;
            }
            snprintf(names[n], FLAT_FS_NAME_MAX, "%s", name);
            n++;
        }
        closedir(d);
    }
#endif
    sort_names(names, n);
    *out_n = n;
    return 0;
}
