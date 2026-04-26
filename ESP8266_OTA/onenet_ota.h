#ifndef ONENET_OTA_H
#define ONENET_OTA_H

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
} OtaPackageInfo;

void ONENET_OTA_SetTaskId(const char *tid);
void ONENET_OTA_ProcessUpgrade(void);
void ONENET_OTA_SetTargetType(uint8_t target_type);

int ota_http_request(const char *host, uint16_t port, const uint8_t *req, uint32_t req_len,
                     uint8_t *resp, uint32_t resp_cap, uint32_t timeout_ms);
int ota_http_status_code(const uint8_t *resp, uint32_t len);
int ota_http_body(const uint8_t *resp, uint32_t len, const uint8_t **body, uint32_t *body_len);
int ota_build_auth_header(const char *version, const char *res_raw, char *out, uint32_t out_cap);

#endif
