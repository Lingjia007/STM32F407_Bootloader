#include "service_lfs_spi_flash_adapter.h"
#include <string.h>
#include <stdio.h>

struct lfs lfs_instance;

static int lfs_spi_flash_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    platform_spi_flash_base_t *flash = &g_w25q128_flash.base;
    uint32_t addr = block * SPI_FLASH_BLOCK_SIZE + off;

    if (block >= SPI_FLASH_BLOCK_COUNT || (off + size) > SPI_FLASH_BLOCK_SIZE)
    {
        printf("LFS read: invalid params, block=%lu, off=%lu, size=%lu\r\n", 
               (unsigned long)block, (unsigned long)off, (unsigned long)size);
        return LFS_ERR_INVAL;
    }

    int16_t ret = SPI_FLASH_READ(flash, addr, (uint8_t *)buffer, size);
    if (ret != SPI_FLASH_STATUS_OK)
    {
        printf("SPI flash read error at addr 0x%08lX, size %lu, ret=%d\r\n", 
               (unsigned long)addr, (unsigned long)size, ret);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_spi_flash_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    platform_spi_flash_base_t *flash = &g_w25q128_flash.base;
    uint32_t addr = block * SPI_FLASH_BLOCK_SIZE + off;

    if (block >= SPI_FLASH_BLOCK_COUNT || (off + size) > SPI_FLASH_BLOCK_SIZE)
    {
        printf("LFS prog: invalid params, block=%lu, off=%lu, size=%lu\r\n", 
               (unsigned long)block, (unsigned long)off, (unsigned long)size);
        return LFS_ERR_INVAL;
    }

    int16_t ret = SPI_FLASH_WRITE(flash, addr, (const uint8_t *)buffer, size);
    if (ret != SPI_FLASH_STATUS_OK)
    {
        printf("SPI flash write error at addr 0x%08lX, size %lu, ret=%d\r\n", 
               (unsigned long)addr, (unsigned long)size, ret);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_spi_flash_erase(const struct lfs_config *c, lfs_block_t block)
{
    platform_spi_flash_base_t *flash = &g_w25q128_flash.base;

    if (block >= SPI_FLASH_BLOCK_COUNT)
    {
        printf("LFS erase: invalid block=%lu\r\n", (unsigned long)block);
        return LFS_ERR_INVAL;
    }

    int16_t ret = SPI_FLASH_ERASE_SECTOR(flash, block);
    if (ret != SPI_FLASH_STATUS_OK)
    {
        printf("SPI flash erase error at block %lu, ret=%d\r\n", 
               (unsigned long)block, ret);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int lfs_spi_flash_sync(const struct lfs_config *c)
{
    platform_spi_flash_base_t *flash = &g_w25q128_flash.base;
    int16_t ret = SPI_FLASH_SYNC(flash);
    if (ret != SPI_FLASH_STATUS_OK)
    {
        printf("SPI flash sync error, ret=%d\r\n", ret);
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

const struct lfs_config lfs_spi_flash_cfg = {
    .context = NULL,
    .read = lfs_spi_flash_read,
    .prog = lfs_spi_flash_prog,
    .erase = lfs_spi_flash_erase,
    .sync = lfs_spi_flash_sync,
    .read_size = SPI_FLASH_READ_SIZE,
    .prog_size = SPI_FLASH_PROG_SIZE,
    .block_size = SPI_FLASH_BLOCK_SIZE,
    .block_count = SPI_FLASH_BLOCK_COUNT,
    .cache_size = SPI_FLASH_PROG_SIZE,
    .lookahead_size = 16,
    .block_cycles = 500,
};

int lfs_spi_flash_init(void)
{
    platform_spi_flash_base_t *flash = &g_w25q128_flash.base;

    int16_t ret = SPI_FLASH_INIT(flash);
    if (ret != SPI_FLASH_STATUS_OK)
    {
        printf("SPI flash init failed, ret=%d\r\n", ret);
        return -1;
    }

    uint16_t flash_id = SPI_FLASH_READ_ID(flash);
    if (flash_id == 0xFFFF || flash_id == 0x0000)
    {
        printf("SPI flash invalid ID: 0x%04X\r\n", flash_id);
        return -1;
    }

    printf("SPI flash init success, ID=0x%04X\r\n", flash_id);
    return 0;
}

int lfs_spi_flash_mount(struct lfs *lfs)
{
    struct lfs *target_lfs = (lfs != NULL) ? lfs : &lfs_instance;

    int err = lfs_mount(target_lfs, &lfs_spi_flash_cfg);

    if (err != LFS_ERR_OK)
    {
        printf("LFS mount failed (err=%d), formatting...\r\n", err);
        err = lfs_format(target_lfs, &lfs_spi_flash_cfg);
        if (err != LFS_ERR_OK)
        {
            printf("LFS format failed, err=%d\r\n", err);
            return err;
        }

        err = lfs_mount(target_lfs, &lfs_spi_flash_cfg);
        if (err != LFS_ERR_OK)
        {
            printf("LFS remount failed, err=%d\r\n", err);
            return err;
        }
    }

    printf("LFS mounted successfully\r\n");
    return err;
}

int lfs_spi_flash_unmount(struct lfs *lfs)
{
    struct lfs *target_lfs = (lfs != NULL) ? lfs : &lfs_instance;

    return lfs_unmount(target_lfs);
}

int lfs_spi_flash_format(struct lfs *lfs)
{
    struct lfs *target_lfs = (lfs != NULL) ? lfs : &lfs_instance;

    return lfs_format(target_lfs, &lfs_spi_flash_cfg);
}
