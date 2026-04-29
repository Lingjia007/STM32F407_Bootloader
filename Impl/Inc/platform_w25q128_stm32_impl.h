#ifndef PLATFORM_W25Q128_STM32_IMPL_H
#define PLATFORM_W25Q128_STM32_IMPL_H

#include "platform_spi_flash.h"
#include "stm32f4xx_hal.h"

#define W25Q128_TOTAL_SIZE (16 * 1024 * 1024)
#define W25Q128_SECTOR_SIZE 4096
#define W25Q128_BLOCK_SIZE 65536
#define W25Q128_PAGE_SIZE 256
#define W25Q128_SECTOR_COUNT (W25Q128_TOTAL_SIZE / W25Q128_SECTOR_SIZE)
#define W25Q128_BLOCK_COUNT (W25Q128_TOTAL_SIZE / W25Q128_BLOCK_SIZE)

typedef struct {
    platform_spi_flash_base_t base;
    SPI_HandleTypeDef* hspi;
    GPIO_TypeDef* cs_port;
    uint16_t cs_pin;
    uint16_t device_id;
} w25q128_stm32_t;

extern w25q128_stm32_t g_w25q128_flash;

#endif
