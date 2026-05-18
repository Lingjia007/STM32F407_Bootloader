#ifndef PLATFORM_INTERNAL_FLASH_STM32_IMPL_H
#define PLATFORM_INTERNAL_FLASH_STM32_IMPL_H

#include "platform_internal_flash.h"
#include "platform_transport.h"
#include "stm32f4xx_hal.h"

#define INTERNAL_FLASH_SECTOR_0_ADDR 0x08000000
#define INTERNAL_FLASH_SECTOR_1_ADDR 0x08004000
#define INTERNAL_FLASH_SECTOR_2_ADDR 0x08008000
#define INTERNAL_FLASH_SECTOR_3_ADDR 0x0800C000
#define INTERNAL_FLASH_SECTOR_4_ADDR 0x08010000
#define INTERNAL_FLASH_SECTOR_5_ADDR 0x08020000
#define INTERNAL_FLASH_SECTOR_6_ADDR 0x08040000
#define INTERNAL_FLASH_SECTOR_7_ADDR 0x08060000
#define INTERNAL_FLASH_SECTOR_8_ADDR 0x08080000
#define INTERNAL_FLASH_SECTOR_9_ADDR 0x080A0000
#define INTERNAL_FLASH_SECTOR_10_ADDR 0x080C0000
#define INTERNAL_FLASH_SECTOR_11_ADDR 0x080E0000

#define INTERNAL_FLASH_END_ADDR 0x080FFFFF

#define INTERNAL_FLASH_SECTOR_SIZE_16K (16 * 1024)
#define INTERNAL_FLASH_SECTOR_SIZE_64K (64 * 1024)
#define INTERNAL_FLASH_SECTOR_SIZE_128K (128 * 1024)

typedef struct
{
    platform_internal_flash_base_t flash_base;
    platform_transport_base_t transport_base;
    uint32_t written_size;
    uint32_t relocate_offset;
    uint8_t pending_buf[4];
    uint8_t pending_len;
    uint8_t is_open;
    uint8_t is_erased;
} internal_flash_stm32_t;

void platform_internal_flash_stm32_register(internal_flash_stm32_t *flash,
                                            uint32_t start_addr,
                                            uint32_t end_addr,
                                            const char *name);

uint32_t internal_flash_get_sector_by_addr(uint32_t addr);
uint32_t internal_flash_get_sector_size_by_index(uint32_t sector);

#endif
