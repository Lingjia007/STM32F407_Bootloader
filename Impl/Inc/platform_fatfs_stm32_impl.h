#ifndef PLATFORM_FATFS_STM32_IMPL_H
#define PLATFORM_FATFS_STM32_IMPL_H

#include "platform_transport.h"
#include "ff.h"

#define FATFS_PATH_MAX 128

typedef struct
{
    platform_transport_base_t base;
    FATFS *fs;
    FIL file;
    char path[FATFS_PATH_MAX];
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
    uint8_t is_source;
} fatfs_stm32_t;

void platform_fatfs_stm32_register(fatfs_stm32_t *transport, const char *name);

#endif
