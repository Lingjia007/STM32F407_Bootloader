#ifndef PLATFORM_INTERNAL_FLASH_H
#define PLATFORM_INTERNAL_FLASH_H

#include <stdint.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

typedef enum
{
    INTERNAL_FLASH_STATUS_OK = 0,
    INTERNAL_FLASH_STATUS_ERROR,
    INTERNAL_FLASH_STATUS_ERASE_ERROR,
    INTERNAL_FLASH_STATUS_WRITE_ERROR,
    INTERNAL_FLASH_STATUS_VERIFY_ERROR,
    INTERNAL_FLASH_STATUS_LOCKED,
    INTERNAL_FLASH_STATUS_INVALID_PARAM,
    INTERNAL_FLASH_STATUS_ADDRESS_ERROR
} platform_internal_flash_status_t;

typedef enum
{
    INTERNAL_FLASH_PROTECTION_NONE = 0,
    INTERNAL_FLASH_PROTECTION_PCROP = 0x01,
    INTERNAL_FLASH_PROTECTION_WRP = 0x02,
    INTERNAL_FLASH_PROTECTION_RDP = 0x04
} platform_internal_flash_protection_t;

typedef struct
{
    int16_t (*init)(void *ctx);
    int16_t (*deinit)(void *ctx);
    int16_t (*read)(void *ctx, uint32_t addr, uint8_t *buffer, uint32_t size);
    int16_t (*write)(void *ctx, uint32_t addr, const uint8_t *buffer, uint32_t size);
    int16_t (*erase)(void *ctx, uint32_t start_addr, uint32_t end_addr);
    int16_t (*erase_sector)(void *ctx, uint32_t sector);
    uint32_t (*get_sector)(void *ctx, uint32_t addr);
    uint32_t (*get_sector_size)(void *ctx, uint32_t sector);
    uint32_t (*get_total_size)(void *ctx);
    uint16_t (*get_protection_status)(void *ctx);
    int16_t (*set_protection)(void *ctx, uint32_t sectors, uint8_t enable);
} platform_internal_flash_ops_t;

typedef struct
{
    const platform_internal_flash_ops_t *ops;
    const char *name;
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t total_size;
    uint32_t sector_size;
    uint32_t page_size;
    void *user_data;
} platform_internal_flash_base_t;

#define INTERNAL_FLASH_INIT(flash) \
    ((flash) && (flash)->ops && (flash)->ops->init ? (flash)->ops->init((flash)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_DEINIT(flash) \
    ((flash) && (flash)->ops && (flash)->ops->deinit ? (flash)->ops->deinit((flash)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_READ(flash, addr, buffer, size) \
    ((flash) && (flash)->ops && (flash)->ops->read ? (flash)->ops->read((flash), (addr), (buffer), (size)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_WRITE(flash, addr, buffer, size) \
    ((flash) && (flash)->ops && (flash)->ops->write ? (flash)->ops->write((flash), (addr), (buffer), (size)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_ERASE(flash, start_addr, end_addr) \
    ((flash) && (flash)->ops && (flash)->ops->erase ? (flash)->ops->erase((flash), (start_addr), (end_addr)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_ERASE_SECTOR(flash, sector) \
    ((flash) && (flash)->ops && (flash)->ops->erase_sector ? (flash)->ops->erase_sector((flash), (sector)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_GET_SECTOR(flash, addr) \
    ((flash) && (flash)->ops && (flash)->ops->get_sector ? (flash)->ops->get_sector((flash), (addr)) : 0)

#define INTERNAL_FLASH_GET_SECTOR_SIZE(flash, sector) \
    ((flash) && (flash)->ops && (flash)->ops->get_sector_size ? (flash)->ops->get_sector_size((flash), (sector)) : 0)

#define INTERNAL_FLASH_GET_TOTAL_SIZE(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_total_size ? (flash)->ops->get_total_size((flash)) : 0)

#define INTERNAL_FLASH_GET_PROTECTION_STATUS(flash) \
    ((flash) && (flash)->ops && (flash)->ops->get_protection_status ? (flash)->ops->get_protection_status((flash)) : 0)

#define INTERNAL_FLASH_SET_PROTECTION(flash, sectors, enable) \
    ((flash) && (flash)->ops && (flash)->ops->set_protection ? (flash)->ops->set_protection((flash), (sectors), (enable)) : (int16_t)INTERNAL_FLASH_STATUS_ERROR)

#define INTERNAL_FLASH_INIT_BASE(flash_ptr, ops_ptr, flash_name, start, end) \
    do \
    { \
        (flash_ptr)->ops = (ops_ptr); \
        (flash_ptr)->name = (flash_name); \
        (flash_ptr)->start_addr = (start); \
        (flash_ptr)->end_addr = (end); \
        (flash_ptr)->total_size = (end) - (start) + 1; \
        (flash_ptr)->sector_size = 0; \
        (flash_ptr)->page_size = 4; \
        (flash_ptr)->user_data = NULL; \
    } while (0)

#endif
