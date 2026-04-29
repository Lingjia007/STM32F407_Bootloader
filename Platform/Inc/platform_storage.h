#ifndef PLATFORM_STORAGE_H
#define PLATFORM_STORAGE_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum {
    STORAGE_TYPE_INTERNAL_FLASH = 0,
    STORAGE_TYPE_SPI_FLASH_LFS,
    STORAGE_TYPE_SD_CARD_FATFS,
    STORAGE_TYPE_UNKNOWN
} platform_storage_type_t;

typedef enum {
    STORAGE_STATUS_OK = 0,
    STORAGE_STATUS_ERROR,
    STORAGE_STATUS_PARAM,
    STORAGE_STATUS_OPEN_SRC,
    STORAGE_STATUS_OPEN_DST,
    STORAGE_STATUS_READ,
    STORAGE_STATUS_WRITE,
    STORAGE_STATUS_CLOSE,
    STORAGE_STATUS_ERASE,
    STORAGE_STATUS_VERIFY,
} platform_storage_status_t;

typedef struct {
    int16_t (*open)(const void* ctx, const char* path, uint32_t* total_size);
    int16_t (*read)(const void* ctx, uint8_t* buf, uint32_t size, uint32_t* bytes_read);
    int16_t (*close)(const void* ctx);
} platform_storage_source_ops_t;

typedef struct {
    int16_t (*open)(const void* ctx, const char* path, uint32_t total_size);
    int16_t (*write)(const void* ctx, uint32_t offset, const uint8_t* data, uint32_t len);
    int16_t (*close)(const void* ctx);
} platform_storage_target_ops_t;

typedef struct {
    const platform_storage_source_ops_t* source_ops;
    const platform_storage_target_ops_t* target_ops;
    const char* name;
    platform_storage_type_t type;
    void* user_data;
} platform_storage_base_t;

#define STORAGE_SOURCE_OPEN(storage, path, total_size) \
    ((storage) && (storage)->source_ops && (storage)->source_ops->open ? \
     (storage)->source_ops->open((storage), (path), (total_size)) : (int16_t)STORAGE_STATUS_ERROR)

#define STORAGE_SOURCE_READ(storage, buf, size, bytes_read) \
    ((storage) && (storage)->source_ops && (storage)->source_ops->read ? \
     (storage)->source_ops->read((storage), (buf), (size), (bytes_read)) : (int16_t)STORAGE_STATUS_ERROR)

#define STORAGE_SOURCE_CLOSE(storage) \
    ((storage) && (storage)->source_ops && (storage)->source_ops->close ? \
     (storage)->source_ops->close((storage)) : (int16_t)STORAGE_STATUS_ERROR)

#define STORAGE_TARGET_OPEN(storage, path, total_size) \
    ((storage) && (storage)->target_ops && (storage)->target_ops->open ? \
     (storage)->target_ops->open((storage), (path), (total_size)) : (int16_t)STORAGE_STATUS_ERROR)

#define STORAGE_TARGET_WRITE(storage, offset, data, len) \
    ((storage) && (storage)->target_ops && (storage)->target_ops->write ? \
     (storage)->target_ops->write((storage), (offset), (data), (len)) : (int16_t)STORAGE_STATUS_ERROR)

#define STORAGE_TARGET_CLOSE(storage) \
    ((storage) && (storage)->target_ops && (storage)->target_ops->close ? \
     (storage)->target_ops->close((storage)) : (int16_t)STORAGE_STATUS_ERROR)

#endif
