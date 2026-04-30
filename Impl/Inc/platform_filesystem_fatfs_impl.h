#ifndef PLATFORM_FILESYSTEM_FATFS_IMPL_H
#define PLATFORM_FILESYSTEM_FATFS_IMPL_H

#include "platform_filesystem.h"
#include "fatfs.h"

typedef struct {
    platform_fs_base_t base;
    FATFS* fs;
} fs_fatfs_t;

typedef struct {
    FIL file;
} fs_fatfs_file_t;

typedef struct {
    DIR dir;
} fs_fatfs_dir_t;

void platform_fs_fatfs_register(fs_fatfs_t* fs, FATFS* fatfs, const char* name);

#endif
