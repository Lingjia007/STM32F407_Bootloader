#include "onenet_http_source.h"
#include "onenet_ota.h"
#include "transport.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HTTP_RESP_BUF_SIZE (ONENET_DOWNLOAD_CHUNK_SIZE + 512)

static const OtaPackageInfo *g_ota_info = NULL;
static uint32_t g_http_offset = 0;
static uint8_t g_http_is_open = 0;
static uint8_t g_http_resp[HTTP_RESP_BUF_SIZE];
static char g_http_req[768];
static char g_http_auth[256];
static ota_progress_callback_t g_progress_callback = NULL;
static uint32_t g_last_reported_progress = 0;

static int http_source_build_auth(void)
{
    if (g_ota_info == NULL)
        return 0;

    extern int ota_build_auth_header(const char *version, const char *res_raw, char *out, uint32_t out_cap);
    return ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_http_auth, sizeof(g_http_auth));
}

static int http_source_download_chunk(uint32_t offset, uint32_t size, uint8_t *buf, uint32_t buf_cap, uint32_t *out_len)
{
    if (g_ota_info == NULL || buf == NULL || out_len == NULL)
    {
        printf("HTTP Source: chunk param error\r\n");
        return 0;
    }

    uint32_t end = offset + size - 1U;
    if (end >= g_ota_info->size)
        end = g_ota_info->size - 1U;

    int req_len = snprintf(g_http_req, sizeof(g_http_req),
                           "GET /fuse-ota/%s/%s/%s/download HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Range: %lu-%lu\r\n"
                           "Authorization: %s\r\n"
                           "Connection: close\r\n\r\n",
                           ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, g_ota_info->tid,
                           ONENET_FUSE_HOST,
                           (unsigned long)offset, (unsigned long)end,
                           g_http_auth);

    if (req_len <= 0 || req_len >= (int)sizeof(g_http_req))
    {
        printf("HTTP Source: build request failed\r\n");
        return 0;
    }

    printf("HTTP Source: sending request offset=%lu-%lu\r\n", (unsigned long)offset, (unsigned long)end);

    int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_http_req, (uint32_t)req_len, g_http_resp, HTTP_RESP_BUF_SIZE, 12000);
    if (n <= 0)
    {
        printf("HTTP Source: http request failed n=%d\r\n", n);
        return 0;
    }

    int status = ota_http_status_code(g_http_resp, (uint32_t)n);
    if (!(status == 206 || status == 200))
    {
        printf("HTTP Source: bad status=%d\r\n", status);
        return 0;
    }

    const uint8_t *body = NULL;
    uint32_t body_len = 0;
    if (!ota_http_body(g_http_resp, (uint32_t)n, &body, &body_len))
    {
        printf("HTTP Source: get body failed\r\n");
        return 0;
    }

    uint32_t copy_len = body_len;
    if (copy_len > buf_cap)
        copy_len = buf_cap;

    memcpy(buf, body, copy_len);
    *out_len = copy_len;

    printf("HTTP Source: chunk success body_len=%lu copy_len=%lu\r\n", (unsigned long)body_len, (unsigned long)copy_len);
    return 1;
}

static int16_t onenet_http_src_open(const void *ctx, const char *path, uint32_t *total_size)
{
    (void)ctx;
    (void)path;

    if (g_ota_info == NULL || total_size == NULL)
        return TRANSPORT_STATUS_PARAM;

    if (!http_source_build_auth())
        return TRANSPORT_STATUS_OPEN_SRC;

    g_http_offset = 0;
    g_http_is_open = 1;
    *total_size = g_ota_info->size;

    printf("HTTP Source: open success, total_size=%lu\r\n", (unsigned long)g_ota_info->size);
    return TRANSPORT_STATUS_OK;
}

static int16_t onenet_http_src_read(const void *ctx, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    (void)ctx;

    if (buf == NULL || bytes_read == NULL)
        return TRANSPORT_STATUS_PARAM;

    if (!g_http_is_open)
    {
        printf("HTTP Source: read failed - not open\r\n");
        return TRANSPORT_STATUS_READ;
    }

    if (g_http_offset >= g_ota_info->size)
    {
        *bytes_read = 0;
        printf("HTTP Source: read complete, offset=%lu\r\n", (unsigned long)g_http_offset);
        return TRANSPORT_STATUS_OK;
    }

    uint32_t chunk_size = ONENET_DOWNLOAD_CHUNK_SIZE;
    if (chunk_size > size)
        chunk_size = size;
    if (g_http_offset + chunk_size > g_ota_info->size)
        chunk_size = g_ota_info->size - g_http_offset;

    if (chunk_size > 4 && (g_http_offset + chunk_size) < g_ota_info->size)
    {
        chunk_size = (chunk_size / 4) * 4;
    }

    printf("HTTP Source: reading offset=%lu size=%lu\r\n", (unsigned long)g_http_offset, (unsigned long)chunk_size);

    uint32_t actual_read = 0;
    for (int retry = 0; retry < 3; retry++)
    {
        if (retry > 0)
        {
            printf("HTTP Source: retry %d offset=%lu\r\n", retry, (unsigned long)g_http_offset);
            HAL_Delay(1000);
        }

        if (http_source_download_chunk(g_http_offset, chunk_size, buf, size, &actual_read))
        {
            break;
        }
        actual_read = 0;
    }

    if (actual_read == 0)
    {
        printf("HTTP Source: read failed after retries\r\n");
        return TRANSPORT_STATUS_READ;
    }

    if (actual_read > 4 && (g_http_offset + actual_read) < g_ota_info->size)
    {
        actual_read = (actual_read / 4) * 4;
    }

    g_http_offset += actual_read;
    *bytes_read = actual_read;

    printf("HTTP Source: read %lu bytes, total=%lu/%lu\r\n", (unsigned long)actual_read, (unsigned long)g_http_offset, (unsigned long)g_ota_info->size);

    if (g_progress_callback != NULL && g_ota_info != NULL)
    {
        uint32_t progress = (g_http_offset * 100U) / g_ota_info->size;
        if (progress > 100U)
            progress = 100U;

        if (progress == 100U || progress >= (g_last_reported_progress + ONENET_PROGRESS_STEP_PCT))
        {
            g_progress_callback(g_ota_info, (int)progress);
            g_last_reported_progress = progress;
        }
    }

    return TRANSPORT_STATUS_OK;
}

static int16_t onenet_http_src_close(const void *ctx)
{
    (void)ctx;
    g_http_is_open = 0;
    g_http_offset = 0;
    printf("HTTP Source: closed\r\n");
    return TRANSPORT_STATUS_OK;
}

static const platform_transport_source_ops_t onenet_http_source_ops = {
    .open = onenet_http_src_open,
    .read = onenet_http_src_read,
    .close = onenet_http_src_close,
};

platform_transport_base_t g_onenet_http_source = {
    .source_ops = &onenet_http_source_ops,
    .target_ops = NULL,
    .name = "onenet_http",
    .type = TRANSPORT_TYPE_HTTP_OTA,
    .user_data = NULL,
};

void onenet_http_source_init(const OtaPackageInfo *info)
{
    g_ota_info = info;
    g_http_offset = 0;
    g_http_is_open = 0;
    g_last_reported_progress = 0;
    printf("HTTP Source: initialized with package info\r\n");
}

void onenet_http_source_deinit(void)
{
    g_ota_info = NULL;
    g_http_offset = 0;
    g_http_is_open = 0;
    g_last_reported_progress = 0;
    g_progress_callback = NULL;
}

void onenet_http_source_set_progress_callback(ota_progress_callback_t callback)
{
    g_progress_callback = callback;
    g_last_reported_progress = 0;
    printf("HTTP Source: progress callback set\r\n");
}
