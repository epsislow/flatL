#include "flat_file.h"

#include <stdlib.h>
#include <string.h>

void flat_file_table_init(FlatFileTable *tbl) {
    if (tbl) {
        memset(tbl, 0, sizeof *tbl);
    }
}

void flat_file_table_close_all(FlatFileTable *tbl) {
    int i;
    if (!tbl) {
        return;
    }
    for (i = 0; i < FLAT_FILE_MAX_HANDLES; i++) {
        FlatFileSlot *s = &tbl->slots[i];
        if (s->in_use && s->fp && !s->closed) {
            fclose(s->fp);
        }
        memset(s, 0, sizeof *s);
    }
}

static int file_slot_take(FlatFileTable *tbl, uint32_t *out_id) {
    int i;
    for (i = 0; i < FLAT_FILE_MAX_HANDLES; i++) {
        if (!tbl->slots[i].in_use) {
            tbl->slots[i].in_use = 1;
            tbl->slots[i].closed = 0;
            tbl->slots[i].refcnt = 0;
            *out_id = (uint32_t)i;
            return 0;
        }
    }
    return -1;
}

int flat_file_is_live(const FlatFileTable *tbl, uint32_t id) {
    if (!tbl || id >= FLAT_FILE_MAX_HANDLES) {
        return 0;
    }
    return tbl->slots[id].in_use && !tbl->slots[id].closed;
}

FILE *flat_file_fp(FlatFileTable *tbl, uint32_t id) {
    if (!flat_file_is_live(tbl, id)) {
        return NULL;
    }
    return tbl->slots[id].fp;
}

void flat_file_retain(FlatFileTable *tbl, uint32_t id) {
    if (flat_file_is_live(tbl, id)) {
        tbl->slots[id].refcnt++;
    }
}

void flat_file_release(FlatFileTable *tbl, uint32_t id) {
    FlatFileSlot *s;
    if (!tbl || id >= FLAT_FILE_MAX_HANDLES || !tbl->slots[id].in_use) {
        return;
    }
    s = &tbl->slots[id];
    if (s->refcnt > 0) {
        s->refcnt--;
    }
    if (s->refcnt == 0 && s->closed) {
        memset(s, 0, sizeof *s);
    }
}

int flat_file_open(FlatFileTable *tbl, const char *path, FlatFileMode mode, uint32_t *out_id) {
    FILE *fp = NULL;
    uint32_t id;
    const char *mode_str;

    if (!tbl || !path || !out_id) {
        return -1;
    }
    switch (mode) {
    case FLAT_FILE_MODE_READ:
        mode_str = "rb";
        fp = fopen(path, mode_str);
        break;
    case FLAT_FILE_MODE_CREATE:
        mode_str = "w+b";
        fp = fopen(path, mode_str);
        break;
    case FLAT_FILE_MODE_WRITE_AT0:
        fp = fopen(path, "r+b");
        if (!fp) {
            return -1;
        }
        if (fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            return -1;
        }
        break;
    case FLAT_FILE_MODE_APPEND:
        fp = fopen(path, "a+b");
        if (!fp) {
            return -1;
        }
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            return -1;
        }
        break;
    case FLAT_FILE_MODE_RDWR:
        fp = fopen(path, "r+b");
        break;
    default:
        return -1;
    }
    if (!fp) {
        return -1;
    }
    if (file_slot_take(tbl, &id) != 0) {
        fclose(fp);
        return -1;
    }
    tbl->slots[id].fp = fp;
    tbl->slots[id].mode = mode;
    *out_id = id;
    return 0;
}

int flat_file_close(FlatFileTable *tbl, uint32_t id) {
    FlatFileSlot *s;
    if (!flat_file_is_live(tbl, id)) {
        return -1;
    }
    s = &tbl->slots[id];
    if (s->fp) {
        fclose(s->fp);
        s->fp = NULL;
    }
    s->closed = 1;
    if (s->refcnt == 0) {
        memset(s, 0, sizeof *s);
    }
    return 0;
}

int flat_value_set_file(FlatValue *v, uint32_t file_id) {
    if (!v) {
        return -1;
    }
    v->tag = VAL_FILE;
    v->u.file_id = file_id;
    return 0;
}
