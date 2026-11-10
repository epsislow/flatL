#ifndef FLAT_LINK_H
#define FLAT_LINK_H

#include "fgir.h"

typedef struct {
    int verbose;
} FlatLinkOptions;

typedef struct {
    int units;
    int fmod;
} FlatLinkReport;

/* Parse entry + transitive use graph, merge FGIR into out. */
int flat_link_program(const char *entry_path, const FlatLinkOptions *opts,
                      FgirProgram *out, FlatLinkReport *report, char *err, size_t err_size);

#endif
