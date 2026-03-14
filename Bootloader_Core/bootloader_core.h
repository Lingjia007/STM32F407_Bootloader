#ifndef BOOTLOADER_CORE_H
#define BOOTLOADER_CORE_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define APPLICATION_ADDRESS (uint32_t)0x08008000

typedef void (*pFunction)(void);

// ==================== 错误码枚举 ====================
typedef enum
{
    BOOTLOADER_OK = 0,            // 成功
    BOOTLOADER_ERR_PARAM = -1,    // 参数错误
    BOOTLOADER_ERR_OPEN_SRC = -2, // 打开源失败
    BOOTLOADER_ERR_OPEN_DST = -3, // 打开目标失败
    BOOTLOADER_ERR_READ = -4,     // 读取失败
    BOOTLOADER_ERR_WRITE = -5,    // 写入失败
    BOOTLOADER_ERR_CLOSE = -6,    // 关闭失败
    BOOTLOADER_ERR_SIZE = -7,     // 大小超出限制
    BOOTLOADER_ERR_ERASE = -8,    // 擦除失败
    BOOTLOADER_ERR_VERIFY = -9,   // 校验失败
    BOOTLOADER_ERR_ABORT = -10,   // 操作被中止
    // 可根据需要扩展
} bootloader_err_t;

// ==================== 源/目标类型枚举 ====================

/**
 * @brief 源类型（数据来源）
 * @note 串口 Ymodem 不参与统一接口，作为独立分支处理
 */
typedef enum
{
    BOOT_SRC_SPI_FLASH,      // SPI Flash 上的 LittleFS 文件
    BOOT_SRC_SD_CARD,        // SD 卡上的 FATFS 文件
    BOOT_SRC_INTERNAL_FLASH, // 内部 Flash（直接读取）
} boot_src_t;

/**
 * @brief 目标类型（数据写入目的地）
 */
typedef enum
{
    TARGET_INTERNAL_FLASH, // 内部 Flash
    TARGET_SPI_FLASH_LFS,  // SPI Flash 上的 LittleFS 文件
    TARGET_SD_CARD_FATFS,  // SD 卡上的 FATFS 文件
} target_type_t;

// ==================== 源私有数据结构 ====================

/**
 * @brief SPI Flash (LittleFS) 源私有数据
 * @note lfs 实际类型为 struct lfs*，由外部传入
 */
typedef struct
{
    void *lfs;     // LittleFS 文件系统对象指针
    char path[64]; // 固件文件路径
} lfs_src_priv_t;

/**
 * @brief SD卡 (FATFS) 源私有数据
 * @note fs 实际类型为 FATFS*，由外部传入
 */
typedef struct
{
    void *fs;      // FATFS 对象指针
    char path[64]; // 固件文件路径
} fatfs_src_priv_t;

/**
 * @brief 内部 Flash 源私有数据（用于读取内部 Flash）
 */
typedef struct
{
    uint32_t start_addr; // 读取起始地址
    uint32_t size;       // 读取总大小
} internal_flash_src_priv_t;

// ==================== 目标私有数据结构 ====================

/**
 * @brief 内部 Flash 目标私有数据
 */
typedef struct
{
    uint32_t start_addr; // 写入起始地址
} internal_flash_target_priv_t;

/**
 * @brief SPI Flash (LittleFS) 目标私有数据（写入文件）
 */
typedef struct
{
    void *lfs;     // LittleFS 文件系统对象指针
    char path[64]; // 目标文件路径
} lfs_target_priv_t;

/**
 * @brief SD卡 (FATFS) 目标私有数据（写入文件）
 */
typedef struct
{
    void *fs;      // FATFS 对象指针
    char path[64]; // 目标文件路径
} fatfs_target_priv_t;

// ==================== 串口 Ymodem 独立参数 ====================

/**
 * @brief 串口 Ymodem 参数结构体（用于独立下载，不参与统一接口）
 */
typedef struct
{
    void *huart;             // 串口句柄，实际类型为 UART_HandleTypeDef*
    __IO uint32_t dest_addr; // 写入内部 Flash 的目标起始地址
} Ymodem_serial_params_t;

// ==================== 源/目标接口定义 ====================

/**
 * @brief 源接口：从何处读取数据
 */
typedef struct
{
    bootloader_err_t (*open)(void *ctx, const char *path, uint32_t *total_size);
    bootloader_err_t (*read)(void *ctx, uint8_t *buf, uint32_t size, uint32_t *bytes_read);
    bootloader_err_t (*close)(void *ctx);
} source_if_t;

/**
 * @brief 目标接口：写入到何处
 */
typedef struct
{
    bootloader_err_t (*open)(void *ctx, const char *path, uint32_t total_size);
    bootloader_err_t (*write)(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len);
    bootloader_err_t (*close)(void *ctx);
} target_if_t;

// ==================== Bootloader 主上下文 ====================

/**
 * @brief Bootloader 主上下文结构体
 */
typedef struct
{
    // ---------- 跳转相关 ----------
    __IO uint32_t app_jump_addr;      /**< 应用程序入口地址 */
    void (*jump_func)(uint32_t addr); /**< 跳转函数指针 */

    // ---------- 当前源 ----------
    boot_src_t src_type; /**< 当前使用的源类型 */
    union
    {
        internal_flash_src_priv_t internal; /**< 内部 Flash 源 */
        lfs_src_priv_t spi;                 /**< SPI Flash (LittleFS) 源 */
        fatfs_src_priv_t sd;                /**< SD卡 (FATFS) 源 */

    } src_priv; /**< 源私有数据联合体 */

    // ---------- 当前目标 ----------
    target_type_t target_type; /**< 当前使用的目标类型 */
    union
    {
        internal_flash_target_priv_t internal; /**< 内部 Flash 目标 */
        lfs_target_priv_t spi;                 /**< SPI Flash (LittleFS) 目标 */
        fatfs_target_priv_t sd;                /**< SD卡 (FATFS) 目标 */
    } target_priv;                             /**< 目标私有数据联合体 */

    // ---------- Ymodem下载参数 ----------
    Ymodem_serial_params_t serial_params; /**< Ymodem下载参数 */

    // ---------- 运行状态 ----------
    bootloader_err_t last_error; /**< 最后一次错误码 */

} bootloader_ctx_t;

// ==================== 统一下载函数声明 ====================

/**
 * @brief 从源读取数据并写入目标
 *
 * @param src_if   源接口函数表
 * @param src_ctx  源私有数据指针
 * @param tgt_if   目标接口函数表
 * @param tgt_ctx  目标私有数据指针
 * @param path     通用路径参数（可为 NULL，具体实现可能从私有数据中获取）
 * @return bootloader_err_t 成功返回 BOOTLOADER_OK，否则返回对应错误码
 */
bootloader_err_t bootloader_download(const source_if_t *src_if, void *src_ctx,
                                     const target_if_t *tgt_if, void *tgt_ctx,
                                     const char *path);

extern bootloader_ctx_t bootloader_ctx;

void jump_to_app(uint32_t app_address);

#endif
