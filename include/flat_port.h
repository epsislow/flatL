#ifndef FLAT_PORT_H
#define FLAT_PORT_H

#include <stddef.h>

#define FLAT_PORT_PATH_MAX 512

typedef int (*FlatPortDirFn)(const char *entry_name, void *ctx);

int flat_port_path_exists(const char *path);
int flat_port_dir_writable(const char *dir);
int flat_port_getcwd(char *buf, size_t buf_size);
int flat_port_path_is_root(const char *path);
int flat_port_mkdir(const char *path, int mode);
/* Writable parent for temp dirs (does not create unique subdir). */
int flat_port_temp_parent(char *parent, size_t parent_size, const char *repo_root);
int flat_port_make_temp_dir(char *out, size_t out_size, const char *repo_root);
/* Unique .fb path under temp parent (creates no file; for flatl crun). */
int flat_port_make_temp_fb_path(char *out, size_t out_size, const char *repo_root);
int flat_port_shell_capture(const char *cmd, int *exit_code, char *out_buf, int out_buf_size);
int flat_port_scan_dir(const char *dir_path, FlatPortDirFn fn, void *ctx);

/* In-place: native path separators; on Windows also flc.exe / flvm.exe in bin/ paths. */
void flat_port_path_to_native(char *path);
void flat_port_prepare_shell_cmd(char *cmd, size_t cmd_cap);
/* MinGW/MSVC: set stdio text mode so piped stdin has LF line ends (matches POSIX tests). */
void flat_port_stdio_setup(void);
/* Resolve node/node.exe from PATH only (process PATH, plus Windows Path env on Win32).
 * Writes a slash-normalized path suitable for sh (quote it if it contains spaces).
 * Returns 0 on success, -1 if not found. */
int flat_port_resolve_node(char *out, size_t out_size);

#endif
