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
int flat_port_shell_capture(const char *cmd, int *exit_code, char *out_buf, int out_buf_size);
int flat_port_scan_dir(const char *dir_path, FlatPortDirFn fn, void *ctx);

#endif
