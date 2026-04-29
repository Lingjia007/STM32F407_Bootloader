#ifndef PLATFORM_INTERNAL_FLASH_STM32_IMPL_H
#define PLATFORM_INTERNAL_FLASH_STM32_IMPL_H

#include "platform_storage.h"
#include "stm32f4xx_hal.h"

#define INTERNAL_FLASH_APP_ADDRESS 0x08020000
#define INTERNAL_FLASH_END_ADDRESS 0x080FFFFF

typedef struct {
    platform_storage_base_t base;
    uint32_t start_addr;
    uint32_t total_size;
    uint32_t written_size;
    uint8_t pending_buf[4];
    uint8_t pending_len;
    uint8_t is_open;
    uint8_t is_erased;
} internal_flash_stm32_t;

extern internal_flash_stm32_t g_internal_flash;

#endif
