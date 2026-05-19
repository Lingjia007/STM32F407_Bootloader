#ifndef PLATFORM_TRANSPORT_H
#define PLATFORM_TRANSPORT_H

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
    TRANSPORT_TYPE_INTERNAL_FLASH = 0,
    TRANSPORT_TYPE_SPI_FLASH_LFS,
    TRANSPORT_TYPE_SD_CARD_FATFS,
    TRANSPORT_TYPE_UART_YMODEM,
    TRANSPORT_TYPE_HTTP_OTA,
    TRANSPORT_TYPE_UNKNOWN
} platform_transport_type_t;

typedef enum {
    TRANSPORT_STATUS_OK = 0,
    TRANSPORT_STATUS_ERROR,
    TRANSPORT_STATUS_PARAM,
    TRANSPORT_STATUS_OPEN_SRC,
    TRANSPORT_STATUS_OPEN_DST,
    TRANSPORT_STATUS_READ,
    TRANSPORT_STATUS_WRITE,
    TRANSPORT_STATUS_CLOSE,
    TRANSPORT_STATUS_ERASE,
    TRANSPORT_STATUS_VERIFY,
} platform_transport_status_t;

typedef struct {
    int16_t (*open)(const void* ctx, const char* path, uint32_t* total_size);
    int16_t (*read)(const void* ctx, uint8_t* buf, uint32_t size, uint32_t* bytes_read);
    int16_t (*close)(const void* ctx);
} platform_transport_source_ops_t;

typedef struct {
    int16_t (*open)(const void* ctx, const char* path, uint32_t total_size);
    int16_t (*write)(const void* ctx, uint32_t offset, const uint8_t* data, uint32_t len);
    int16_t (*read)(const void* ctx, uint32_t offset, uint8_t* buf, uint32_t size, uint32_t* bytes_read);
    int16_t (*close)(const void* ctx);
} platform_transport_target_ops_t;

typedef struct {
    const platform_transport_source_ops_t* source_ops;
    const platform_transport_target_ops_t* target_ops;
    const char* name;
    platform_transport_type_t type;
    void* user_data;
} platform_transport_base_t;

#define TRANSPORT_SOURCE_OPEN(transport, path, total_size) \
    ((transport) && (transport)->source_ops && (transport)->source_ops->open ? \
     (transport)->source_ops->open((transport), (path), (total_size)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_SOURCE_READ(transport, buf, size, bytes_read) \
    ((transport) && (transport)->source_ops && (transport)->source_ops->read ? \
     (transport)->source_ops->read((transport), (buf), (size), (bytes_read)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_SOURCE_CLOSE(transport) \
    ((transport) && (transport)->source_ops && (transport)->source_ops->close ? \
     (transport)->source_ops->close((transport)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_TARGET_OPEN(transport, path, total_size) \
    ((transport) && (transport)->target_ops && (transport)->target_ops->open ? \
     (transport)->target_ops->open((transport), (path), (total_size)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_TARGET_WRITE(transport, offset, data, len) \
    ((transport) && (transport)->target_ops && (transport)->target_ops->write ? \
     (transport)->target_ops->write((transport), (offset), (data), (len)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_TARGET_READ(transport, offset, buf, size, bytes_read) \
    ((transport) && (transport)->target_ops && (transport)->target_ops->read ? \
     (transport)->target_ops->read((transport), (offset), (buf), (size), (bytes_read)) : (int16_t)TRANSPORT_STATUS_ERROR)

#define TRANSPORT_TARGET_CLOSE(transport) \
    ((transport) && (transport)->target_ops && (transport)->target_ops->close ? \
     (transport)->target_ops->close((transport)) : (int16_t)TRANSPORT_STATUS_ERROR)

#endif
