#ifndef SERVICE_ONENET_OTA_H
#define SERVICE_ONENET_OTA_H

#include "platform_wifi.h"
#include "platform_rtc.h"
#include "platform_transport.h"
#include <stdint.h>

#define ONENET_FUSE_HOST "iot-api.heclouds.com"
#define ONENET_HTTP_PORT 80

#define ONENET_PRODUCT_ID "OmZ40ZapAD"
#define ONENET_DEVICE_NAME "STM32F407VGT6_SKYSTAR"
#define ONENET_CURRENT_VERSION "V1.0"

#define ONENET_ACCESS_KEY_B64 "0edef2e927d242a5bc8d154160cd70e1"
#define ONENET_AUTH_FUSE_VER "2022-05-01"
#define ONENET_AUTH_FUSE_RES_RAW "userid/510792"
#define ONENET_AUTH_METHOD "sha1"
#define ONENET_AUTH_UNIX_NOW_BASE 1774170000UL
#define ONENET_AUTH_ET_TTL_SEC 3600UL

#define ONENET_DOWNLOAD_CHUNK_SIZE 4096U
#define ONENET_PROGRESS_STEP_PCT 10U

typedef struct
{
    char tid[48];
    char target[24];
    char md5_hex[33];
    uint8_t md5_bin[16];
    uint32_t size;
} onenet_ota_package_info_t;

typedef void (*onenet_ota_progress_cb_t)(const onenet_ota_package_info_t *info, int progress);

typedef struct
{
    platform_wifi_base_t *wifi;
    platform_rtc_base_t *rtc;
    onenet_ota_progress_cb_t progress_cb;
    uint8_t target_type;
    uint32_t unix_now_base;
} onenet_ota_ctx_t;

typedef enum
{
    ONENET_OTA_TARGET_INTERNAL_FLASH = 0,
    ONENET_OTA_TARGET_SD_CARD_FATFS = 1,
    ONENET_OTA_TARGET_SPI_FLASH_LFS = 2,
} onenet_ota_target_t;

void onenet_ota_ctx_init(onenet_ota_ctx_t *ctx, platform_wifi_base_t *wifi, platform_rtc_base_t *rtc);
void onenet_ota_set_target(onenet_ota_ctx_t *ctx, onenet_ota_target_t target);
void onenet_ota_set_progress_callback(onenet_ota_ctx_t *ctx, onenet_ota_progress_cb_t cb);
int onenet_ota_sync_time(onenet_ota_ctx_t *ctx);
void onenet_ota_process_upgrade(onenet_ota_ctx_t *ctx);
void onenet_ota_set_task_id(const char *tid);

int onenet_ota_http_status_code(const uint8_t *resp, uint32_t len);
int onenet_ota_http_body(const uint8_t *resp, uint32_t len, const uint8_t **body, uint32_t *body_len);
int onenet_ota_build_auth(onenet_ota_ctx_t *ctx, const char *version, const char *res_raw, char *out, uint32_t out_cap);

#endif
