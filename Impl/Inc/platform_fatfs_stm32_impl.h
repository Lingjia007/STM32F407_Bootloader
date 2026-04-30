#ifndef PLATFORM_FATFS_STM32_IMPL_H
#define PLATFORM_FATFS_STM32_IMPL_H

#include "platform_transport.h"
#include "ff.h"

#define FATFS_PATH_MAX 128
#define FATFS_DMA_BUF_SIZE 512

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
    __attribute__((aligned(4))) uint8_t dma_buf[FATFS_DMA_BUF_SIZE];
} fatfs_stm32_t;

void platform_fatfs_stm32_register(fatfs_stm32_t *transport, const char *name);

#endif
