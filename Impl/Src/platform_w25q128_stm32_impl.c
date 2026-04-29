#include "platform_w25q128_stm32_impl.h"

#define W25X_WriteEnable 0x06
#define W25X_WriteDisable 0x04
#define W25X_ReadStatusReg 0x05
#define W25X_WriteStatusReg 0x01
#define W25X_ReadData 0x03
#define W25X_FastReadData 0x0B
#define W25X_FastReadDual 0x3B
#define W25X_PageProgram 0x02
#define W25X_BlockErase 0xD8
#define W25X_SectorErase 0x20
#define W25X_ChipErase 0xC7
#define W25X_PowerDown 0xB9
#define W25X_ReleasePowerDown 0xAB
#define W25X_DeviceID 0xAB
#define W25X_ManufactDeviceID 0x90
#define W25X_JedecDeviceID 0x9F

static uint8_t spi_read_write_byte(w25q128_stm32_t* self, uint8_t dat)
{
    uint8_t rx_data;
    if (HAL_SPI_TransmitReceive(self->hspi, &dat, &rx_data, 1, HAL_MAX_DELAY) == HAL_OK) {
        return rx_data;
    }
    return 0xFF;
}

static void cs_on(w25q128_stm32_t* self, uint8_t state)
{
    HAL_GPIO_WritePin(self->cs_port, self->cs_pin, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void write_enable(w25q128_stm32_t* self)
{
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_WriteEnable);
    cs_on(self, 0);
}

static int16_t wait_busy_impl(w25q128_stm32_t* self)
{
    uint8_t byte = 0;
    do {
        cs_on(self, 1);
        spi_read_write_byte(self, W25X_ReadStatusReg);
        byte = spi_read_write_byte(self, 0xFF);
        cs_on(self, 0);
    } while ((byte & 0x01) == 1);
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_init(void* ctx)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    cs_on(self, 0);
    
    self->device_id = 0;
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_ManufactDeviceID);
    spi_read_write_byte(self, 0x00);
    spi_read_write_byte(self, 0x00);
    spi_read_write_byte(self, 0x00);
    self->device_id |= spi_read_write_byte(self, 0xFF) << 8;
    self->device_id |= spi_read_write_byte(self, 0xFF);
    cs_on(self, 0);
    
    if (self->device_id == 0xFFFF || self->device_id == 0x0000) {
        return SPI_FLASH_STATUS_ERROR;
    }
    
    self->base.total_size = W25Q128_TOTAL_SIZE;
    self->base.sector_size = W25Q128_SECTOR_SIZE;
    self->base.block_size = W25Q128_BLOCK_SIZE;
    self->base.page_size = W25Q128_PAGE_SIZE;
    
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_deinit(void* ctx)
{
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_read(void* ctx, uint32_t addr, uint8_t* buffer, uint32_t size)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    if (addr + size > self->base.total_size) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }
    
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_ReadData);
    spi_read_write_byte(self, (uint8_t)(addr >> 16));
    spi_read_write_byte(self, (uint8_t)(addr >> 8));
    spi_read_write_byte(self, (uint8_t)addr);
    
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = spi_read_write_byte(self, 0xFF);
    }
    
    cs_on(self, 0);
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_write(void* ctx, uint32_t addr, const uint8_t* buffer, uint32_t size)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    if (addr + size > self->base.total_size) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }
    
    write_enable(self);
    wait_busy_impl(self);
    
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_PageProgram);
    spi_read_write_byte(self, (uint8_t)(addr >> 16));
    spi_read_write_byte(self, (uint8_t)(addr >> 8));
    spi_read_write_byte(self, (uint8_t)addr);
    
    for (uint32_t i = 0; i < size; i++) {
        spi_read_write_byte(self, buffer[i]);
    }
    
    cs_on(self, 0);
    wait_busy_impl(self);
    
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_erase_sector(void* ctx, uint32_t sector)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    if (sector >= W25Q128_SECTOR_COUNT) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }
    
    uint32_t addr = sector * W25Q128_SECTOR_SIZE;
    
    write_enable(self);
    wait_busy_impl(self);
    
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_SectorErase);
    spi_read_write_byte(self, (uint8_t)(addr >> 16));
    spi_read_write_byte(self, (uint8_t)(addr >> 8));
    spi_read_write_byte(self, (uint8_t)addr);
    cs_on(self, 0);
    
    wait_busy_impl(self);
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_erase_block(void* ctx, uint32_t block)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    if (block >= W25Q128_BLOCK_COUNT) {
        return SPI_FLASH_STATUS_INVALID_PARAM;
    }
    
    uint32_t addr = block * W25Q128_BLOCK_SIZE;
    
    write_enable(self);
    wait_busy_impl(self);
    
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_BlockErase);
    spi_read_write_byte(self, (uint8_t)(addr >> 16));
    spi_read_write_byte(self, (uint8_t)(addr >> 8));
    spi_read_write_byte(self, (uint8_t)addr);
    cs_on(self, 0);
    
    wait_busy_impl(self);
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_erase_chip(void* ctx)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    
    write_enable(self);
    wait_busy_impl(self);
    
    cs_on(self, 1);
    spi_read_write_byte(self, W25X_ChipErase);
    cs_on(self, 0);
    
    wait_busy_impl(self);
    return SPI_FLASH_STATUS_OK;
}

static int16_t w25q128_sync(void* ctx)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    return wait_busy_impl(self);
}

static uint32_t w25q128_get_sector_size(void* ctx)
{
    return W25Q128_SECTOR_SIZE;
}

static uint32_t w25q128_get_block_size(void* ctx)
{
    return W25Q128_BLOCK_SIZE;
}

static uint32_t w25q128_get_total_size(void* ctx)
{
    return W25Q128_TOTAL_SIZE;
}

static uint16_t w25q128_get_page_size(void* ctx)
{
    return W25Q128_PAGE_SIZE;
}

static uint16_t w25q128_read_id(void* ctx)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    return self->device_id;
}

static int16_t w25q128_wait_busy(void* ctx)
{
    w25q128_stm32_t* self = container_of(ctx, w25q128_stm32_t, base);
    return wait_busy_impl(self);
}

static const platform_spi_flash_ops_t w25q128_ops = {
    .init = w25q128_init,
    .deinit = w25q128_deinit,
    .read = w25q128_read,
    .write = w25q128_write,
    .erase_sector = w25q128_erase_sector,
    .erase_block = w25q128_erase_block,
    .erase_chip = w25q128_erase_chip,
    .sync = w25q128_sync,
    .get_sector_size = w25q128_get_sector_size,
    .get_block_size = w25q128_get_block_size,
    .get_total_size = w25q128_get_total_size,
    .get_page_size = w25q128_get_page_size,
    .read_id = w25q128_read_id,
    .wait_busy = w25q128_wait_busy,
};

extern SPI_HandleTypeDef hspi1;

w25q128_stm32_t g_w25q128_flash = {
    .base = {
        .ops = &w25q128_ops,
        .name = "w25q128",
        .type = SPI_FLASH_TYPE_W25Q128,
        .total_size = W25Q128_TOTAL_SIZE,
        .sector_size = W25Q128_SECTOR_SIZE,
        .block_size = W25Q128_BLOCK_SIZE,
        .page_size = W25Q128_PAGE_SIZE,
        .user_data = NULL,
    },
    .hspi = &hspi1,
    .cs_port = GPIOA,
    .cs_pin = GPIO_PIN_4,
    .device_id = 0,
};
