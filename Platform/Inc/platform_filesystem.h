#ifndef PLATFORM_FILESYSTEM_H
#define PLATFORM_FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum
{
    FS_STATUS_OK = 0,
    FS_STATUS_ERROR,
    FS_STATUS_PARAM,
    FS_STATUS_NOT_FOUND,
    FS_STATUS_NO_SPACE,
    FS_STATUS_TIMEOUT,
    FS_STATUS_EOF,
} platform_fs_status_t;

typedef enum
{
    FS_SEEK_SET = 0,
    FS_SEEK_CUR,
    FS_SEEK_END,
} platform_fs_seek_t;

typedef enum
{
    FS_MODE_READ = 0,
    FS_MODE_WRITE,
    FS_MODE_READ_WRITE,
    FS_MODE_CREATE,
    FS_MODE_CREATE_ALWAYS,
    FS_MODE_APPEND,
} platform_fs_mode_t;

typedef struct platform_fs_base_s platform_fs_base_t;

typedef struct
{
    uint8_t data[600];
} platform_fs_file_t;

typedef struct
{
    uint8_t data[80];
} platform_fs_dir_t;

typedef struct
{
    int16_t (*open)(platform_fs_base_t *fs, platform_fs_file_t *file, const char *path, platform_fs_mode_t mode);
    int16_t (*close)(platform_fs_base_t *fs, platform_fs_file_t *file);
    int32_t (*read)(platform_fs_base_t *fs, platform_fs_file_t *file, uint8_t *buf, uint32_t size);
    int32_t (*write)(platform_fs_base_t *fs, platform_fs_file_t *file, const uint8_t *data, uint32_t size);
    int32_t (*seek)(platform_fs_base_t *fs, platform_fs_file_t *file, int32_t offset, platform_fs_seek_t whence);
    int32_t (*tell)(platform_fs_base_t *fs, platform_fs_file_t *file);
    int32_t (*size)(platform_fs_base_t *fs, platform_fs_file_t *file);
    int16_t (*sync)(platform_fs_base_t *fs, platform_fs_file_t *file);
    int16_t (*remove)(platform_fs_base_t *fs, const char *path);
    int16_t (*exists)(platform_fs_base_t *fs, const char *path);
    int16_t (*mkdir)(platform_fs_base_t *fs, const char *path);
} platform_fs_ops_t;

typedef struct
{
    int16_t (*open)(platform_fs_base_t *fs, platform_fs_dir_t *dir, const char *path);
    int16_t (*close)(platform_fs_base_t *fs, platform_fs_dir_t *dir);
    int16_t (*read)(platform_fs_base_t *fs, platform_fs_dir_t *dir, char *name, uint32_t *size, uint8_t *is_dir);
} platform_fs_dir_ops_t;

struct platform_fs_base_s
{
    const platform_fs_ops_t *ops;
    const platform_fs_dir_ops_t *dir_ops;
    const char *name;
    void *user_data;
};

#define FS_OPEN(fs, file, path, mode) \
    ((fs) && (fs)->ops && (fs)->ops->open ? (fs)->ops->open((fs), (file), (path), (mode)) : (int16_t)FS_STATUS_ERROR)

#define FS_CLOSE(fs, file) \
    ((fs) && (fs)->ops && (fs)->ops->close ? (fs)->ops->close((fs), (file)) : (int16_t)FS_STATUS_ERROR)

#define FS_READ(fs, file, buf, size) \
    ((fs) && (fs)->ops && (fs)->ops->read ? (fs)->ops->read((fs), (file), (buf), (size)) : (int32_t)FS_STATUS_ERROR)

#define FS_WRITE(fs, file, data, size) \
    ((fs) && (fs)->ops && (fs)->ops->write ? (fs)->ops->write((fs), (file), (data), (size)) : (int32_t)FS_STATUS_ERROR)

#define FS_SEEK(fs, file, offset, whence) \
    ((fs) && (fs)->ops && (fs)->ops->seek ? (fs)->ops->seek((fs), (file), (offset), (whence)) : (int32_t)FS_STATUS_ERROR)

#define FS_TELL(fs, file) \
    ((fs) && (fs)->ops && (fs)->ops->tell ? (fs)->ops->tell((fs), (file)) : (int32_t)FS_STATUS_ERROR)

#define FS_SIZE(fs, file) \
    ((fs) && (fs)->ops && (fs)->ops->size ? (fs)->ops->size((fs), (file)) : (int32_t)FS_STATUS_ERROR)

#define FS_SYNC(fs, file) \
    ((fs) && (fs)->ops && (fs)->ops->sync ? (fs)->ops->sync((fs), (file)) : (int16_t)FS_STATUS_ERROR)

#define FS_REMOVE(fs, path) \
    ((fs) && (fs)->ops && (fs)->ops->remove ? (fs)->ops->remove((fs), (path)) : (int16_t)FS_STATUS_ERROR)

#define FS_EXISTS(fs, path) \
    ((fs) && (fs)->ops && (fs)->ops->exists ? (fs)->ops->exists((fs), (path)) : (int16_t)FS_STATUS_ERROR)

#define FS_MKDIR(fs, path) \
    ((fs) && (fs)->ops && (fs)->ops->mkdir ? (fs)->ops->mkdir((fs), (path)) : (int16_t)FS_STATUS_ERROR)

#define FS_DIR_OPEN(fs, dir, path) \
    ((fs) && (fs)->dir_ops && (fs)->dir_ops->open ? (fs)->dir_ops->open((fs), (dir), (path)) : (int16_t)FS_STATUS_ERROR)

#define FS_DIR_CLOSE(fs, dir) \
    ((fs) && (fs)->dir_ops && (fs)->dir_ops->close ? (fs)->dir_ops->close((fs), (dir)) : (int16_t)FS_STATUS_ERROR)

#define FS_DIR_READ(fs, dir, name, size, is_dir) \
    ((fs) && (fs)->dir_ops && (fs)->dir_ops->read ? (fs)->dir_ops->read((fs), (dir), (name), (size), (is_dir)) : (int16_t)FS_STATUS_ERROR)

#endif
