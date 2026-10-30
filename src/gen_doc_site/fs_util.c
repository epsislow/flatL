#include "flat_port.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static int mkdir_p(const char *path) {
    char buf[FLAT_PORT_PATH_MAX];
    size_t len;
    size_t i;

    if (!path || !path[0]) {
        return -1;
    }
    snprintf(buf, sizeof buf, "%s", path);
    len = strlen(buf);
    for (i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char save = buf[i];
            buf[i] = '\0';
            if (!flat_port_path_exists(buf)) {
                if (flat_port_mkdir(buf, 0755) != 0) {
                    return -1;
                }
            }
            buf[i] = save;
        }
    }
    if (!flat_port_path_exists(buf)) {
        return flat_port_mkdir(buf, 0755);
    }
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in;
    FILE *out;
    char block[4096];
    size_t n;

    in = fopen(src, "rb");
    if (!in) {
        return -1;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(block, 1, sizeof block, in)) > 0) {
        if (fwrite(block, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        return -1;
    }
    return 0;
}

static int remove_file_or_dir(const char *path);

#ifdef _WIN32
static int remove_dir_win(const char *dir) {
    char pattern[FLAT_PORT_PATH_MAX];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, sizeof pattern, "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return _rmdir(dir) == 0 ? 0 : -1;
    }
    do {
        char child[FLAT_PORT_PATH_MAX];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }
        snprintf(child, sizeof child, "%s\\%s", dir, fd.cFileName);
        if (remove_file_or_dir(child) != 0) {
            FindClose(h);
            return -1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return _rmdir(dir) == 0 ? 0 : -1;
}
#endif

static int remove_file_or_dir(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        return remove_dir_win(path);
    }
    return DeleteFileA(path) ? 0 : -1;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *ent;
        if (!d) {
            return -1;
        }
        while ((ent = readdir(d)) != NULL) {
            char child[FLAT_PORT_PATH_MAX];
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (remove_file_or_dir(child) != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
        return rmdir(path) == 0 ? 0 : -1;
    }
    return unlink(path) == 0 ? 0 : -1;
#endif
}

int gen_doc_remove_tree(const char *path, char *err, size_t err_size) {
    if (!path || !path[0]) {
        snprintf(err, err_size, "empty path");
        return -1;
    }
    if (!flat_port_path_exists(path)) {
        return 0;
    }
    if (remove_file_or_dir(path) != 0) {
        snprintf(err, err_size, "remove failed: %s", path);
        return -1;
    }
    return 0;
}

int gen_doc_copy_templates(const char *templates_dir, const char *out_dir, char *err,
                           size_t err_size) {
    static const char *const files[] = {
        "index.html",
        "css/docs.css",
        "js/doc-viewer.js",
    };
    size_t i;
    char src[FLAT_PORT_PATH_MAX];
    char dst[FLAT_PORT_PATH_MAX];

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(src, sizeof src, "%s/%s", templates_dir, files[i]);
        snprintf(dst, sizeof dst, "%s/%s", out_dir, files[i]);
        {
            char parent[FLAT_PORT_PATH_MAX];
            const char *slash = strrchr(dst, '/');
            const char *bs = strrchr(dst, '\\');
            const char *sep = slash;
            if (bs && (!slash || bs > slash)) {
                sep = bs;
            }
            if (sep) {
                size_t n = (size_t)(sep - dst);
                if (n >= sizeof parent) {
                    n = sizeof parent - 1;
                }
                memcpy(parent, dst, n);
                parent[n] = '\0';
                if (mkdir_p(parent) != 0) {
                    snprintf(err, err_size, "mkdir failed: %s", parent);
                    return -1;
                }
            }
        }
        if (copy_file(src, dst) != 0) {
            snprintf(err, err_size, "copy failed: %s -> %s", src, dst);
            return -1;
        }
    }
    return 0;
}
