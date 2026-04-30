#include "platform_filesystem_lfs_impl.h"
#include <string.h>

_Static_assert(sizeof(platform_fs_file_t) >= sizeof(fs_lfs_file_t), "platform_fs_file_t too small for fs_lfs_file_t");
_Static_assert(sizeof(platform_fs_dir_t) >= sizeof(fs_lfs_dir_t), "platform_fs_dir_t too small for fs_lfs_dir_t");

static int16_t fs_lfs_open(platform_fs_base_t *fs, platform_fs_file_t *file, const char *path, platform_fs_mode_t mode)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    int lfs_mode = 0;

    switch (mode)
    {
    case FS_MODE_READ:
        lfs_mode = LFS_O_RDONLY;
        break;
    case FS_MODE_WRITE:
        lfs_mode = LFS_O_WRONLY;
        break;
    case FS_MODE_READ_WRITE:
        lfs_mode = LFS_O_RDWR;
        break;
    case FS_MODE_CREATE:
        lfs_mode = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_EXCL;
        break;
    case FS_MODE_CREATE_ALWAYS:
        lfs_mode = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
        break;
    case FS_MODE_APPEND:
        lfs_mode = LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
        break;
    default:
        return (int16_t)FS_STATUS_PARAM;
    }

    int res = lfs_file_open(self->lfs, &f->file, path, lfs_mode);
    if (res != LFS_ERR_OK)
        return (int16_t)FS_STATUS_ERROR;

    return (int16_t)FS_STATUS_OK;
}

static int16_t fs_lfs_close(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    int res = lfs_file_close(self->lfs, &f->file);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int32_t fs_lfs_read(platform_fs_base_t *fs, platform_fs_file_t *file, uint8_t *buf, uint32_t size)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    lfs_ssize_t bytes_read = lfs_file_read(self->lfs, &f->file, buf, size);
    if (bytes_read < 0)
        return (int32_t)FS_STATUS_ERROR;
    return (int32_t)bytes_read;
}

static int32_t fs_lfs_write(platform_fs_base_t *fs, platform_fs_file_t *file, const uint8_t *data, uint32_t size)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    lfs_ssize_t bytes_written = lfs_file_write(self->lfs, &f->file, data, size);
    if (bytes_written < 0 || bytes_written != (lfs_ssize_t)size)
        return (int32_t)FS_STATUS_ERROR;
    return (int32_t)bytes_written;
}

static int32_t fs_lfs_seek(platform_fs_base_t *fs, platform_fs_file_t *file, int32_t offset, platform_fs_seek_t whence)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    int lfs_whence;

    switch (whence)
    {
    case FS_SEEK_SET:
        lfs_whence = LFS_SEEK_SET;
        break;
    case FS_SEEK_CUR:
        lfs_whence = LFS_SEEK_CUR;
        break;
    case FS_SEEK_END:
        lfs_whence = LFS_SEEK_END;
        break;
    default:
        return (int32_t)FS_STATUS_PARAM;
    }

    lfs_soff_t new_pos = lfs_file_seek(self->lfs, &f->file, offset, lfs_whence);
    if (new_pos < 0)
        return (int32_t)FS_STATUS_ERROR;

    return (int32_t)new_pos;
}

static int32_t fs_lfs_tell(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    return (int32_t)lfs_file_tell(self->lfs, &f->file);
}

static int32_t fs_lfs_size(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    return (int32_t)lfs_file_size(self->lfs, &f->file);
}

static int16_t fs_lfs_sync(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_file_t *f = (fs_lfs_file_t *)file;
    int res = lfs_file_sync(self->lfs, &f->file);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fs_lfs_remove(platform_fs_base_t *fs, const char *path)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    int res = lfs_remove(self->lfs, path);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fs_lfs_exists(platform_fs_base_t *fs, const char *path)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    struct lfs_info info;
    int res = lfs_stat(self->lfs, path, &info);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_NOT_FOUND;
}

static int16_t fs_lfs_mkdir(platform_fs_base_t *fs, const char *path)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    int res = lfs_mkdir(self->lfs, path);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fs_lfs_dir_open(platform_fs_base_t *fs, platform_fs_dir_t *dir, const char *path)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_dir_t *d = (fs_lfs_dir_t *)dir;
    int res = lfs_dir_open(self->lfs, &d->dir, path);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fs_lfs_dir_close(platform_fs_base_t *fs, platform_fs_dir_t *dir)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_dir_t *d = (fs_lfs_dir_t *)dir;
    int res = lfs_dir_close(self->lfs, &d->dir);
    return (res == LFS_ERR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fs_lfs_dir_read(platform_fs_base_t *fs, platform_fs_dir_t *dir, char *name, uint32_t *size, uint8_t *is_dir)
{
    fs_lfs_t *self = container_of(fs, fs_lfs_t, base);
    fs_lfs_dir_t *d = (fs_lfs_dir_t *)dir;
    struct lfs_info info;

    int res = lfs_dir_read(self->lfs, &d->dir, &info);
    if (res <= 0)
        return (int16_t)FS_STATUS_EOF;

    strcpy(name, info.name);
    *size = (uint32_t)info.size;
    *is_dir = (info.type == LFS_TYPE_DIR) ? 1 : 0;

    return (int16_t)FS_STATUS_OK;
}

static const platform_fs_ops_t lfs_ops = {
    .open = fs_lfs_open,
    .close = fs_lfs_close,
    .read = fs_lfs_read,
    .write = fs_lfs_write,
    .seek = fs_lfs_seek,
    .tell = fs_lfs_tell,
    .size = fs_lfs_size,
    .sync = fs_lfs_sync,
    .remove = fs_lfs_remove,
    .exists = fs_lfs_exists,
    .mkdir = fs_lfs_mkdir,
};

static const platform_fs_dir_ops_t lfs_dir_ops = {
    .open = fs_lfs_dir_open,
    .close = fs_lfs_dir_close,
    .read = fs_lfs_dir_read,
};

void platform_fs_lfs_register(fs_lfs_t *fs, lfs_t *lfs, const char *name)
{
    if (fs == NULL || lfs == NULL)
        return;

    fs->lfs = lfs;
    fs->base.ops = &lfs_ops;
    fs->base.dir_ops = &lfs_dir_ops;
    fs->base.name = name;
    fs->base.user_data = NULL;
}

fs_lfs_t g_fs_lfs = {
    .base = {
        .ops = &lfs_ops,
        .dir_ops = &lfs_dir_ops,
        .name = "lfs",
        .user_data = NULL,
    },
    .lfs = NULL,
};
