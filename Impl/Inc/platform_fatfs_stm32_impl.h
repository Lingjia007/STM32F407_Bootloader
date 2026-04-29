#ifndef PLATFORM_FATFS_STM32_IMPL_H
#define PLATFORM_FATFS_STM32_IMPL_H

#include "platform_storage.h"
#include "ff.h"

#define FATFS_PATH_MAX 128

typedef struct {
    platform_storage_base_t base;
    FATFS* fs;
    FIL file;
    char path[FATFS_PATH_MAX];
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
    uint8_t is_source;
} fatfs_stm32_t;

extern fatfs_stm32_t g_fatfs_storage;

#endif
