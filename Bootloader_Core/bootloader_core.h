#ifndef BOOTLOADER_CORE_H
#define BOOTLOADER_CORE_H

#include "stm32f4xx_hal.h"
#include "platform_transport.h"
#include "platform_internal_flash_stm32_impl.h"
#include "platform_fatfs_stm32_impl.h"
#include "platform_lfs_stm32_impl.h"
#include "firmware_package.h"
#include <stdint.h>

#define APPLICATION_ADDRESS (uint32_t)0x08020000
#define BOOTLOADER_PATH_MAX 128

#define UPDATE_FLAG_MAGIC 0x5A5A5A5A
#define JUMP_FLAG_MAGIC 0xA5A5A5A5

#if defined(__CC_ARM)
extern volatile uint32_t update_flag;
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
extern volatile uint32_t update_flag;
#elif defined(__GNUC__)
extern volatile uint32_t update_flag;
#elif defined(__ICCARM__)
extern volatile uint32_t update_flag;
#else
extern volatile uint32_t update_flag;
#endif

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
    BOOTLOADER_ERR_PKG_MAGIC = -11,
    BOOTLOADER_ERR_PKG_HMAC = -12,
    BOOTLOADER_ERR_PKG_SIG = -13,
    BOOTLOADER_ERR_PKG_DECRYPT = -14,
    BOOTLOADER_ERR_PKG_ROLLBACK = -15,
    BOOTLOADER_ERR_PKG_HW_COMPAT = -16,
    BOOTLOADER_ERR_PKG_HEADER_VER = -17,
    BOOTLOADER_ERR_PKG_UNSUPPORTED = -18,
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
    fw_pkg_verify_config_t pkg_verify;
    bootloader_err_t last_error;
} bootloader_ctx_t;

bootloader_err_t bootloader_download(const platform_transport_base_t *src_transport,
                                     const platform_transport_base_t *tgt_transport,
                                     const char *path);

bootloader_err_t bootloader_secure_download(const platform_transport_base_t *src_transport,
                                            const platform_transport_base_t *tgt_transport,
                                            const char *path,
                                            const fw_pkg_verify_config_t *config);

extern bootloader_ctx_t bootloader_ctx;

void jump_to_app(uint32_t app_address);
void execute_app_jump(void);

#endif
