#ifndef PLATFORM_LFS_STM32_IMPL_H
#define PLATFORM_LFS_STM32_IMPL_H

#include "platform_storage.h"
#include "lfs.h"

#define LFS_PATH_MAX 128

typedef struct {
    platform_storage_base_t base;
    lfs_t* lfs;
    lfs_file_t file;
    char path[LFS_PATH_MAX];
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
    uint8_t is_source;
} lfs_stm32_t;

extern lfs_stm32_t g_lfs_storage;

#endif
