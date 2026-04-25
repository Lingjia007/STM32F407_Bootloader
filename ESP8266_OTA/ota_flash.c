#include "ota_flash.h"
#include "w25q128.h"
#include "flash_if.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>

#define OTA_META_FLASH_ADDR 0x080FC000U
#define OTA_META_SECTOR FLASH_SECTOR_11

static void OTA_SPIFlash_WriteNoErase(uint32_t addr, const uint8_t *data, uint16_t len)
{
    W25Q128_write_enable();
    W25Q128_wait_busy();

    W25QXX_CS_ON(1);
    spi_read_write_byte(W25X_PageProgram);
    spi_read_write_byte((uint8_t)((addr) >> 16));
    spi_read_write_byte((uint8_t)((addr) >> 8));
    spi_read_write_byte((uint8_t)addr);

    for (uint16_t i = 0; i < len; i++)
    {
        spi_read_write_byte(data[i]);
    }

    W25QXX_CS_ON(0);
    W25Q128_wait_busy();
}

static void OTA_SPIFlash_Read(uint32_t addr, uint8_t *data, uint16_t len)
{
    W25QXX_CS_ON(1);
    spi_read_write_byte(W25X_ReadData);
    spi_read_write_byte((uint8_t)((addr) >> 16));
    spi_read_write_byte((uint8_t)((addr) >> 8));
    spi_read_write_byte((uint8_t)addr);

    for (uint16_t i = 0; i < len; i++)
    {
        data[i] = spi_read_write_byte(0xFF);
    }

    W25QXX_CS_ON(0);
}

uint32_t OTA_GetDownloadAddr(void)
{
    return OTA_SPI_FLASH_ADDR;
}

uint32_t OTA_GetDownloadMaxSize(void)
{
    return OTA_SPI_FLASH_SIZE;
}

int OTA_PrepareDownloadArea(uint32_t image_size)
{
    if (image_size == 0 || image_size > OTA_SPI_FLASH_SIZE)
    {
        printf("OTA: invalid image size %lu\r\n", (unsigned long)image_size);
        return 0;
    }

    printf("OTA: erasing SPI Flash download area...\r\n");

    uint32_t start_sector = OTA_SPI_FLASH_ADDR / 4096U;
    uint32_t sector_count = (image_size + 4095U) / 4096U;

    for (uint32_t i = 0; i < sector_count; i++)
    {
        W25Q128_erase_sector(start_sector + i);

        if ((i % 32) == 0)
        {
            printf("OTA: erase sector %lu/%lu\r\n", (unsigned long)i, (unsigned long)sector_count);
        }
    }

    printf("OTA: erase success, %lu sectors\r\n", (unsigned long)sector_count);
    return 1;
}

int OTA_WriteDownloadChunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0)
    {
        return 0;
    }

    uint32_t addr = OTA_SPI_FLASH_ADDR + offset;

    uint32_t remaining = len;
    uint32_t src_offset = 0;

    while (remaining > 0)
    {
        uint32_t page_offset = addr % 256U;
        uint32_t write_len = 256U - page_offset;

        if (write_len > remaining)
        {
            write_len = remaining;
        }

        if (write_len > 65535U)
        {
            write_len = 65535U;
        }

        OTA_SPIFlash_WriteNoErase(addr, data + src_offset, (uint16_t)write_len);

        addr += write_len;
        src_offset += write_len;
        remaining -= write_len;
    }

    return 1;
}

void OTA_ReadDownloadData(uint32_t offset, uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0)
    {
        return;
    }

    uint32_t addr = OTA_SPI_FLASH_ADDR + offset;

    uint32_t remaining = len;
    uint32_t dst_offset = 0;

    while (remaining > 0)
    {
        uint16_t read_len = (remaining > 65535U) ? 65535U : (uint16_t)remaining;

        OTA_SPIFlash_Read(addr + dst_offset, data + dst_offset, read_len);

        dst_offset += read_len;
        remaining -= read_len;
    }
}

static int OTA_EraseMetaSector(void)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase_init.TypeErase = TYPEERASE_SECTORS;
    erase_init.Sector = OTA_META_SECTOR;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = VOLTAGE_RANGE_3;

    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &sector_error);

    HAL_FLASH_Lock();

    return (status == HAL_OK) ? 1 : 0;
}

static int OTA_WriteMeta(const OtaBootMeta *meta)
{
    if (!OTA_EraseMetaSector())
    {
        printf("OTA: erase meta sector failed\r\n");
        return 0;
    }

    HAL_FLASH_Unlock();

    const uint32_t *data = (const uint32_t *)meta;
    uint32_t addr = OTA_META_FLASH_ADDR;
    uint32_t word_count = sizeof(OtaBootMeta) / 4;

    for (uint32_t i = 0; i < word_count; i++)
    {
        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, addr, data[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            printf("OTA: write meta failed at word %lu\r\n", (unsigned long)i);
            return 0;
        }
        addr += 4;
    }

    HAL_FLASH_Lock();

    return 1;
}

int OTA_ReadBootMeta(void *meta, uint32_t meta_size)
{
    if (meta == NULL || meta_size == 0)
    {
        return 0;
    }

    memcpy(meta, (void *)OTA_META_FLASH_ADDR, meta_size);
    return 1;
}

int OTA_SetPendingImage(uint32_t image_size, const uint8_t md5[16], const char *version, const char *token)
{
    OtaBootMeta meta;

    memset(&meta, 0, sizeof(meta));
    meta.magic = OTA_META_MAGIC;
    meta.state = OTA_META_STATE_PENDING;
    meta.image_size = image_size;

    if (md5 != NULL)
    {
        memcpy(meta.md5, md5, 16);
    }

    if (version != NULL)
    {
        strncpy(meta.target_version, version, sizeof(meta.target_version) - 1);
    }

    if (token != NULL)
    {
        strncpy(meta.ota_token, token, sizeof(meta.ota_token) - 1);
    }

    if (!OTA_WriteMeta(&meta))
    {
        printf("OTA: set pending image failed\r\n");
        return 0;
    }

    printf("OTA: set pending image success, size=%lu\r\n", (unsigned long)image_size);
    return 1;
}

int OTA_ClearPendingImage(void)
{
    OtaBootMeta meta;

    memset(&meta, 0, sizeof(meta));
    meta.magic = OTA_META_MAGIC;
    meta.state = OTA_META_STATE_NONE;

    if (!OTA_WriteMeta(&meta))
    {
        printf("OTA: clear pending image failed\r\n");
        return 0;
    }

    printf("OTA: clear pending image success\r\n");
    return 1;
}
