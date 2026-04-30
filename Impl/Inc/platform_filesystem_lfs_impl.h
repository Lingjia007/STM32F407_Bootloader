#ifndef PLATFORM_FILESYSTEM_LFS_IMPL_H
#define PLATFORM_FILESYSTEM_LFS_IMPL_H

#include "platform_filesystem.h"
#include "lfs.h"

typedef struct {
    platform_fs_base_t base;
    lfs_t* lfs;
} fs_lfs_t;

typedef struct {
    lfs_file_t file;
} fs_lfs_file_t;

typedef struct {
    lfs_dir_t dir;
} fs_lfs_dir_t;

extern fs_lfs_t g_fs_lfs;

void platform_fs_lfs_register(fs_lfs_t* fs, lfs_t* lfs, const char* name);

#endif
