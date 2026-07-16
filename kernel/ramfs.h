#ifndef RAMFS_H
#define RAMFS_H

#include "types.h"

enum ramfs_file_type {
    RAMFS_FILE_TEXT,
    RAMFS_FILE_MODULE
};

struct ramfs_file {
    const char *path;
    const uint8_t *data;
    uint64_t size;
    enum ramfs_file_type type;
};

/* Read-only filesystem interface. A future FAT12 backend can preserve this API. */
int ramfs_init(void);
uint32_t ramfs_file_count(void);
const struct ramfs_file *ramfs_file_at(uint32_t index);
const struct ramfs_file *ramfs_lookup(const char *path);
uint64_t ramfs_read(const struct ramfs_file *file, uint64_t offset,
                    void *buffer, uint64_t buffer_size);

#endif
