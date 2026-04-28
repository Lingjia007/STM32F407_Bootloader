#include "onenet_ota.h"
#include "transport.h"
#include "cJSON.h"
#include "md5.h"
#include "bootloader_core.h"
#include "onenet_http_source.h"
#include "fatfs.h"
#include "lfs.h"
#include "lfs_spi_flash_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "rtc.h"

typedef enum
{
    OTA_TARGET_INTERNAL_FLASH = 0,
    OTA_TARGET_SD_CARD_FATFS = 1,
    OTA_TARGET_SPI_FLASH_LFS = 2,
} ota_target_type_t;

static ota_target_type_t g_ota_target_type = OTA_TARGET_INTERNAL_FLASH;

static char g_ota_tid[48] = {0};
static char g_ota_auth[256];
static char g_ota_req[768];
static uint8_t g_ota_resp[3072];
static char g_ota_json[1024];
static char g_ota_body[128];
static char g_sign_string[192];
static char g_sign_res_enc[96];
static char g_sign_b64[64];
static char g_sign_enc[128];
static uint8_t g_sign_key_raw[96];
static uint32_t g_ota_unix_now_base = ONENET_AUTH_UNIX_NOW_BASE;

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[5];
} Sha1Ctx;

static void ota_rtc_set_unix_timestamp(uint32_t timestamp)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    timestamp += 28800UL;

    uint32_t days = timestamp / 86400UL;
    uint32_t secs = timestamp % 86400UL;

    sTime.Hours = secs / 3600;
    sTime.Minutes = (secs % 3600) / 60;
    sTime.Seconds = secs % 60;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;

    uint32_t year = 1970;
    while (1)
    {
        uint32_t days_in_year = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
        if (days < days_in_year)
            break;
        days -= days_in_year;
        year++;
    }

    static const uint8_t days_in_month[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}};
    uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
    uint8_t month = 0;
    while (month < 12 && days >= days_in_month[is_leap][month])
    {
        days -= days_in_month[is_leap][month];
        month++;
    }

    sDate.Year = year - 2000;
    sDate.Month = month + 1;
    sDate.Date = days + 1;

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

static uint32_t ota_rtc_get_unix_timestamp(void)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
        return 0;
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    if (sDate.Year < 25)
        return 0;

    uint16_t year = sDate.Year + 2000;
    uint8_t is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;

    static const uint16_t days_before_month[2][12] = {
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
        {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}};

    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; y++)
    {
        days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
    }
    days += days_before_month[is_leap][sDate.Month - 1];
    days += sDate.Date - 1;

    return days * 86400UL + sTime.Hours * 3600UL + sTime.Minutes * 60UL + sTime.Seconds - 28800UL;
}

static int ota_hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return (c - 'a') + 10;
    if (c >= 'A' && c <= 'F')
        return (c - 'A') + 10;
    return -1;
}

static int ota_md5_hex_to_bin(const char *hex, uint8_t out[16])
{
    if (hex == NULL || out == NULL)
        return 0;
    for (int i = 0; i < 16; i++)
    {
        int hi = ota_hex_to_nibble(hex[i * 2]);
        int lo = ota_hex_to_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static void ota_md5_bin_to_hex(const uint8_t in[16], char out[33])
{
    static const char *tbl = "0123456789abcdef";
    for (int i = 0; i < 16; i++)
    {
        out[i * 2] = tbl[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = tbl[in[i] & 0x0F];
    }
    out[32] = '\0';
}

static uint32_t sha1_rotl(uint32_t v, uint32_t n)
{
    return (v << n) | (v >> (32U - n));
}

static void sha1_transform(Sha1Ctx *ctx, const uint8_t data[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               (uint32_t)data[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++)
    {
        w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];

    for (int i = 0; i < 80; i++)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = sha1_rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rotl(b, 30);
        b = a;
        a = t;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(Sha1Ctx *ctx)
{
    memset(ctx, 0, sizeof(Sha1Ctx));
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

static void sha1_update(Sha1Ctx *ctx, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64U)
        {
            sha1_transform(ctx, ctx->data);
            ctx->bitlen += 512U;
            ctx->datalen = 0;
        }
    }
}

static void sha1_final(Sha1Ctx *ctx, uint8_t hash[20])
{
    uint32_t i = ctx->datalen;
    ctx->data[i++] = 0x80;
    if (i > 56U)
    {
        while (i < 64U)
            ctx->data[i++] = 0;
        sha1_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56U)
        ctx->data[i++] = 0;
    ctx->bitlen += (uint64_t)ctx->datalen * 8ULL;
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    sha1_transform(ctx, ctx->data);

    for (i = 0; i < 5; i++)
    {
        hash[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static void hmac_sha1(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len, uint8_t out[20])
{
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t tk[20];
    memset(k_ipad, 0, sizeof(k_ipad));
    memset(k_opad, 0, sizeof(k_opad));

    if (key_len > 64U)
    {
        Sha1Ctx tctx;
        sha1_init(&tctx);
        sha1_update(&tctx, key, key_len);
        sha1_final(&tctx, tk);
        key = tk;
        key_len = 20U;
    }

    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);
    for (uint32_t i = 0; i < 64U; i++)
    {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5C;
    }

    uint8_t inner[20];
    Sha1Ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, k_ipad, 64U);
    sha1_update(&ctx, msg, msg_len);
    sha1_final(&ctx, inner);

    sha1_init(&ctx);
    sha1_update(&ctx, k_opad, 64U);
    sha1_update(&ctx, inner, 20U);
    sha1_final(&ctx, out);
}

static int b64_index(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static int base64_decode_bytes(const char *in, uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    uint32_t len = (uint32_t)strlen(in);
    uint32_t i = 0;
    uint32_t o = 0;
    while (i < len)
    {
        int a = -1, b = -1, c = -2, d = -2;
        while (i < len && a < 0)
            a = b64_index(in[i++]);
        while (i < len && b < 0)
            b = b64_index(in[i++]);
        if (a < 0 || b < 0)
            break;
        while (i < len && c == -2)
        {
            if (in[i] == '=')
            {
                c = -1;
                i++;
                break;
            }
            c = b64_index(in[i++]);
        }
        while (i < len && d == -2)
        {
            if (in[i] == '=')
            {
                d = -1;
                i++;
                break;
            }
            d = b64_index(in[i++]);
        }
        if (o + 1U > out_cap)
            return 0;
        out[o++] = (uint8_t)((a << 2) | (b >> 4));
        if (c >= 0)
        {
            if (o + 1U > out_cap)
                return 0;
            out[o++] = (uint8_t)(((b & 0x0F) << 4) | (c >> 2));
            if (d >= 0)
            {
                if (o + 1U > out_cap)
                    return 0;
                out[o++] = (uint8_t)(((c & 0x03) << 6) | d);
            }
        }
        if (c < 0 || d < 0)
            break;
    }
    *out_len = o;
    return 1;
}

static int base64_encode_bytes(const uint8_t *in, uint32_t in_len, char *out, uint32_t out_cap)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t olen = ((in_len + 2U) / 3U) * 4U;
    if (out_cap <= olen)
        return 0;
    uint32_t i = 0;
    uint32_t o = 0;
    while ((i + 3U) <= in_len)
    {
        uint32_t a = in[i++];
        uint32_t b = in[i++];
        uint32_t c = in[i++];
        out[o++] = tbl[(a >> 2) & 0x3F];
        out[o++] = tbl[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
        out[o++] = tbl[((b & 0x0F) << 2) | ((c >> 6) & 0x03)];
        out[o++] = tbl[c & 0x3F];
    }
    uint32_t rem = in_len - i;
    if (rem == 1U)
    {
        uint32_t a = in[i];
        out[o++] = tbl[(a >> 2) & 0x3F];
        out[o++] = tbl[(a & 0x03) << 4];
        out[o++] = '=';
        out[o++] = '=';
    }
    else if (rem == 2U)
    {
        uint32_t a = in[i++];
        uint32_t b = in[i];
        out[o++] = tbl[(a >> 2) & 0x3F];
        out[o++] = tbl[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
        out[o++] = tbl[(b & 0x0F) << 2];
        out[o++] = '=';
    }
    out[o] = '\0';
    return 1;
}

static int url_encode_ascii(const char *in, char *out, uint32_t out_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t o = 0;
    for (uint32_t i = 0; in[i] != '\0'; i++)
    {
        uint8_t c = (uint8_t)in[i];
        uint8_t keep = ((c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') ||
                        c == '-' || c == '_' || c == '.' || c == '~');
        if (keep)
        {
            if (o + 1U >= out_cap)
                return 0;
            out[o++] = (char)c;
        }
        else
        {
            if (o + 3U >= out_cap)
                return 0;
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0x0F];
            out[o++] = hex[c & 0x0F];
        }
    }
    if (o >= out_cap)
        return 0;
    out[o] = '\0';
    return 1;
}

int ota_build_auth_header(const char *version, const char *res_raw, char *out, uint32_t out_cap)
{
    uint32_t key_len = 0;
    uint8_t digest[20];
    char method_enc[24];
    char version_enc[32];
    char et_text[24];
    char et_enc[32];
    uint32_t now_unix = ota_rtc_get_unix_timestamp();
    if (now_unix < 1000000000UL)
    {
        now_unix = g_ota_unix_now_base + (HAL_GetTick() / 1000UL);
    }
    uint32_t et = now_unix + ONENET_AUTH_ET_TTL_SEC;

    int sig_len = snprintf(g_sign_string, sizeof(g_sign_string), "%lu\n%s\n%s\n%s",
                           (unsigned long)et, ONENET_AUTH_METHOD, res_raw, version);
    if (sig_len <= 0 || sig_len >= (int)sizeof(g_sign_string))
        return 0;
    if (!base64_decode_bytes(ONENET_ACCESS_KEY_B64, g_sign_key_raw, sizeof(g_sign_key_raw), &key_len))
    {
        printf("OTA auth: b64 decode fail\r\n");
        return 0;
    }
    hmac_sha1(g_sign_key_raw, key_len, (const uint8_t *)g_sign_string, (uint32_t)sig_len, digest);
    if (!base64_encode_bytes(digest, sizeof(digest), g_sign_b64, sizeof(g_sign_b64)))
        return 0;
    if (!url_encode_ascii(g_sign_b64, g_sign_enc, sizeof(g_sign_enc)))
        return 0;
    if (!url_encode_ascii(res_raw, g_sign_res_enc, sizeof(g_sign_res_enc)))
        return 0;
    if (!url_encode_ascii(ONENET_AUTH_METHOD, method_enc, sizeof(method_enc)))
        return 0;
    if (!url_encode_ascii(version, version_enc, sizeof(version_enc)))
        return 0;
    snprintf(et_text, sizeof(et_text), "%lu", (unsigned long)et);
    if (!url_encode_ascii(et_text, et_enc, sizeof(et_enc)))
        return 0;

    int auth_len = snprintf(out, out_cap, "version=%s&res=%s&et=%s&method=%s&sign=%s",
                            version_enc, g_sign_res_enc, et_enc, method_enc, g_sign_enc);
    if (auth_len <= 0 || auth_len >= (int)out_cap)
        return 0;
    printf("OTA auth: now=%lu et=%lu ttl=%lu key_len=%lu\r\n",
           (unsigned long)now_unix, (unsigned long)et, (unsigned long)ONENET_AUTH_ET_TTL_SEC, (unsigned long)key_len);
    printf("OTA auth: sfs=%s\r\n", g_sign_string);
    printf("OTA auth: token=%s\r\n", out);
    return 1;
}

int ota_http_status_code(const uint8_t *resp, uint32_t len)
{
    if (resp == NULL || len < 12)
        return -1;
    for (uint32_t i = 0; i + 11 < len; i++)
    {
        if (resp[i] == 'H' && resp[i + 1] == 'T' && resp[i + 2] == 'T' && resp[i + 3] == 'P' && resp[i + 4] == '/')
        {
            for (uint32_t j = i; j < len; j++)
            {
                if (resp[j] == ' ')
                {
                    if (j + 3 < len)
                    {
                        return atoi((const char *)&resp[j + 1]);
                    }
                    return -1;
                }
                if (resp[j] == '\n')
                    break;
            }
            break;
        }
    }
    return -1;
}

int ota_http_body(const uint8_t *resp, uint32_t len, const uint8_t **body, uint32_t *body_len)
{
    if (resp == NULL || body == NULL || body_len == NULL)
        return 0;

    uint32_t header_end = 0;
    for (uint32_t i = 0; i + 3 < len; i++)
    {
        if (resp[i] == '\r' && resp[i + 1] == '\n' && resp[i + 2] == '\r' && resp[i + 3] == '\n')
        {
            header_end = i;
            break;
        }
    }

    if (header_end == 0)
        return 0;

    *body = &resp[header_end + 4];

    uint32_t content_length = 0;
    for (uint32_t i = 0; i < header_end; i++)
    {
        if (i + 15 < header_end &&
            resp[i] == 'C' && resp[i + 1] == 'o' && resp[i + 2] == 'n' && resp[i + 3] == 't' &&
            resp[i + 4] == 'e' && resp[i + 5] == 'n' && resp[i + 6] == 't' && resp[i + 7] == '-' &&
            resp[i + 8] == 'L' && resp[i + 9] == 'e' && resp[i + 10] == 'n' && resp[i + 11] == 'g' &&
            resp[i + 12] == 't' && resp[i + 13] == 'h')
        {
            uint32_t j = i + 14;
            while (j < header_end && (resp[j] == ' ' || resp[j] == ':'))
                j++;
            while (j < header_end && resp[j] >= '0' && resp[j] <= '9')
            {
                content_length = content_length * 10 + (resp[j] - '0');
                j++;
            }
            break;
        }
    }

    if (content_length > 0 && content_length <= (len - header_end - 4))
    {
        *body_len = content_length;
    }
    else
    {
        *body_len = len - (header_end + 4);
    }

    return 1;
}

int ota_http_request(const char *host, uint16_t port, const uint8_t *req, uint32_t req_len, uint8_t *resp, uint32_t resp_cap, uint32_t timeout_ms)
{
    if (host == NULL || req == NULL || resp == NULL || resp_cap == 0)
        return -1;

    int sock = transport_open((char *)host, port);
    if (sock < 0)
        return -1;

    if (transport_sendPacketBuffer(sock, (unsigned char *)req, req_len) != (int)req_len)
    {
        transport_close(sock);
        return -1;
    }

    uint32_t used = 0;
    uint32_t start = HAL_GetTick();
    uint32_t last_rx = start;
    int saw_http = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (used >= resp_cap)
            break;

        int n = transport_getdatanb(NULL, resp + used, (int)(resp_cap - used));

        if (n > 0)
        {
            used += (uint32_t)n;
            last_rx = HAL_GetTick();

            if (!saw_http && ota_http_status_code(resp, used) > 0)
                saw_http = 1;
            continue;
        }

        if (used > 0 && (HAL_GetTick() - last_rx) > 300U)
        {
            break;
        }

        HAL_Delay(10);
    }

    if (used > 0 && ota_http_status_code(resp, used) < 0)
    {
        uint32_t dump = used;
        if (dump > 120U)
            dump = 120U;
        printf("OTA http: no status host=%s used=%lu dump=", host, (unsigned long)used);
        for (uint32_t i = 0; i < dump; i++)
        {
            uint8_t c = resp[i];
            if (c >= 32U && c <= 126U)
                printf("%c", c);
            else
                printf(".");
        }
        printf("\r\n");
    }

    transport_close(sock);

    return (int)used;
}

static int ota_json_is_success(const uint8_t *body, uint32_t body_len)
{
    if (body == NULL || body_len == 0)
        return 0;

    uint32_t copy_len = body_len;
    if (copy_len >= sizeof(g_ota_json))
        copy_len = sizeof(g_ota_json) - 1U;
    memcpy(g_ota_json, body, copy_len);
    g_ota_json[copy_len] = '\0';

    cJSON *root = cJSON_Parse(g_ota_json);
    if (root == NULL)
        return 0;

    int ok = 0;
    cJSON *errno_node = cJSON_GetObjectItemCaseSensitive(root, "errno");
    if (cJSON_IsNumber(errno_node) && errno_node->valueint == 0)
        ok = 1;

    cJSON *code_node = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code_node) && code_node->valueint == 0)
        ok = 1;

    cJSON_Delete(root);
    return ok;
}

static int ota_json_copy_tid(cJSON *tid_node, char *out, uint32_t out_cap)
{
    if (out == NULL || out_cap == 0U)
        return 0;
    out[0] = '\0';

    if (cJSON_IsString(tid_node) && tid_node->valuestring != NULL)
    {
        strncpy(out, tid_node->valuestring, out_cap - 1U);
        out[out_cap - 1U] = '\0';
        return out[0] != '\0';
    }

    if (cJSON_IsNumber(tid_node))
    {
        int n = snprintf(out, out_cap, "%d", tid_node->valueint);
        return (n > 0 && n < (int)out_cap);
    }
    return 0;
}

static int ota_json_get_code(const uint8_t *body, uint32_t body_len, int *code_out)
{
    if (body == NULL || body_len == 0 || code_out == NULL)
        return 0;

    uint32_t copy_len = body_len;
    if (copy_len >= sizeof(g_ota_json))
        copy_len = sizeof(g_ota_json) - 1U;
    memcpy(g_ota_json, body, copy_len);
    g_ota_json[copy_len] = '\0';

    cJSON *root = cJSON_Parse(g_ota_json);
    if (root == NULL)
        return 0;

    int ok = 0;
    cJSON *code_node = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code_node))
    {
        *code_out = code_node->valueint;
        ok = 1;
    }

    cJSON_Delete(root);
    return ok;
}

static int ota_try_sync_unix_base_from_text(const char *text)
{
    if (text == NULL)
        return 0;

    const char *pos = strstr(text, "now=");
    if (pos == NULL)
        return 0;

    pos += 4;
    uint32_t now = 0;
    int has_digit = 0;

    while (*pos >= '0' && *pos <= '9')
    {
        has_digit = 1;
        now = now * 10U + (uint32_t)(*pos - '0');
        pos++;
    }

    if (!has_digit || now < 1000000000UL)
        return 0;

    uint32_t tick_s = HAL_GetTick() / 1000UL;

    if (now > tick_s)
    {
        g_ota_unix_now_base = now - tick_s;
        ota_rtc_set_unix_timestamp(now);
        printf("OTA auth: sync server now=%lu tick=%lu new_base=%lu (RTC updated)\r\n",
               (unsigned long)now, (unsigned long)tick_s, (unsigned long)g_ota_unix_now_base);
        return 1;
    }
    return 0;
}

void ONENET_OTA_SetTaskId(const char *tid)
{
    if (tid == NULL || tid[0] == '\0')
    {
        g_ota_tid[0] = '\0';
        return;
    }
    strncpy(g_ota_tid, tid, sizeof(g_ota_tid) - 1U);
    g_ota_tid[sizeof(g_ota_tid) - 1U] = '\0';
}

static int ota_report_version(void)
{
    for (int attempt = 0; attempt < 2; attempt++)
    {
        printf("OTA version: build auth\r\n");
        if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        {
            printf("OTA version: auth fail\r\n");
            return 0;
        }
        snprintf(g_ota_body, sizeof(g_ota_body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}", ONENET_CURRENT_VERSION, ONENET_CURRENT_VERSION);
        int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                               "POST /fuse-ota/%s/%s/version HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Authorization: %s\r\n"
                               "Content-Type: application/json\r\n"
                               "Connection: close\r\n"
                               "Content-Length: %u\r\n\r\n"
                               "%s",
                               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);
        if (req_len <= 0)
        {
            printf("OTA version: build req fail\r\n");
            return 0;
        }
        printf("OTA version: send req len=%d\r\n", req_len);
        int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 3000);
        if (n <= 0)
        {
            printf("OTA version: http fail n=%d\r\n", n);
            return 0;
        }
        int status = ota_http_status_code(g_ota_resp, (uint32_t)n);
        printf("OTA version: http status=%d resp_len=%d\r\n", status, n);
        if (status < 200 || status >= 300)
        {
            printf("OTA version: bad status\r\n");
            return 0;
        }
        const uint8_t *res_body = NULL;
        uint32_t body_len = 0;
        if (!ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
        {
            printf("OTA version: no body\r\n");
            return 0;
        }
        int ok = ota_json_is_success(res_body, body_len);
        printf("OTA version: json ok=%d body_len=%lu\r\n", ok, (unsigned long)body_len);
        if (ok)
            return 1;
        int code = 0;
        if (ota_json_get_code(res_body, body_len, &code) && code == 10403 && ota_try_sync_unix_base_from_text(g_ota_json) && attempt == 0)
        {
            printf("OTA version: retry after time sync\r\n");
            continue;
        }
        return 0;
    }
    return 0;
}

static int ota_check_upgrade(OtaPackageInfo *info)
{
    if (info == NULL)
        return 0;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        memset(info, 0, sizeof(OtaPackageInfo));
        printf("OTA check: build auth\r\n");

        if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        {
            printf("OTA check: auth fail\r\n");
            return 0;
        }

        int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                               "GET /fuse-ota/%s/%s/check?type=2&version=%s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Authorization: %s\r\n"
                               "Connection: close\r\n\r\n",
                               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_CURRENT_VERSION, ONENET_FUSE_HOST, g_ota_auth);
        if (req_len <= 0)
        {
            printf("OTA check: build req fail\r\n");
            return 0;
        }

        printf("OTA check: send req len=%d version=%s\r\n", req_len, ONENET_CURRENT_VERSION);

        int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 8000);
        if (n <= 0)
        {
            printf("OTA check: http fail n=%d\r\n", n);
            return 0;
        }
        int http_status = ota_http_status_code(g_ota_resp, (uint32_t)n);
        printf("OTA check: http status=%d resp_len=%d\r\n", http_status, n);
        if (http_status != 200)
        {
            printf("OTA check: bad status\r\n");
            return 0;
        }
        const uint8_t *res_body = NULL;
        uint32_t body_len = 0;
        if (!ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
        {
            printf("OTA check: no body\r\n");
            return 0;
        }
        uint32_t copy_len = body_len;
        if (copy_len >= sizeof(g_ota_json))
            copy_len = sizeof(g_ota_json) - 1U;
        memcpy(g_ota_json, res_body, copy_len);
        g_ota_json[copy_len] = '\0';
        cJSON *root = cJSON_Parse(g_ota_json);
        if (root == NULL)
        {
            printf("OTA check: parse json fail body=%s\r\n", g_ota_json);
            return 0;
        }
        int has_pkg = 0;
        cJSON *code_node = cJSON_GetObjectItemCaseSensitive(root, "code");
        if (cJSON_IsNumber(code_node))
        {
            printf("OTA check: code=%d\r\n", code_node->valueint);
            if (code_node->valueint == 10403 && ota_try_sync_unix_base_from_text(g_ota_json) && attempt == 0)
            {
                cJSON_Delete(root);
                printf("OTA check: retry after time sync\r\n");
                continue;
            }
        }
        if (cJSON_IsNumber(code_node) && code_node->valueint == 0)
        {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            cJSON *tid = cJSON_GetObjectItemCaseSensitive(data, "tid");
            cJSON *status = cJSON_GetObjectItemCaseSensitive(data, "status");
            cJSON *target = cJSON_GetObjectItemCaseSensitive(data, "target");
            cJSON *size = cJSON_GetObjectItemCaseSensitive(data, "size");
            cJSON *md5 = cJSON_GetObjectItemCaseSensitive(data, "md5");
            if (cJSON_IsNumber(status) &&
                (status->valueint == 1 || status->valueint == 2 || status->valueint == 3) &&
                cJSON_IsString(target) && cJSON_IsNumber(size) && cJSON_IsString(md5))
            {
                if ((uint32_t)size->valuedouble > 0U)
                {
                    strncpy(info->target, target->valuestring, sizeof(info->target) - 1U);
                    strncpy(info->md5_hex, md5->valuestring, sizeof(info->md5_hex) - 1U);
                    info->size = (uint32_t)size->valuedouble;
                    if (strlen(info->md5_hex) == 32U && ota_md5_hex_to_bin(info->md5_hex, info->md5_bin) && ota_json_copy_tid(tid, info->tid, sizeof(info->tid)))
                    {
                        has_pkg = 1;
                        printf("OTA check: has pkg tid=%s target=%s size=%lu status=%d\r\n",
                               info->tid, info->target, (unsigned long)info->size, status->valueint);
                    }
                }
            }
        }
        if (!has_pkg)
        {
            printf("OTA check: no valid pkg, body=%s\r\n", g_ota_json);
        }
        cJSON_Delete(root);
        return has_pkg;
    }
    return 0;
}

static int ota_check_task_ready(const OtaPackageInfo *info)
{
    if (info == NULL || info->tid[0] == '\0')
        return 0;
    printf("OTA task: check tid=%s\r\n", info->tid);
    if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
    {
        printf("OTA task: auth fail\r\n");
        return 0;
    }
    int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                           "GET /fuse-ota/%s/%s/%s/check?type=2&version=%s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Authorization: %s\r\n"
                           "Connection: close\r\n\r\n",
                           ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid, ONENET_CURRENT_VERSION, ONENET_FUSE_HOST, g_ota_auth);
    if (req_len <= 0)
    {
        printf("OTA task: build req fail\r\n");
        return 0;
    }
    int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 4000);
    if (n <= 0)
    {
        printf("OTA task: http fail n=%d\r\n", n);
        return 0;
    }
    int http_status = ota_http_status_code(g_ota_resp, (uint32_t)n);
    if (http_status != 200)
    {
        printf("OTA task: bad status=%d\r\n", http_status);
        return 0;
    }
    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (!ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
    {
        printf("OTA task: no body\r\n");
        return 0;
    }
    uint32_t copy_len = body_len;
    if (copy_len >= sizeof(g_ota_json))
        copy_len = sizeof(g_ota_json) - 1U;
    memcpy(g_ota_json, res_body, copy_len);
    g_ota_json[copy_len] = '\0';
    cJSON *root = cJSON_Parse(g_ota_json);
    if (root == NULL)
    {
        printf("OTA task: parse json fail body=%s\r\n", g_ota_json);
        return 0;
    }
    int ready = 0;
    cJSON *code_node = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (cJSON_IsNumber(code_node) && code_node->valueint == 0)
    {
        cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
        cJSON *status = cJSON_GetObjectItemCaseSensitive(data, "status");
        if (cJSON_IsNumber(status))
        {
            printf("OTA task: remote status=%d\r\n", status->valueint);
            if (status->valueint == 1 || status->valueint == 2 || status->valueint == 3)
                ready = 1;
        }
    }
    cJSON_Delete(root);
    printf("OTA task: ready=%d\r\n", ready);
    return ready;
}

static int ota_report_result(const OtaPackageInfo *info, int result_code)
{
    if (info == NULL || info->tid[0] == '\0')
        return 0;
    if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        return 0;
    snprintf(g_ota_body, sizeof(g_ota_body), "{\"step\":%d}", result_code);
    int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                           "POST /fuse-ota/%s/%s/%s/status HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Authorization: %s\r\n"
                           "Content-Type: application/json\r\n"
                           "Connection: close\r\n"
                           "Content-Length: %u\r\n\r\n"
                           "%s",
                           ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);
    if (req_len <= 0)
    {
        printf("OTA status: build req fail step=%d\r\n", result_code);
        return 0;
    }
    int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 3000);
    if (n <= 0)
    {
        printf("OTA status: http fail step=%d n=%d\r\n", result_code, n);
        return 0;
    }
    int status = ota_http_status_code(g_ota_resp, (uint32_t)n);
    if (status < 200 || status >= 300)
    {
        printf("OTA status: bad http step=%d status=%d\r\n", result_code, status);
        return 0;
    }
    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (!ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
    {
        printf("OTA status: no body step=%d\r\n", result_code);
        return 0;
    }
    int code = 0;
    if (!ota_json_get_code(res_body, body_len, &code))
    {
        int ok = ota_json_is_success(res_body, body_len);
        printf("OTA status: step=%d parse_code_fail ok=%d\r\n", result_code, ok);
        return ok;
    }
    printf("OTA status: step=%d code=%d\r\n", result_code, code);
    if (code == 0 || code == 20)
        return 1;
    return 0;
}

static void ota_progress_callback_wrapper(const OtaPackageInfo *info, int progress)
{
    ota_report_result(info, progress);
}

static int ota_download_and_verify(const OtaPackageInfo *info)
{
    if (info == NULL)
        return 0;

    const target_if_t *target_if = NULL;
    bootloader_err_t err = BOOTLOADER_OK;
    FATFS fatfs;
    lfs_t lfs;
    int fs_initialized = 0;

    printf("OTA download: start tid=%s size=%lu target=%s\r\n", info->tid, (unsigned long)info->size, info->target);
    ota_report_result(info, 0);

    switch (g_ota_target_type)
    {
    case OTA_TARGET_INTERNAL_FLASH:
        printf("OTA download: target = Internal Flash\r\n");
        bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;
        target_if = &internal_flash_target_if;
        break;

    case OTA_TARGET_SD_CARD_FATFS:
        printf("OTA download: target = SD Card (FATFS)\r\n");
        {
            FRESULT res = f_mount(&fatfs, "0:", 1);
            if (res != FR_OK)
            {
                printf("OTA download: SD card mount failed, res=%d\r\n", res);
                return 0;
            }
            bootloader_ctx.config.storage.fatfs = &fatfs;
            snprintf(bootloader_ctx.config.storage.fatfs_path,
                     sizeof(bootloader_ctx.config.storage.fatfs_path),
                     "0:ota_firmware_%s.bin", info->target);
            target_if = &fatfs_target_if;
            fs_initialized = 1;
        }
        break;

    case OTA_TARGET_SPI_FLASH_LFS:
        printf("OTA download: target = SPI Flash (LittleFS)\r\n");
        {
            int res = lfs_spi_flash_init();
            if (res != 0)
            {
                printf("OTA download: SPI Flash init failed, res=%d\r\n", res);
                return 0;
            }
            res = lfs_spi_flash_mount(&lfs);
            if (res != LFS_ERR_OK)
            {
                printf("OTA download: LittleFS mount failed, res=%d\r\n", res);
                return 0;
            }
            bootloader_ctx.config.storage.lfs = &lfs;
            snprintf(bootloader_ctx.config.storage.lfs_path,
                     sizeof(bootloader_ctx.config.storage.lfs_path),
                     "ota_firmware_%s.bin", info->target);
            target_if = &lfs_target_if;
            fs_initialized = 2;
        }
        break;

    default:
        printf("OTA download: invalid target type %d\r\n", g_ota_target_type);
        return 0;
    }

    onenet_http_source_init(info);
    onenet_http_source_set_progress_callback(ota_progress_callback_wrapper);

    err = bootloader_download(&onenet_http_source_if, target_if, NULL);

    onenet_http_source_deinit();

    if (err != BOOTLOADER_OK)
    {
        printf("OTA download: failed with error %d\r\n", err);
        if (fs_initialized == 1)
            f_mount(NULL, "0:", 0);
        else if (fs_initialized == 2)
            lfs_spi_flash_unmount(&lfs);
        return 0;
    }

    printf("OTA download: verifying data...\r\n");

    MD5_CTX verify_ctx;
    MD5Init(&verify_ctx);
    uint8_t verify_buf[512];
    uint32_t verify_offset = 0;

    if (g_ota_target_type == OTA_TARGET_INTERNAL_FLASH)
    {
        while (verify_offset < info->size)
        {
            uint32_t to_read = sizeof(verify_buf);
            if (verify_offset + to_read > info->size)
                to_read = info->size - verify_offset;

            memcpy(verify_buf, (const uint8_t *)(APPLICATION_ADDRESS + verify_offset), to_read);
            MD5Update(&verify_ctx, verify_buf, to_read);
            verify_offset += to_read;
        }
    }
    else if (g_ota_target_type == OTA_TARGET_SD_CARD_FATFS)
    {
        FIL fp;
        UINT bytes_read;
        FRESULT res = f_open(&fp, "0:ota_firmware.bin", FA_READ);
        if (res != FR_OK)
        {
            printf("OTA download: verify open failed, res=%d\r\n", res);
            f_mount(NULL, "0:", 0);
            return 0;
        }

        while (verify_offset < info->size)
        {
            uint32_t to_read = sizeof(verify_buf);
            if (verify_offset + to_read > info->size)
                to_read = info->size - verify_offset;

            res = f_read(&fp, verify_buf, to_read, &bytes_read);
            if (res != FR_OK || bytes_read != to_read)
            {
                printf("OTA download: verify read failed, res=%d\r\n", res);
                f_close(&fp);
                f_mount(NULL, "0:", 0);
                return 0;
            }

            MD5Update(&verify_ctx, verify_buf, to_read);
            verify_offset += to_read;
        }
        f_close(&fp);
        f_mount(NULL, "0:", 0);
    }
    else if (g_ota_target_type == OTA_TARGET_SPI_FLASH_LFS)
    {
        lfs_file_t file;
        lfs_ssize_t bytes_read;

        int res = lfs_file_open(&lfs, &file, "ota_firmware.bin", LFS_O_RDONLY);
        if (res != LFS_ERR_OK)
        {
            printf("OTA download: verify open failed, res=%d\r\n", res);
            lfs_spi_flash_unmount(&lfs);
            return 0;
        }

        while (verify_offset < info->size)
        {
            uint32_t to_read = sizeof(verify_buf);
            if (verify_offset + to_read > info->size)
                to_read = info->size - verify_offset;

            bytes_read = lfs_file_read(&lfs, &file, verify_buf, to_read);
            if (bytes_read != (lfs_ssize_t)to_read)
            {
                printf("OTA download: verify read failed, bytes=%ld\r\n", (long)bytes_read);
                lfs_file_close(&lfs, &file);
                lfs_spi_flash_unmount(&lfs);
                return 0;
            }

            MD5Update(&verify_ctx, verify_buf, to_read);
            verify_offset += to_read;
        }
        lfs_file_close(&lfs, &file);
        lfs_spi_flash_unmount(&lfs);
    }

    uint8_t verify_md5[16];
    MD5Final(&verify_ctx, verify_md5);

    char verify_hex[33];
    ota_md5_bin_to_hex(verify_md5, verify_hex);

    const char *target_name = "";
    switch (g_ota_target_type)
    {
    case OTA_TARGET_INTERNAL_FLASH:
        target_name = "Internal Flash";
        break;
    case OTA_TARGET_SD_CARD_FATFS:
        target_name = "SD Card (FATFS)";
        break;
    case OTA_TARGET_SPI_FLASH_LFS:
        target_name = "SPI Flash (LFS)";
        break;
    }

    printf("OTA download: %s md5=%s\r\n", target_name, verify_hex);
    printf("OTA download: remote_md5=%s\r\n", info->md5_hex);

    if (memcmp(verify_md5, info->md5_bin, 16) != 0)
    {
        char local_hex[33];
        ota_md5_bin_to_hex(verify_md5, local_hex);
        printf("OTA md5 mismatch local=%s remote=%s\r\n", local_hex, info->md5_hex);
        return 0;
    }

    printf("OTA download: verify ok, saved to %s\r\n", target_name);
    return 1;
}

void ONENET_OTA_ProcessUpgrade(void)
{
    OtaPackageInfo info;
    memset(&info, 0, sizeof(info));
    printf("OTA start\r\n");
    if (!ota_report_version())
    {
        printf("OTA stop: report version fail\r\n");
    }
    if (!ota_check_upgrade(&info))
    {
        printf("OTA no package\r\n");
        return;
    }
    if (!ota_check_task_ready(&info))
    {
        printf("OTA task not ready\r\n");
        return;
    }
    printf("OTA check success, package info:\r\n");
    printf("  tid: %s\r\n", info.tid);
    printf("  target: %s\r\n", info.target);
    printf("  size: %lu\r\n", (unsigned long)info.size);
    printf("  md5: %s\r\n", info.md5_hex);

    if (!ota_download_and_verify(&info))
    {
        ota_report_result(&info, 206);
        printf("OTA failed\r\n");
        return;
    }
    ota_report_result(&info, 206);
    printf("OTA download success\r\n");
}

void ONENET_OTA_SetTargetType(uint8_t target_type)
{
    if (target_type > OTA_TARGET_SPI_FLASH_LFS)
    {
        printf("OTA: invalid target type %d, using default (Internal Flash)\r\n", target_type);
        g_ota_target_type = OTA_TARGET_INTERNAL_FLASH;
        return;
    }

    g_ota_target_type = (ota_target_type_t)target_type;
    const char *name = "";
    switch (g_ota_target_type)
    {
    case OTA_TARGET_INTERNAL_FLASH:
        name = "Internal Flash";
        break;
    case OTA_TARGET_SD_CARD_FATFS:
        name = "SD Card (FATFS)";
        break;
    case OTA_TARGET_SPI_FLASH_LFS:
        name = "SPI Flash (LittleFS)";
        break;
    }
    printf("OTA: target set to %s\r\n", name);
}

int ONENET_SyncTime(void)
{
    printf("Time sync: building auth header...\r\n");

    if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
    {
        printf("Time sync: auth build failed\r\n");
        return 0;
    }

    snprintf(g_ota_body, sizeof(g_ota_body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}", ONENET_CURRENT_VERSION, ONENET_CURRENT_VERSION);

    int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                           "POST /fuse-ota/%s/%s/version HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Authorization: %s\r\n"
                           "Content-Type: application/json\r\n"
                           "Connection: close\r\n"
                           "Content-Length: %u\r\n\r\n"
                           "%s",
                           ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);

    if (req_len <= 0)
    {
        printf("Time sync: build request failed\r\n");
        return 0;
    }

    printf("Time sync: sending HTTP request...\r\n");
    int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 5000);

    if (n <= 0)
    {
        printf("Time sync: HTTP request failed, n=%d\r\n", n);
        return 0;
    }

    int status = ota_http_status_code(g_ota_resp, (uint32_t)n);
    printf("Time sync: HTTP status=%d\r\n", status);

    if (status < 200 || status >= 300)
    {
        printf("Time sync: HTTP error status=%d\r\n", status);
        return 0;
    }

    if (ota_try_sync_unix_base_from_text((const char *)g_ota_resp))
    {
        printf("Time sync: success!\r\n");
        return 1;
    }

    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
    {
        uint32_t copy_len = body_len;
        if (copy_len >= sizeof(g_ota_json))
            copy_len = sizeof(g_ota_json) - 1;
        memcpy(g_ota_json, res_body, copy_len);
        g_ota_json[copy_len] = '\0';

        printf("Time sync: response body (%lu bytes): %s\r\n", (unsigned long)body_len, g_ota_json);

        if (ota_try_sync_unix_base_from_text(g_ota_json))
        {
            printf("Time sync: success from body!\r\n");
            return 1;
        }

        cJSON *root = cJSON_Parse(g_ota_json);
        if (root != NULL)
        {
            cJSON *now_node = cJSON_GetObjectItemCaseSensitive(root, "now");
            if (cJSON_IsNumber(now_node))
            {
                uint32_t now = (uint32_t)now_node->valueint;
                uint32_t tick_s = HAL_GetTick() / 1000UL;
                if (now > tick_s)
                {
                    g_ota_unix_now_base = now - tick_s;
                    ota_rtc_set_unix_timestamp(now);
                    printf("Time sync: success from JSON now=%lu\r\n", (unsigned long)now);
                    cJSON_Delete(root);
                    return 1;
                }
            }

            cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            if (data != NULL)
            {
                cJSON *data_now = cJSON_GetObjectItemCaseSensitive(data, "now");
                if (cJSON_IsNumber(data_now))
                {
                    uint32_t now = (uint32_t)data_now->valueint;
                    uint32_t tick_s = HAL_GetTick() / 1000UL;
                    if (now > tick_s)
                    {
                        g_ota_unix_now_base = now - tick_s;
                        ota_rtc_set_unix_timestamp(now);
                        printf("Time sync: success from JSON data.now=%lu\r\n", (unsigned long)now);
                        cJSON_Delete(root);
                        return 1;
                    }
                }
            }

            cJSON_Delete(root);
        }
    }

    printf("Time sync: failed to extract time from response\r\n");
    return 0;
}
