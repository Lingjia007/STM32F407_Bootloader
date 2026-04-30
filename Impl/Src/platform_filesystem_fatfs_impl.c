#include "platform_filesystem_fatfs_impl.h"
#include <string.h>

_Static_assert(sizeof(platform_fs_file_t) >= sizeof(fs_fatfs_file_t), "platform_fs_file_t too small for fs_fatfs_file_t");
_Static_assert(sizeof(platform_fs_dir_t) >= sizeof(fs_fatfs_dir_t), "platform_fs_dir_t too small for fs_fatfs_dir_t");

static int16_t fatfs_open(platform_fs_base_t *fs, platform_fs_file_t *file, const char *path, platform_fs_mode_t mode)
{
    fs_fatfs_t *self = container_of(fs, fs_fatfs_t, base);
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    BYTE fatfs_mode = 0;

    switch (mode)
    {
    case FS_MODE_READ:
        fatfs_mode = FA_READ;
        break;
    case FS_MODE_WRITE:
        fatfs_mode = FA_WRITE;
        break;
    case FS_MODE_READ_WRITE:
        fatfs_mode = FA_READ | FA_WRITE;
        break;
    case FS_MODE_CREATE:
        fatfs_mode = FA_WRITE | FA_CREATE_NEW;
        break;
    case FS_MODE_CREATE_ALWAYS:
        fatfs_mode = FA_WRITE | FA_CREATE_ALWAYS;
        break;
    case FS_MODE_APPEND:
        fatfs_mode = FA_WRITE | FA_OPEN_APPEND;
        break;
    default:
        return (int16_t)FS_STATUS_PARAM;
    }

    FRESULT res = f_open(&f->file, path, fatfs_mode);
    if (res != FR_OK)
        return (int16_t)FS_STATUS_ERROR;

    return (int16_t)FS_STATUS_OK;
}

static int16_t fatfs_close(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    FRESULT res = f_close(&f->file);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int32_t fatfs_read(platform_fs_base_t *fs, platform_fs_file_t *file, uint8_t *buf, uint32_t size)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    UINT bytes_read = 0;
    FRESULT res = f_read(&f->file, buf, size, &bytes_read);
    if (res != FR_OK)
        return (int32_t)FS_STATUS_ERROR;
    return (int32_t)bytes_read;
}

static int32_t fatfs_write(platform_fs_base_t *fs, platform_fs_file_t *file, const uint8_t *data, uint32_t size)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    UINT bytes_written = 0;
    FRESULT res = f_write(&f->file, data, size, &bytes_written);
    if (res != FR_OK || bytes_written != size)
        return (int32_t)FS_STATUS_ERROR;
    return (int32_t)bytes_written;
}

static int32_t fatfs_seek(platform_fs_base_t *fs, platform_fs_file_t *file, int32_t offset, platform_fs_seek_t whence)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    FSIZE_t new_pos = 0;

    switch (whence)
    {
    case FS_SEEK_SET:
        new_pos = (FSIZE_t)offset;
        break;
    case FS_SEEK_CUR:
        new_pos = f_tell(&f->file) + (FSIZE_t)offset;
        break;
    case FS_SEEK_END:
        new_pos = f_size(&f->file) + (FSIZE_t)offset;
        break;
    default:
        return (int32_t)FS_STATUS_PARAM;
    }

    FRESULT res = f_lseek(&f->file, new_pos);
    if (res != FR_OK)
        return (int32_t)FS_STATUS_ERROR;

    return (int32_t)f_tell(&f->file);
}

static int32_t fatfs_tell(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    return (int32_t)f_tell(&f->file);
}

static int32_t fatfs_size(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    return (int32_t)f_size(&f->file);
}

static int16_t fatfs_sync(platform_fs_base_t *fs, platform_fs_file_t *file)
{
    fs_fatfs_file_t *f = (fs_fatfs_file_t *)file;
    FRESULT res = f_sync(&f->file);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fatfs_remove(platform_fs_base_t *fs, const char *path)
{
    FRESULT res = f_unlink(path);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fatfs_exists(platform_fs_base_t *fs, const char *path)
{
    FILINFO fno;
    FRESULT res = f_stat(path, &fno);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_NOT_FOUND;
}

static int16_t fatfs_mkdir(platform_fs_base_t *fs, const char *path)
{
    FRESULT res = f_mkdir(path);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fatfs_dir_open(platform_fs_base_t *fs, platform_fs_dir_t *dir, const char *path)
{
    fs_fatfs_dir_t *d = (fs_fatfs_dir_t *)dir;
    FRESULT res = f_opendir(&d->dir, path);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fatfs_dir_close(platform_fs_base_t *fs, platform_fs_dir_t *dir)
{
    fs_fatfs_dir_t *d = (fs_fatfs_dir_t *)dir;
    FRESULT res = f_closedir(&d->dir);
    return (res == FR_OK) ? (int16_t)FS_STATUS_OK : (int16_t)FS_STATUS_ERROR;
}

static int16_t fatfs_dir_read(platform_fs_base_t *fs, platform_fs_dir_t *dir, char *name, uint32_t *size, uint8_t *is_dir)
{
    fs_fatfs_dir_t *d = (fs_fatfs_dir_t *)dir;
    FILINFO fno;
    FRESULT res = f_readdir(&d->dir, &fno);

    if (res != FR_OK || fno.fname[0] == 0)
        return (int16_t)FS_STATUS_EOF;

    strcpy(name, fno.fname);
    *size = (uint32_t)fno.fsize;
    *is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;

    return (int16_t)FS_STATUS_OK;
}

static const platform_fs_ops_t fatfs_ops = {
    .open = fatfs_open,
    .close = fatfs_close,
    .read = fatfs_read,
    .write = fatfs_write,
    .seek = fatfs_seek,
    .tell = fatfs_tell,
    .size = fatfs_size,
    .sync = fatfs_sync,
    .remove = fatfs_remove,
    .exists = fatfs_exists,
    .mkdir = fatfs_mkdir,
};

static const platform_fs_dir_ops_t fatfs_dir_ops = {
    .open = fatfs_dir_open,
    .close = fatfs_dir_close,
    .read = fatfs_dir_read,
};

void platform_fs_fatfs_register(fs_fatfs_t *fs, FATFS *fatfs, const char *name)
{
    if (fs == NULL || fatfs == NULL)
        return;

    fs->fs = fatfs;
    fs->base.ops = &fatfs_ops;
    fs->base.dir_ops = &fatfs_dir_ops;
    fs->base.name = name;
    fs->base.user_data = NULL;
}
