#include "platform_fatfs_stm32_impl.h"
#include <string.h>
#include <stdio.h>

static int16_t fatfs_src_open(const void *ctx, const char *path, uint32_t *total_size)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;
    FILINFO fno;

    if (total_size == NULL || self->fs == NULL)
    {
        return STORAGE_STATUS_PARAM;
    }

    memset(&self->file, 0, sizeof(self->file));
    self->is_source = 1;

    if (path != NULL)
    {
        strncpy(self->path, path, sizeof(self->path) - 1);
    }
    self->path[sizeof(self->path) - 1] = '\0';

    res = f_stat(self->path, &fno);
    if (res != FR_OK)
    {
        printf("FATFS: file not found %s, res=%d\r\n", self->path, res);
        return STORAGE_STATUS_OPEN_SRC;
    }

    self->total_size = (uint32_t)fno.fsize;
    *total_size = self->total_size;

    res = f_open(&self->file, self->path, FA_READ);
    if (res != FR_OK)
    {
        printf("FATFS: open failed %s, res=%d\r\n", self->path, res);
        return STORAGE_STATUS_OPEN_SRC;
    }

    self->is_open = 1;
    printf("FATFS source opened: %s, size=%lu\r\n", self->path, (unsigned long)self->total_size);
    return STORAGE_STATUS_OK;
}

static int16_t fatfs_src_read(const void *ctx, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;
    UINT br;

    if (buf == NULL || bytes_read == NULL)
    {
        return STORAGE_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("FATFS: read failed, not open\r\n");
        return STORAGE_STATUS_READ;
    }

    res = f_read(&self->file, buf, size, &br);
    if (res != FR_OK)
    {
        printf("FATFS: read failed, res=%d\r\n", res);
        return STORAGE_STATUS_READ;
    }

    *bytes_read = (uint32_t)br;
    return STORAGE_STATUS_OK;
}

static int16_t fatfs_src_close(const void *ctx)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;

    if (!self->is_open)
    {
        return STORAGE_STATUS_OK;
    }

    res = f_close(&self->file);
    if (res != FR_OK)
    {
        printf("FATFS: close failed, res=%d\r\n", res);
        return STORAGE_STATUS_CLOSE;
    }

    self->is_open = 0;
    printf("FATFS source closed\r\n");
    return STORAGE_STATUS_OK;
}

static int16_t fatfs_tgt_open(const void *ctx, const char *path, uint32_t total_size)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;

    if (self->fs == NULL)
    {
        return STORAGE_STATUS_PARAM;
    }

    memset(&self->file, 0, sizeof(self->file));
    self->is_source = 0;

    if (path != NULL)
    {
        strncpy(self->path, path, sizeof(self->path) - 1);
    }
    self->path[sizeof(self->path) - 1] = '\0';

    self->total_size = total_size;

    res = f_open(&self->file, self->path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK)
    {
        printf("FATFS: create failed %s, res=%d\r\n", self->path, res);
        return STORAGE_STATUS_OPEN_DST;
    }

    if (total_size > 0)
    {
        res = f_lseek(&self->file, total_size);
        if (res != FR_OK)
        {
            f_close(&self->file);
            printf("FATFS: seek failed, res=%d\r\n", res);
            return STORAGE_STATUS_OPEN_DST;
        }

        res = f_truncate(&self->file);
        if (res != FR_OK)
        {
            f_close(&self->file);
            printf("FATFS: truncate failed, res=%d\r\n", res);
            return STORAGE_STATUS_OPEN_DST;
        }

        res = f_lseek(&self->file, 0);
        if (res != FR_OK)
        {
            f_close(&self->file);
            printf("FATFS: seek to 0 failed, res=%d\r\n", res);
            return STORAGE_STATUS_OPEN_DST;
        }
    }

    self->is_open = 1;
    self->written_size = 0;
    printf("FATFS target opened: %s, size=%lu\r\n", self->path, (unsigned long)total_size);
    return STORAGE_STATUS_OK;
}

static int16_t fatfs_tgt_write(const void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;
    UINT bw;

    if (data == NULL || len == 0)
    {
        return STORAGE_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("FATFS: write failed, not open\r\n");
        return STORAGE_STATUS_WRITE;
    }

    if (offset != self->written_size)
    {
        res = f_lseek(&self->file, offset);
        if (res != FR_OK)
        {
            printf("FATFS: seek failed at offset %lu, res=%d\r\n", (unsigned long)offset, res);
            return STORAGE_STATUS_WRITE;
        }
    }

    res = f_write(&self->file, data, len, &bw);
    if (res != FR_OK || bw != len)
    {
        printf("FATFS: write failed, res=%d, bw=%u\r\n", res, bw);
        return STORAGE_STATUS_WRITE;
    }

    self->written_size = offset + len;
    return STORAGE_STATUS_OK;
}

static int16_t fatfs_tgt_close(const void *ctx)
{
    fatfs_stm32_t *self = container_of(ctx, fatfs_stm32_t, base);
    FRESULT res;

    if (!self->is_open)
    {
        return STORAGE_STATUS_OK;
    }

    res = f_sync(&self->file);
    if (res != FR_OK)
    {
        f_close(&self->file);
        printf("FATFS: sync failed, res=%d\r\n", res);
        return STORAGE_STATUS_CLOSE;
    }

    res = f_close(&self->file);
    if (res != FR_OK)
    {
        printf("FATFS: close failed, res=%d\r\n", res);
        return STORAGE_STATUS_CLOSE;
    }

    self->is_open = 0;
    printf("FATFS target closed\r\n");
    return STORAGE_STATUS_OK;
}

static const platform_storage_source_ops_t fatfs_source_ops = {
    .open = fatfs_src_open,
    .read = fatfs_src_read,
    .close = fatfs_src_close,
};

static const platform_storage_target_ops_t fatfs_target_ops = {
    .open = fatfs_tgt_open,
    .write = fatfs_tgt_write,
    .close = fatfs_tgt_close,
};

fatfs_stm32_t g_fatfs_storage = {
    .base = {
        .source_ops = &fatfs_source_ops,
        .target_ops = &fatfs_target_ops,
        .name = "fatfs",
        .type = STORAGE_TYPE_SD_CARD_FATFS,
        .user_data = NULL,
    },
    .fs = NULL,
    .total_size = 0,
    .written_size = 0,
    .is_open = 0,
    .is_source = 0,
};
