#include "bootloader_core.h"
#include "main.h"
#include "ff.h"
#include "lfs.h"
#include <string.h>
#include "flash_if.h"

#define USER_FLASH_END_ADDRESS 0x080FFFFF

bootloader_ctx_t bootloader_ctx = {
    .app_jump_addr = APPLICATION_ADDRESS,
    .jump_func = jump_to_app,
};

/**
 * @brief  Jump to user application
 * @param  None
 * @retval None
 */
void jump_to_app(uint32_t app_address)
{
    pFunction jump_fn;
    SCB->VTOR = app_address;
    __DSB();
    __ISB();
    __set_MSP(*(__IO uint32_t *)app_address);
    jump_fn = (pFunction)(*(__IO uint32_t *)(app_address + 4));

    __disable_irq();
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    HAL_RCC_DeInit();
    HAL_DeInit();
    __enable_irq();

    jump_fn();
}

typedef struct
{
    uint32_t start_addr;
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
    uint8_t is_erased;
} internal_flash_tgt_ctx_t;

static internal_flash_tgt_ctx_t internal_flash_ctx;

bootloader_err_t internal_flash_tgt_open(void *ctx, const char *path, uint32_t total_size)
{
    internal_flash_target_priv_t *priv = (internal_flash_target_priv_t *)ctx;
    uint32_t StartSector, EndSector;
    FLASH_EraseInitTypeDef pEraseInit;
    uint32_t SectorError;

    (void)path;

    if (priv == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    memset(&internal_flash_ctx, 0, sizeof(internal_flash_ctx));
    internal_flash_ctx.start_addr = priv->start_addr;
    internal_flash_ctx.total_size = total_size;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    if (total_size > 0)
    {
        StartSector = GetSector(priv->start_addr);
        EndSector = GetSector(priv->start_addr + total_size - 1);

        pEraseInit.TypeErase = TYPEERASE_SECTORS;
        pEraseInit.Sector = StartSector;
        pEraseInit.NbSectors = EndSector - StartSector + 1;
        pEraseInit.VoltageRange = VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&pEraseInit, &SectorError) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return BOOTLOADER_ERR_ERASE;
        }
    }

    internal_flash_ctx.is_open = 1;
    internal_flash_ctx.is_erased = 1;
    internal_flash_ctx.written_size = 0;

    return BOOTLOADER_OK;
}

bootloader_err_t internal_flash_tgt_write(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    uint32_t i;
    uint32_t FlashAddress;
    uint32_t *DataWord;

    if (data == NULL || len == 0)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (!internal_flash_ctx.is_open)
    {
        return BOOTLOADER_ERR_WRITE;
    }

    FlashAddress = internal_flash_ctx.start_addr + offset;
    DataWord = (uint32_t *)data;

    for (i = 0; (i < (len / 4)) && (FlashAddress <= (USER_FLASH_END_ADDRESS - 4)); i++)
    {
        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord[i]) == HAL_OK)
        {
            if (*(uint32_t *)FlashAddress != DataWord[i])
            {
                return BOOTLOADER_ERR_VERIFY;
            }
            FlashAddress += 4;
        }
        else
        {
            return BOOTLOADER_ERR_WRITE;
        }
    }

    internal_flash_ctx.written_size = offset + (i * 4);

    return BOOTLOADER_OK;
}

bootloader_err_t internal_flash_tgt_close(void *ctx)
{
    (void)ctx;

    if (!internal_flash_ctx.is_open)
    {
        return BOOTLOADER_OK;
    }

    HAL_FLASH_Lock();

    internal_flash_ctx.is_open = 0;

    return BOOTLOADER_OK;
}

const target_if_t internal_flash_target_if = {
    .open = internal_flash_tgt_open,
    .write = internal_flash_tgt_write,
    .close = internal_flash_tgt_close,
};

// ==================== FATFS 源接口实现 ====================

typedef struct
{
    FATFS *fs;
    FIL file;
    char path[64];
    uint32_t total_size;
    uint8_t is_open;
} fatfs_src_ctx_t;

static fatfs_src_ctx_t fatfs_src_ctx;

bootloader_err_t fatfs_src_open(void *ctx, const char *path, uint32_t *total_size)
{
    fatfs_src_priv_t *priv = (fatfs_src_priv_t *)ctx;
    FRESULT res;
    FILINFO fno;

    if (priv == NULL || total_size == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (priv->fs == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    memset(&fatfs_src_ctx, 0, sizeof(fatfs_src_ctx));
    fatfs_src_ctx.fs = (FATFS *)priv->fs;

    if (path != NULL)
    {
        strncpy(fatfs_src_ctx.path, path, sizeof(fatfs_src_ctx.path) - 1);
    }
    else
    {
        strncpy(fatfs_src_ctx.path, priv->path, sizeof(fatfs_src_ctx.path) - 1);
    }
    fatfs_src_ctx.path[sizeof(fatfs_src_ctx.path) - 1] = '\0';

    res = f_stat(fatfs_src_ctx.path, &fno);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_OPEN_SRC;
    }

    fatfs_src_ctx.total_size = (uint32_t)fno.fsize;
    *total_size = fatfs_src_ctx.total_size;

    res = f_open(&fatfs_src_ctx.file, fatfs_src_ctx.path, FA_READ);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_OPEN_SRC;
    }

    fatfs_src_ctx.is_open = 1;

    return BOOTLOADER_OK;
}

bootloader_err_t fatfs_src_read(void *ctx, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    (void)ctx;
    FRESULT res;
    UINT br;

    if (buf == NULL || bytes_read == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (!fatfs_src_ctx.is_open)
    {
        return BOOTLOADER_ERR_READ;
    }

    res = f_read(&fatfs_src_ctx.file, buf, size, &br);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_READ;
    }

    *bytes_read = (uint32_t)br;

    return BOOTLOADER_OK;
}

bootloader_err_t fatfs_src_close(void *ctx)
{
    (void)ctx;
    FRESULT res;

    if (!fatfs_src_ctx.is_open)
    {
        return BOOTLOADER_OK;
    }

    res = f_close(&fatfs_src_ctx.file);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_CLOSE;
    }

    fatfs_src_ctx.is_open = 0;

    return BOOTLOADER_OK;
}

const source_if_t fatfs_source_if = {
    .open = fatfs_src_open,
    .read = fatfs_src_read,
    .close = fatfs_src_close,
};

// ==================== FATFS 目标接口实现 ====================

typedef struct
{
    FATFS *fs;
    FIL file;
    char path[64];
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
} fatfs_tgt_ctx_t;

static fatfs_tgt_ctx_t fatfs_tgt_ctx;

bootloader_err_t fatfs_tgt_open(void *ctx, const char *path, uint32_t total_size)
{
    fatfs_target_priv_t *priv = (fatfs_target_priv_t *)ctx;
    FRESULT res;

    if (priv == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (priv->fs == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    memset(&fatfs_tgt_ctx, 0, sizeof(fatfs_tgt_ctx));
    fatfs_tgt_ctx.fs = (FATFS *)priv->fs;

    if (path != NULL)
    {
        strncpy(fatfs_tgt_ctx.path, path, sizeof(fatfs_tgt_ctx.path) - 1);
    }
    else
    {
        strncpy(fatfs_tgt_ctx.path, priv->path, sizeof(fatfs_tgt_ctx.path) - 1);
    }
    fatfs_tgt_ctx.path[sizeof(fatfs_tgt_ctx.path) - 1] = '\0';

    fatfs_tgt_ctx.total_size = total_size;

    res = f_open(&fatfs_tgt_ctx.file, fatfs_tgt_ctx.path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_OPEN_DST;
    }

    if (total_size > 0)
    {
        res = f_lseek(&fatfs_tgt_ctx.file, total_size);
        if (res != FR_OK)
        {
            f_close(&fatfs_tgt_ctx.file);
            return BOOTLOADER_ERR_OPEN_DST;
        }

        res = f_truncate(&fatfs_tgt_ctx.file);
        if (res != FR_OK)
        {
            f_close(&fatfs_tgt_ctx.file);
            return BOOTLOADER_ERR_OPEN_DST;
        }

        res = f_lseek(&fatfs_tgt_ctx.file, 0);
        if (res != FR_OK)
        {
            f_close(&fatfs_tgt_ctx.file);
            return BOOTLOADER_ERR_OPEN_DST;
        }
    }

    fatfs_tgt_ctx.is_open = 1;
    fatfs_tgt_ctx.written_size = 0;

    return BOOTLOADER_OK;
}

bootloader_err_t fatfs_tgt_write(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    FRESULT res;
    UINT bw;

    if (data == NULL || len == 0)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (!fatfs_tgt_ctx.is_open)
    {
        return BOOTLOADER_ERR_WRITE;
    }

    if (offset != fatfs_tgt_ctx.written_size)
    {
        res = f_lseek(&fatfs_tgt_ctx.file, offset);
        if (res != FR_OK)
        {
            return BOOTLOADER_ERR_WRITE;
        }
    }

    res = f_write(&fatfs_tgt_ctx.file, data, len, &bw);
    if (res != FR_OK || bw != len)
    {
        return BOOTLOADER_ERR_WRITE;
    }

    fatfs_tgt_ctx.written_size = offset + len;

    return BOOTLOADER_OK;
}

bootloader_err_t fatfs_tgt_close(void *ctx)
{
    (void)ctx;
    FRESULT res;

    if (!fatfs_tgt_ctx.is_open)
    {
        return BOOTLOADER_OK;
    }

    res = f_sync(&fatfs_tgt_ctx.file);
    if (res != FR_OK)
    {
        f_close(&fatfs_tgt_ctx.file);
        return BOOTLOADER_ERR_CLOSE;
    }

    res = f_close(&fatfs_tgt_ctx.file);
    if (res != FR_OK)
    {
        return BOOTLOADER_ERR_CLOSE;
    }

    fatfs_tgt_ctx.is_open = 0;

    return BOOTLOADER_OK;
}

const target_if_t fatfs_target_if = {
    .open = fatfs_tgt_open,
    .write = fatfs_tgt_write,
    .close = fatfs_tgt_close,
};

// ==================== LFS (LittleFS) 源接口实现 ====================

typedef struct
{
    lfs_t *lfs;
    lfs_file_t file;
    char path[64];
    uint32_t total_size;
    uint8_t is_open;
} lfs_src_ctx_t;

static lfs_src_ctx_t lfs_src_ctx;

bootloader_err_t lfs_src_open(void *ctx, const char *path, uint32_t *total_size)
{
    lfs_src_priv_t *priv = (lfs_src_priv_t *)ctx;
    struct lfs_info info;
    int res;

    if (priv == NULL || total_size == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (priv->lfs == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    memset(&lfs_src_ctx, 0, sizeof(lfs_src_ctx));
    lfs_src_ctx.lfs = (lfs_t *)priv->lfs;

    if (path != NULL)
    {
        strncpy(lfs_src_ctx.path, path, sizeof(lfs_src_ctx.path) - 1);
    }
    else
    {
        strncpy(lfs_src_ctx.path, priv->path, sizeof(lfs_src_ctx.path) - 1);
    }
    lfs_src_ctx.path[sizeof(lfs_src_ctx.path) - 1] = '\0';

    res = lfs_stat(lfs_src_ctx.lfs, lfs_src_ctx.path, &info);
    if (res != LFS_ERR_OK)
    {
        return BOOTLOADER_ERR_OPEN_SRC;
    }

    if (info.type != LFS_TYPE_REG)
    {
        return BOOTLOADER_ERR_OPEN_SRC;
    }

    lfs_src_ctx.total_size = (uint32_t)info.size;
    *total_size = lfs_src_ctx.total_size;

    res = lfs_file_open(lfs_src_ctx.lfs, &lfs_src_ctx.file, lfs_src_ctx.path, LFS_O_RDONLY);
    if (res != LFS_ERR_OK)
    {
        return BOOTLOADER_ERR_OPEN_SRC;
    }

    lfs_src_ctx.is_open = 1;

    return BOOTLOADER_OK;
}

bootloader_err_t lfs_src_read(void *ctx, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    (void)ctx;
    lfs_ssize_t res;

    if (buf == NULL || bytes_read == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (!lfs_src_ctx.is_open)
    {
        return BOOTLOADER_ERR_READ;
    }

    res = lfs_file_read(lfs_src_ctx.lfs, &lfs_src_ctx.file, buf, size);
    if (res < 0)
    {
        return BOOTLOADER_ERR_READ;
    }

    *bytes_read = (uint32_t)res;

    return BOOTLOADER_OK;
}

bootloader_err_t lfs_src_close(void *ctx)
{
    (void)ctx;
    int res;

    if (!lfs_src_ctx.is_open)
    {
        return BOOTLOADER_OK;
    }

    res = lfs_file_close(lfs_src_ctx.lfs, &lfs_src_ctx.file);
    if (res != LFS_ERR_OK)
    {
        return BOOTLOADER_ERR_CLOSE;
    }

    lfs_src_ctx.is_open = 0;

    return BOOTLOADER_OK;
}

const source_if_t lfs_source_if = {
    .open = lfs_src_open,
    .read = lfs_src_read,
    .close = lfs_src_close,
};

// ==================== LFS (LittleFS) 目标接口实现 ====================

typedef struct
{
    lfs_t *lfs;
    lfs_file_t file;
    char path[64];
    uint32_t total_size;
    uint32_t written_size;
    uint8_t is_open;
} lfs_tgt_ctx_t;

static lfs_tgt_ctx_t lfs_tgt_ctx;

bootloader_err_t lfs_tgt_open(void *ctx, const char *path, uint32_t total_size)
{
    lfs_target_priv_t *priv = (lfs_target_priv_t *)ctx;
    int res;

    if (priv == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (priv->lfs == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    memset(&lfs_tgt_ctx, 0, sizeof(lfs_tgt_ctx));
    lfs_tgt_ctx.lfs = (lfs_t *)priv->lfs;

    if (path != NULL)
    {
        strncpy(lfs_tgt_ctx.path, path, sizeof(lfs_tgt_ctx.path) - 1);
    }
    else
    {
        strncpy(lfs_tgt_ctx.path, priv->path, sizeof(lfs_tgt_ctx.path) - 1);
    }
    lfs_tgt_ctx.path[sizeof(lfs_tgt_ctx.path) - 1] = '\0';

    lfs_tgt_ctx.total_size = total_size;

    res = lfs_file_open(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file, lfs_tgt_ctx.path,
                        LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (res != LFS_ERR_OK)
    {
        return BOOTLOADER_ERR_OPEN_DST;
    }

    lfs_tgt_ctx.is_open = 1;
    lfs_tgt_ctx.written_size = 0;

    return BOOTLOADER_OK;
}

bootloader_err_t lfs_tgt_write(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    lfs_ssize_t res;

    if (data == NULL || len == 0)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (!lfs_tgt_ctx.is_open)
    {
        return BOOTLOADER_ERR_WRITE;
    }

    if (offset != lfs_tgt_ctx.written_size)
    {
        res = lfs_file_seek(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file, offset, LFS_SEEK_SET);
        if (res < 0)
        {
            return BOOTLOADER_ERR_WRITE;
        }
    }

    res = lfs_file_write(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file, data, len);
    if (res != (lfs_ssize_t)len)
    {
        return BOOTLOADER_ERR_WRITE;
    }

    lfs_tgt_ctx.written_size = offset + len;

    return BOOTLOADER_OK;
}

bootloader_err_t lfs_tgt_close(void *ctx)
{
    (void)ctx;
    int res;

    if (!lfs_tgt_ctx.is_open)
    {
        return BOOTLOADER_OK;
    }

    res = lfs_file_sync(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file);
    if (res != LFS_ERR_OK)
    {
        lfs_file_close(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file);
        return BOOTLOADER_ERR_CLOSE;
    }

    res = lfs_file_close(lfs_tgt_ctx.lfs, &lfs_tgt_ctx.file);
    if (res != LFS_ERR_OK)
    {
        return BOOTLOADER_ERR_CLOSE;
    }

    lfs_tgt_ctx.is_open = 0;

    return BOOTLOADER_OK;
}

const target_if_t lfs_target_if = {
    .open = lfs_tgt_open,
    .write = lfs_tgt_write,
    .close = lfs_tgt_close,
};

// ==================== 统一下载函数实现 ====================

#define BOOTLOADER_BUFFER_SIZE 4096

static uint8_t bootloader_buffer[BOOTLOADER_BUFFER_SIZE];

bootloader_err_t bootloader_download(const source_if_t *src_if, void *src_ctx,
                                     const target_if_t *tgt_if, void *tgt_ctx,
                                     const char *path)
{
    bootloader_err_t err;
    uint32_t total_size = 0;
    uint32_t bytes_read = 0;
    uint32_t total_read = 0;
    uint32_t offset = 0;

    if (src_if == NULL || tgt_if == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (src_if->open == NULL || src_if->read == NULL || src_if->close == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    if (tgt_if->open == NULL || tgt_if->write == NULL || tgt_if->close == NULL)
    {
        return BOOTLOADER_ERR_PARAM;
    }

    err = src_if->open(src_ctx, path, &total_size);
    if (err != BOOTLOADER_OK)
    {
        return err;
    }

    err = tgt_if->open(tgt_ctx, path, total_size);
    if (err != BOOTLOADER_OK)
    {
        src_if->close(src_ctx);
        return err;
    }

    while (total_read < total_size)
    {
        uint32_t to_read = BOOTLOADER_BUFFER_SIZE;
        if (total_size - total_read < to_read)
        {
            to_read = total_size - total_read;
        }

        err = src_if->read(src_ctx, bootloader_buffer, to_read, &bytes_read);
        if (err != BOOTLOADER_OK)
        {
            tgt_if->close(tgt_ctx);
            src_if->close(src_ctx);
            return err;
        }

        if (bytes_read == 0)
        {
            break;
        }

        err = tgt_if->write(tgt_ctx, offset, bootloader_buffer, bytes_read);
        if (err != BOOTLOADER_OK)
        {
            tgt_if->close(tgt_ctx);
            src_if->close(src_ctx);
            return err;
        }

        total_read += bytes_read;
        offset += bytes_read;
    }

    err = tgt_if->close(tgt_ctx);
    if (err != BOOTLOADER_OK)
    {
        src_if->close(src_ctx);
        return err;
    }

    err = src_if->close(src_ctx);
    if (err != BOOTLOADER_OK)
    {
        return err;
    }

    return BOOTLOADER_OK;
}
