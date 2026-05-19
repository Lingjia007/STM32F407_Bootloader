#include "platform_lfs_stm32_impl.h"
#include <string.h>
#include <stdio.h>

static int16_t lfs_src_open(const void* ctx, const char* path, uint32_t* total_size)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    struct lfs_info info;
    int res;

    if (total_size == NULL || self->lfs == NULL)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    memset(&self->file, 0, sizeof(self->file));
    self->is_source = 1;

    if (path != NULL)
    {
        strncpy(self->path, path, sizeof(self->path) - 1);
    }
    self->path[sizeof(self->path) - 1] = '\0';

    res = lfs_stat(self->lfs, self->path, &info);
    if (res != LFS_ERR_OK)
    {
        printf("LFS: file not found %s, res=%d\r\n", self->path, res);
        return TRANSPORT_STATUS_OPEN_SRC;
    }

    if (info.type != LFS_TYPE_REG)
    {
        printf("LFS: not a regular file %s\r\n", self->path);
        return TRANSPORT_STATUS_OPEN_SRC;
    }

    self->total_size = (uint32_t)info.size;
    *total_size = self->total_size;

    res = lfs_file_open(self->lfs, &self->file, self->path, LFS_O_RDONLY);
    if (res != LFS_ERR_OK)
    {
        printf("LFS: open failed %s, res=%d\r\n", self->path, res);
        return TRANSPORT_STATUS_OPEN_SRC;
    }

    self->is_open = 1;
    printf("LFS source opened: %s, size=%lu\r\n", self->path, (unsigned long)self->total_size);
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_src_read(const void* ctx, uint8_t* buf, uint32_t size, uint32_t* bytes_read)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    lfs_ssize_t res;

    if (buf == NULL || bytes_read == NULL)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("LFS: read failed, not open\r\n");
        return TRANSPORT_STATUS_READ;
    }

    res = lfs_file_read(self->lfs, &self->file, buf, size);
    if (res < 0)
    {
        printf("LFS: read failed, res=%ld\r\n", (long)res);
        return TRANSPORT_STATUS_READ;
    }

    *bytes_read = (uint32_t)res;
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_src_close(const void* ctx)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    int res;

    if (!self->is_open)
    {
        return TRANSPORT_STATUS_OK;
    }

    res = lfs_file_close(self->lfs, &self->file);
    if (res != LFS_ERR_OK)
    {
        printf("LFS: close failed, res=%d\r\n", res);
        return TRANSPORT_STATUS_CLOSE;
    }

    self->is_open = 0;
    printf("LFS source closed\r\n");
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_tgt_open(const void* ctx, const char* path, uint32_t total_size)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    int res;

    if (self->lfs == NULL)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    memset(&self->file, 0, sizeof(self->file));
    self->is_source = 0;

    if (path != NULL)
    {
        strncpy(self->path, path, sizeof(self->path) - 1);
    }
    self->path[sizeof(self->path) - 1] = '\0';

    self->total_size = total_size;

    res = lfs_file_open(self->lfs, &self->file, self->path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (res != LFS_ERR_OK)
    {
        printf("LFS: create failed %s, res=%d\r\n", self->path, res);
        return TRANSPORT_STATUS_OPEN_DST;
    }

    self->is_open = 1;
    self->written_size = 0;
    printf("LFS target opened: %s, size=%lu\r\n", self->path, (unsigned long)total_size);
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_tgt_write(const void* ctx, uint32_t offset, const uint8_t* data, uint32_t len)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    lfs_ssize_t res;

    if (data == NULL || len == 0)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("LFS: write failed, not open\r\n");
        return TRANSPORT_STATUS_WRITE;
    }

    if (offset != self->written_size)
    {
        res = lfs_file_seek(self->lfs, &self->file, offset, LFS_SEEK_SET);
        if (res < 0)
        {
            printf("LFS: seek failed at offset %lu, res=%ld\r\n", (unsigned long)offset, (long)res);
            return TRANSPORT_STATUS_WRITE;
        }
    }

    res = lfs_file_write(self->lfs, &self->file, data, len);
    if (res != (lfs_ssize_t)len)
    {
        printf("LFS: write failed, res=%ld, expected=%lu\r\n", (long)res, (unsigned long)len);
        return TRANSPORT_STATUS_WRITE;
    }

    self->written_size = offset + len;
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_tgt_close(const void* ctx)
{
    lfs_stm32_t* self = container_of(ctx, lfs_stm32_t, base);
    int res;

    if (!self->is_open)
    {
        return TRANSPORT_STATUS_OK;
    }

    res = lfs_file_sync(self->lfs, &self->file);
    if (res != LFS_ERR_OK)
    {
        lfs_file_close(self->lfs, &self->file);
        printf("LFS: sync failed, res=%d\r\n", res);
        return TRANSPORT_STATUS_CLOSE;
    }

    res = lfs_file_close(self->lfs, &self->file);
    if (res != LFS_ERR_OK)
    {
        printf("LFS: close failed, res=%d\r\n", res);
        return TRANSPORT_STATUS_CLOSE;
    }

    self->is_open = 0;
    printf("LFS target closed\r\n");
    return TRANSPORT_STATUS_OK;
}

static int16_t lfs_tgt_read(const void *ctx, uint32_t offset, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    lfs_stm32_t *self = container_of(ctx, lfs_stm32_t, base);
    lfs_ssize_t res;

    if (buf == NULL || size == 0)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("LFS: read failed, not open\r\n");
        return TRANSPORT_STATUS_READ;
    }

    res = lfs_file_seek(self->lfs, &self->file, offset, LFS_SEEK_SET);
    if (res < 0)
    {
        printf("LFS: seek failed at offset %lu, res=%ld\r\n", (unsigned long)offset, (long)res);
        return TRANSPORT_STATUS_READ;
    }

    res = lfs_file_read(self->lfs, &self->file, buf, size);
    if (res < 0)
    {
        printf("LFS: read failed, res=%ld\r\n", (long)res);
        return TRANSPORT_STATUS_READ;
    }

    *bytes_read = (uint32_t)res;
    return TRANSPORT_STATUS_OK;
}

static const platform_transport_source_ops_t lfs_source_ops = {
    .open = lfs_src_open,
    .read = lfs_src_read,
    .close = lfs_src_close,
};

static const platform_transport_target_ops_t lfs_target_ops = {
    .open = lfs_tgt_open,
    .write = lfs_tgt_write,
    .read = lfs_tgt_read,
    .close = lfs_tgt_close,
};

void platform_lfs_stm32_register(lfs_stm32_t *transport, const char *name)
{
    if (transport == NULL)
    {
        return;
    }

    transport->base.source_ops = &lfs_source_ops;
    transport->base.target_ops = &lfs_target_ops;
    transport->base.name = name;
    transport->base.type = TRANSPORT_TYPE_SPI_FLASH_LFS;
    transport->base.user_data = NULL;
    
    transport->lfs = NULL;
    transport->total_size = 0;
    transport->written_size = 0;
    transport->is_open = 0;
    transport->is_source = 0;
}
