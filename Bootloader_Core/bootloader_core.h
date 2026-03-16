#ifndef BOOTLOADER_CORE_H
#define BOOTLOADER_CORE_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define APPLICATION_ADDRESS (uint32_t)0x08020000
#define BOOTLOADER_PATH_MAX 64

typedef void (*pFunction)(void);

typedef enum
{
    BOOTLOADER_OK = 0,
    BOOTLOADER_ERR_PARAM = -1,
    BOOTLOADER_ERR_OPEN_SRC = -2,
    BOOTLOADER_ERR_OPEN_DST = -3,
    BOOTLOADER_ERR_READ = -4,
    BOOTLOADER_ERR_WRITE = -5,
    BOOTLOADER_ERR_CLOSE = -6,
    BOOTLOADER_ERR_SIZE = -7,
    BOOTLOADER_ERR_ERASE = -8,
    BOOTLOADER_ERR_VERIFY = -9,
    BOOTLOADER_ERR_ABORT = -10,
} bootloader_err_t;

typedef enum
{
    BOOTLOADER_SRC_SPI_FLASH,
    BOOTLOADER_SRC_SD_CARD,
    BOOTLOADER_SRC_INTERNAL_FLASH,
} bootloader_src_t;

typedef enum
{
    BOOTLOADER_TARGET_INTERNAL_FLASH,
    BOOTLOADER_TARGET_SPI_FLASH_LFS,
    BOOTLOADER_TARGET_SD_CARD_FATFS,
} bootloader_target_t;

typedef struct
{
    bootloader_err_t (*open)(const char *path, uint32_t *total_size);
    bootloader_err_t (*read)(uint8_t *buf, uint32_t size, uint32_t *bytes_read);
    bootloader_err_t (*close)(void);
} source_if_t;

typedef struct
{
    bootloader_err_t (*open)(const char *path, uint32_t total_size);
    bootloader_err_t (*write)(uint32_t offset, const uint8_t *data, uint32_t len);
    bootloader_err_t (*close)(void);
} target_if_t;

typedef struct
{
    void *lfs;
    void *fatfs;
    char lfs_path[BOOTLOADER_PATH_MAX];
    char fatfs_path[BOOTLOADER_PATH_MAX];
    uint32_t internal_flash_addr;
} bootloader_storage_config_t;

typedef struct
{
    void *huart;
    __IO uint32_t dest_addr;
} ymodem_serial_params_t;

typedef struct
{
    __IO uint32_t app_jump_addr;
    void (*jump_func)(uint32_t addr);
} bootloader_jump_config_t;

typedef struct
{
    bootloader_storage_config_t storage;
    ymodem_serial_params_t ymodem;
    bootloader_jump_config_t jump;
} bootloader_config_t;

typedef struct
{
    bootloader_config_t config;
    bootloader_src_t src_type;
    bootloader_target_t target_type;
    bootloader_err_t last_error;
} bootloader_ctx_t;

bootloader_err_t bootloader_download(const source_if_t *src_if,
                                     const target_if_t *tgt_if,
                                     const char *path);

extern bootloader_ctx_t bootloader_ctx;

void jump_to_app(uint32_t app_address);

extern const source_if_t fatfs_source_if;
extern const source_if_t lfs_source_if;

extern const target_if_t fatfs_target_if;
extern const target_if_t internal_flash_target_if;
extern const target_if_t lfs_target_if;

#endif
