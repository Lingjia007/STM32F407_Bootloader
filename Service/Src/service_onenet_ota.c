#include "service_onenet_ota.h"
#include "service_wifi_transport.h"
#include "platform_config.h"
#include "esp8266_ota_config.h"
#include "bootloader_core.h"
#include "cJSON.h"
#include "md5.h"
#include "fatfs.h"
#include "lfs.h"
#include "service_lfs_spi_flash_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[5];
} Sha1Ctx;

static uint8_t is_leap_year(uint16_t year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 1 : 0;
}

static void sha1_transform(Sha1Ctx *ctx, const uint8_t data[64]);
static void sha1_init(Sha1Ctx *ctx);
static void sha1_update(Sha1Ctx *ctx, const uint8_t *data, uint32_t len);
static void sha1_final(Sha1Ctx *ctx, uint8_t hash[20]);
static void hmac_sha1(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len, uint8_t out[20]);
static int b64_index(char c);
static int base64_decode_bytes(const char *in, uint8_t *out, uint32_t out_cap, uint32_t *out_len);
static int base64_encode_bytes(const uint8_t *in, uint32_t in_len, char *out, uint32_t out_cap);
static int url_encode_ascii(const char *in, char *out, uint32_t out_cap);

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

static uint32_t ota_rtc_get_unix_timestamp(onenet_ota_ctx_t *ctx)
{
    if (ctx == NULL || ctx->rtc == NULL)
        return 0;
    uint32_t ts = 0;
    if (RTC_GET_TIMESTAMP(ctx->rtc, &ts) != RTC_STATUS_OK)
        return 0;
    return ts;
}

static void ota_rtc_set_unix_timestamp(onenet_ota_ctx_t *ctx, uint32_t timestamp)
{
    if (ctx == NULL || ctx->rtc == NULL)
        return;
    RTC_SET_TIMESTAMP(ctx->rtc, timestamp);
}

static uint32_t sha1_rotl(uint32_t v, uint32_t n) { return (v << n) | (v >> (32U - n)); }

static void sha1_transform(Sha1Ctx *ctx, const uint8_t data[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) | ((uint32_t)data[i * 4 + 2] << 8) | (uint32_t)data[i * 4 + 3];
    for (int i = 16; i < 80; i++)
        w[i] = sha1_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3], e = ctx->state[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
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
    uint8_t k_ipad[64], k_opad[64], tk[20];
    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
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
    uint32_t len = (uint32_t)strlen(in), i = 0, o = 0;
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
    uint32_t i = 0, o = 0;
    while ((i + 3U) <= in_len)
    {
        uint32_t a = in[i++], b = in[i++], c = in[i++];
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
        uint32_t a = in[i++], b = in[i];
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
        uint8_t keep = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~');
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

int onenet_ota_build_auth(onenet_ota_ctx_t *ctx, const char *version, const char *res_raw, char *out, uint32_t out_cap)
{
    uint32_t key_len = 0;
    uint8_t digest[20];
    char method_enc[24], version_enc[32], et_text[24], et_enc[32];
    uint32_t now_unix = ota_rtc_get_unix_timestamp(ctx);
    if (now_unix < 1000000000UL)
        now_unix = ctx->unix_now_base + (PLATFORM_GET_TICK(g_tick) / 1000UL);
    uint32_t et = now_unix + ONENET_AUTH_ET_TTL_SEC;
    int sig_len = snprintf(g_sign_string, sizeof(g_sign_string), "%lu\n%s\n%s\n%s", (unsigned long)et, ONENET_AUTH_METHOD, res_raw, version);
    if (sig_len <= 0 || sig_len >= (int)sizeof(g_sign_string))
        return 0;
    if (!base64_decode_bytes(ONENET_ACCESS_KEY_B64, g_sign_key_raw, sizeof(g_sign_key_raw), &key_len))
        return 0;
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
    int auth_len = snprintf(out, out_cap, "version=%s&res=%s&et=%s&method=%s&sign=%s", version_enc, g_sign_res_enc, et_enc, method_enc, g_sign_enc);
    if (auth_len <= 0 || auth_len >= (int)out_cap)
        return 0;
    printf("OTA auth: now=%lu et=%lu ttl=%lu key_len=%lu\r\n", (unsigned long)now_unix, (unsigned long)et, (unsigned long)ONENET_AUTH_ET_TTL_SEC, (unsigned long)key_len);
    printf("OTA auth: token=%s\r\n", out);
    return 1;
}

int onenet_ota_http_status_code(const uint8_t *resp, uint32_t len)
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
                        return atoi((const char *)&resp[j + 1]);
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

int onenet_ota_http_body(const uint8_t *resp, uint32_t len, const uint8_t **body, uint32_t *body_len)
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
        if (i + 15 < header_end && resp[i] == 'C' && resp[i + 1] == 'o' && resp[i + 2] == 'n' && resp[i + 3] == 't' && resp[i + 4] == 'e' && resp[i + 5] == 'n' && resp[i + 6] == 't' && resp[i + 7] == '-' && resp[i + 8] == 'L' && resp[i + 9] == 'e' && resp[i + 10] == 'n' && resp[i + 11] == 'g' && resp[i + 12] == 't' && resp[i + 13] == 'h')
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
        *body_len = content_length;
    else
        *body_len = len - (header_end + 4);
    return 1;
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

static int ota_try_sync_unix_base_from_text(onenet_ota_ctx_t *ctx, const char *text)
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
    uint32_t tick_s = PLATFORM_GET_TICK(g_tick) / 1000UL;
    if (now > tick_s)
    {
        ctx->unix_now_base = now - tick_s;
        ota_rtc_set_unix_timestamp(ctx, now);
        printf("OTA auth: sync server now=%lu tick=%lu new_base=%lu (RTC updated)\r\n", (unsigned long)now, (unsigned long)tick_s, (unsigned long)ctx->unix_now_base);
        return 1;
    }
    return 0;
}

static int ota_report_result(onenet_ota_ctx_t *ctx, const onenet_ota_package_info_t *info, int result_code)
{
    if (info == NULL || info->tid[0] == '\0')
        return 0;
    if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        return 0;
    snprintf(g_ota_body, sizeof(g_ota_body), "{\"step\":%d}", result_code);
    int req_len = snprintf(g_ota_req, sizeof(g_ota_req), "POST /fuse-ota/%s/%s/%s/status HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);
    if (req_len <= 0)
        return 0;
    int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 3000);
    if (n <= 0)
        return 0;
    int status = onenet_ota_http_status_code(g_ota_resp, (uint32_t)n);
    if (status < 200 || status >= 300)
        return 0;
    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (!onenet_ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
        return 0;
    int code = 0;
    if (!ota_json_get_code(res_body, body_len, &code))
        return ota_json_is_success(res_body, body_len);
    printf("OTA status: step=%d code=%d\r\n", result_code, code);
    return (code == 0 || code == 20);
}

static void ota_progress_callback_wrapper(const onenet_ota_package_info_t *info, int progress)
{
    (void)info;
    (void)progress;
}

static int ota_report_version(onenet_ota_ctx_t *ctx)
{
    for (int attempt = 0; attempt < 2; attempt++)
    {
        printf("OTA version: build auth\r\n");
        if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        {
            printf("OTA version: auth fail\r\n");
            return 0;
        }
        snprintf(g_ota_body, sizeof(g_ota_body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}", ctx->firmware_version, ctx->firmware_version);
        int req_len = snprintf(g_ota_req, sizeof(g_ota_req), "POST /fuse-ota/%s/%s/version HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);
        if (req_len <= 0)
            return 0;
        printf("OTA version: send req len=%d\r\n", req_len);
        int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 3000);
        if (n <= 0)
        {
            printf("OTA version: http fail n=%d\r\n", n);
            return 0;
        }
        int status = onenet_ota_http_status_code(g_ota_resp, (uint32_t)n);
        printf("OTA version: http status=%d resp_len=%d\r\n", status, n);
        if (status < 200 || status >= 300)
            return 0;
        const uint8_t *res_body = NULL;
        uint32_t body_len = 0;
        if (!onenet_ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
            return 0;
        int ok = ota_json_is_success(res_body, body_len);
        printf("OTA version: json ok=%d body_len=%lu\r\n", ok, (unsigned long)body_len);
        if (ok)
            return 1;
        int code = 0;
        if (ota_json_get_code(res_body, body_len, &code) && code == 10403 && ota_try_sync_unix_base_from_text(ctx, g_ota_json) && attempt == 0)
        {
            printf("OTA version: retry after time sync\r\n");
            continue;
        }
        return 0;
    }
    return 0;
}

static int ota_check_upgrade(onenet_ota_ctx_t *ctx, onenet_ota_package_info_t *info)
{
    if (info == NULL)
        return 0;
    for (int attempt = 0; attempt < 2; attempt++)
    {
        memset(info, 0, sizeof(onenet_ota_package_info_t));
        printf("OTA check: build auth\r\n");
        if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
            return 0;
        int req_len = snprintf(g_ota_req, sizeof(g_ota_req), "GET /fuse-ota/%s/%s/check?type=2&version=%s HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nConnection: close\r\n\r\n", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ctx->firmware_version, ONENET_FUSE_HOST, g_ota_auth);
        if (req_len <= 0)
            return 0;
        printf("OTA check: send req len=%d version=%s\r\n", req_len, ctx->firmware_version);
        int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 8000);
        if (n <= 0)
            return 0;
        int http_status = onenet_ota_http_status_code(g_ota_resp, (uint32_t)n);
        printf("OTA check: http status=%d resp_len=%d\r\n", http_status, n);
        if (http_status != 200)
            return 0;
        const uint8_t *res_body = NULL;
        uint32_t body_len = 0;
        if (!onenet_ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
            return 0;
        uint32_t copy_len = body_len;
        if (copy_len >= sizeof(g_ota_json))
            copy_len = sizeof(g_ota_json) - 1U;
        memcpy(g_ota_json, res_body, copy_len);
        g_ota_json[copy_len] = '\0';
        cJSON *root = cJSON_Parse(g_ota_json);
        if (root == NULL)
            return 0;
        int has_pkg = 0;
        cJSON *code_node = cJSON_GetObjectItemCaseSensitive(root, "code");
        if (cJSON_IsNumber(code_node))
        {
            printf("OTA check: code=%d\r\n", code_node->valueint);
            if (code_node->valueint == 10403 && ota_try_sync_unix_base_from_text(ctx, g_ota_json) && attempt == 0)
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
            if (cJSON_IsNumber(status) && (status->valueint == 1 || status->valueint == 2 || status->valueint == 3) && cJSON_IsString(target) && cJSON_IsNumber(size) && cJSON_IsString(md5))
            {
                if ((uint32_t)size->valuedouble > 0U)
                {
                    strncpy(info->target, target->valuestring, sizeof(info->target) - 1U);
                    strncpy(info->md5_hex, md5->valuestring, sizeof(info->md5_hex) - 1U);
                    info->size = (uint32_t)size->valuedouble;
                    if (strlen(info->md5_hex) == 32U && ota_md5_hex_to_bin(info->md5_hex, info->md5_bin) && ota_json_copy_tid(tid, info->tid, sizeof(info->tid)))
                    {
                        has_pkg = 1;
                        printf("OTA check: has pkg tid=%s target=%s size=%lu status=%d\r\n", info->tid, info->target, (unsigned long)info->size, status->valueint);
                    }
                }
            }
        }
        if (!has_pkg)
            printf("OTA check: no valid pkg\r\n");
        cJSON_Delete(root);
        return has_pkg;
    }
    return 0;
}

static int ota_check_task_ready(onenet_ota_ctx_t *ctx, const onenet_ota_package_info_t *info)
{
    if (info == NULL || info->tid[0] == '\0')
        return 0;
    printf("OTA task: check tid=%s\r\n", info->tid);
    if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
        return 0;
    int req_len = snprintf(g_ota_req, sizeof(g_ota_req), "GET /fuse-ota/%s/%s/%s/check?type=2&version=%s HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nConnection: close\r\n\r\n", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid, ctx->firmware_version, ONENET_FUSE_HOST, g_ota_auth);
    if (req_len <= 0)
        return 0;
    int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 4000);
    if (n <= 0)
        return 0;
    int http_status = onenet_ota_http_status_code(g_ota_resp, (uint32_t)n);
    if (http_status != 200)
        return 0;
    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (!onenet_ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
        return 0;
    uint32_t copy_len = body_len;
    if (copy_len >= sizeof(g_ota_json))
        copy_len = sizeof(g_ota_json) - 1U;
    memcpy(g_ota_json, res_body, copy_len);
    g_ota_json[copy_len] = '\0';
    cJSON *root = cJSON_Parse(g_ota_json);
    if (root == NULL)
        return 0;
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

static int ota_download_and_verify(onenet_ota_ctx_t *ctx, const onenet_ota_package_info_t *info)
{
    if (info == NULL || ctx == NULL)
        return ONENET_STEP_FAIL_UNKNOWN;
    platform_transport_base_t *target_transport = NULL;
    const char *target_path = NULL;
    bootloader_err_t err = BOOTLOADER_OK;
    FATFS fatfs;
    lfs_t lfs;
    int fs_initialized = 0;
    printf("OTA download: start tid=%s size=%lu target=%s\r\n", info->tid, (unsigned long)info->size, info->target);
    ota_report_result(ctx, info, 0);
    switch (ctx->target_type)
    {
    case ONENET_OTA_TARGET_INTERNAL_FLASH:
        printf("OTA download: target = Internal Flash\r\n");
        bootloader_ctx.config.storage.internal_flash_addr = APPLICATION_ADDRESS;
        target_transport = &g_internal_flash.transport_base;
        target_path = NULL;
        break;
    case ONENET_OTA_TARGET_SD_CARD_FATFS:
        printf("OTA download: target = SD Card (FATFS)\r\n");
        {
            FRESULT res = f_mount(&fatfs, "0:", 1);
            if (res != FR_OK)
            {
                printf("OTA download: SD card mount failed, res=%d\r\n", res);
                return ONENET_STEP_FAIL_NO_SPACE;
            }
            g_fatfs_transport.fs = &fatfs;
            snprintf(bootloader_ctx.config.storage.fatfs_path, sizeof(bootloader_ctx.config.storage.fatfs_path), "0:ota_%s.bin", info->target);
            target_transport = &g_fatfs_transport.base;
            target_path = bootloader_ctx.config.storage.fatfs_path;
            fs_initialized = 1;
        }
        break;
    case ONENET_OTA_TARGET_SPI_FLASH_LFS:
        printf("OTA download: target = SPI Flash (LittleFS)\r\n");
        {
            int res = lfs_spi_flash_init();
            if (res != 0)
                return ONENET_STEP_FAIL_NO_SPACE;
            res = lfs_spi_flash_mount(&lfs);
            if (res != LFS_ERR_OK)
                return ONENET_STEP_FAIL_NO_SPACE;
            g_lfs_transport.lfs = &lfs;
            snprintf(bootloader_ctx.config.storage.lfs_path, sizeof(bootloader_ctx.config.storage.lfs_path), "ota_%s.bin", info->target);
            target_transport = &g_lfs_transport.base;
            target_path = bootloader_ctx.config.storage.lfs_path;
            fs_initialized = 2;
        }
        break;
    default:
        printf("OTA download: invalid target type %d\r\n", ctx->target_type);
        return ONENET_STEP_FAIL_UNKNOWN;
    }

    char http_auth[256];
    uint8_t http_resp[ONENET_DOWNLOAD_CHUNK_SIZE + 512];
    char http_req[768];
    if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, http_auth, sizeof(http_auth)))
    {
        printf("OTA download: auth fail\r\n");
        return ONENET_STEP_FAIL_UNKNOWN;
    }
    uint32_t http_offset = 0;
    int download_ok = ONENET_STEP_DOWNLOAD_OK;
    int last_reported_pct = 0;

    int16_t tgt_ret = target_transport->target_ops->open(target_transport, target_path, info->size);
    if (tgt_ret != TRANSPORT_STATUS_OK)
    {
        printf("OTA download: tgt open failed err=%d\r\n", tgt_ret);
        download_ok = ONENET_STEP_FAIL_NO_SPACE;
    }

    while (download_ok == ONENET_STEP_DOWNLOAD_OK && http_offset < info->size)
    {
        uint32_t chunk_size = ONENET_DOWNLOAD_CHUNK_SIZE;
        if (http_offset + chunk_size > info->size)
            chunk_size = info->size - http_offset;
        uint32_t end = http_offset + chunk_size - 1U;
        if (end >= info->size)
            end = info->size - 1U;
        int req_len = snprintf(http_req, sizeof(http_req), "GET /fuse-ota/%s/%s/%s/download HTTP/1.1\r\nHost: %s\r\nRange: %lu-%lu\r\nAuthorization: %s\r\nConnection: close\r\n\r\n", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid, ONENET_FUSE_HOST, (unsigned long)http_offset, (unsigned long)end, http_auth);
        if (req_len <= 0)
        {
            download_ok = ONENET_STEP_FAIL_UNKNOWN;
            break;
        }
        int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)http_req, (uint32_t)req_len, http_resp, sizeof(http_resp), 12000);
        if (n <= 0)
        {
            printf("OTA download: http fail\r\n");
            download_ok = ONENET_STEP_FAIL_TIMEOUT;
            break;
        }
        int status = onenet_ota_http_status_code(http_resp, (uint32_t)n);
        if (!(status == 206 || status == 200))
        {
            printf("OTA download: bad status=%d\r\n", status);
            download_ok = ONENET_STEP_FAIL_TIMEOUT;
            break;
        }
        const uint8_t *body = NULL;
        uint32_t body_len = 0;
        if (!onenet_ota_http_body(http_resp, (uint32_t)n, &body, &body_len))
        {
            download_ok = ONENET_STEP_FAIL_UNKNOWN;
            break;
        }
        if (body_len > 4 && (http_offset + body_len) < info->size)
            body_len = (body_len / 4) * 4;
        int16_t wr = target_transport->target_ops->write(target_transport, http_offset, body, body_len);
        if (wr != TRANSPORT_STATUS_OK)
        {
            printf("OTA download: write fail\r\n");
            download_ok = ONENET_STEP_FAIL_NO_SPACE;
            break;
        }
        http_offset += body_len;
        int pct = (int)((http_offset * 100U) / info->size);
        if (pct > 100)
            pct = 100;
        if (ctx->progress_cb)
            ctx->progress_cb(info, pct);
        if ((pct - last_reported_pct) >= (int)ONENET_PROGRESS_STEP_PCT || pct >= 100)
        {
            ota_report_result(ctx, info, pct);
            last_reported_pct = pct;
        }
    }

    if (target_transport->target_ops->close)
        target_transport->target_ops->close(target_transport);
    if (download_ok != ONENET_STEP_DOWNLOAD_OK)
    {
        if (fs_initialized == 1)
            f_mount(NULL, "0:", 0);
        else if (fs_initialized == 2)
            lfs_spi_flash_unmount(&lfs);
        return download_ok;
    }

    printf("OTA download: verifying data...\r\n");
    MD5_CTX verify_ctx;
    MD5Init(&verify_ctx);
    uint8_t verify_buf[512];
    uint32_t verify_offset = 0;
    if (ctx->target_type == ONENET_OTA_TARGET_INTERNAL_FLASH)
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
    else if (ctx->target_type == ONENET_OTA_TARGET_SD_CARD_FATFS)
    {
        FIL fp;
        UINT bytes_read;
        FRESULT res = f_open(&fp, bootloader_ctx.config.storage.fatfs_path, FA_READ);
        if (res != FR_OK)
        {
            f_mount(NULL, "0:", 0);
            return ONENET_STEP_FAIL_UNKNOWN;
        }
        while (verify_offset < info->size)
        {
            uint32_t to_read = sizeof(verify_buf);
            if (verify_offset + to_read > info->size)
                to_read = info->size - verify_offset;
            res = f_read(&fp, verify_buf, to_read, &bytes_read);
            if (res != FR_OK || bytes_read != to_read)
            {
                f_close(&fp);
                f_mount(NULL, "0:", 0);
                return ONENET_STEP_FAIL_UNKNOWN;
            }
            MD5Update(&verify_ctx, verify_buf, to_read);
            verify_offset += to_read;
        }
        f_close(&fp);
        f_mount(NULL, "0:", 0);
    }
    else if (ctx->target_type == ONENET_OTA_TARGET_SPI_FLASH_LFS)
    {
        lfs_file_t file;
        int res = lfs_file_open(&lfs, &file, bootloader_ctx.config.storage.lfs_path, LFS_O_RDONLY);
        if (res != LFS_ERR_OK)
        {
            lfs_spi_flash_unmount(&lfs);
            return ONENET_STEP_FAIL_UNKNOWN;
        }
        while (verify_offset < info->size)
        {
            uint32_t to_read = sizeof(verify_buf);
            if (verify_offset + to_read > info->size)
                to_read = info->size - verify_offset;
            lfs_ssize_t bytes_read = lfs_file_read(&lfs, &file, verify_buf, to_read);
            if (bytes_read != (lfs_ssize_t)to_read)
            {
                lfs_file_close(&lfs, &file);
                lfs_spi_flash_unmount(&lfs);
                return ONENET_STEP_FAIL_UNKNOWN;
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
    printf("OTA download: md5=%s\r\n", verify_hex);
    printf("OTA download: remote_md5=%s\r\n", info->md5_hex);
    if (memcmp(verify_md5, info->md5_bin, 16) != 0)
    {
        printf("OTA md5 mismatch\r\n");
        return ONENET_STEP_UPGRADE_MD5_FAIL;
    }
    printf("OTA download: verify ok\r\n");
    return ONENET_STEP_DOWNLOAD_OK;
}

void onenet_ota_ctx_init(onenet_ota_ctx_t *ctx, platform_wifi_base_t *wifi, platform_rtc_base_t *rtc, platform_mqtt_base_t *mqtt)
{
    if (ctx == NULL)
        return;
    memset(ctx, 0, sizeof(onenet_ota_ctx_t));
    ctx->wifi = wifi;
    ctx->rtc = rtc;
    ctx->mqtt = mqtt;
    ctx->target_type = ONENET_OTA_TARGET_INTERNAL_FLASH;
    ctx->unix_now_base = ONENET_AUTH_UNIX_NOW_BASE;
    strncpy(ctx->firmware_version, ONENET_CURRENT_VERSION, sizeof(ctx->firmware_version) - 1);
}

void onenet_ota_set_target(onenet_ota_ctx_t *ctx, onenet_ota_target_t target)
{
    if (ctx == NULL)
        return;
    ctx->target_type = (uint8_t)target;
}

void onenet_ota_set_firmware_version(onenet_ota_ctx_t *ctx, const char *version)
{
    if (ctx == NULL || version == NULL)
        return;
    strncpy(ctx->firmware_version, version, sizeof(ctx->firmware_version) - 1);
    ctx->firmware_version[sizeof(ctx->firmware_version) - 1] = '\0';
}

void onenet_ota_set_progress_callback(onenet_ota_ctx_t *ctx, onenet_ota_progress_cb_t cb)
{
    if (ctx == NULL)
        return;
    ctx->progress_cb = cb;
}

void onenet_ota_set_task_id(const char *tid)
{
    if (tid == NULL || tid[0] == '\0')
    {
        g_ota_tid[0] = '\0';
        return;
    }
    strncpy(g_ota_tid, tid, sizeof(g_ota_tid) - 1U);
    g_ota_tid[sizeof(g_ota_tid) - 1U] = '\0';
}

int onenet_ota_sync_time(onenet_ota_ctx_t *ctx)
{
    printf("Time sync: building auth header...\r\n");
    if (!onenet_ota_build_auth(ctx, ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
    {
        printf("Time sync: auth build failed\r\n");
        return 0;
    }
    snprintf(g_ota_body, sizeof(g_ota_body), "{\"s_version\":\"%s\",\"f_version\":\"%s\"}", ctx->firmware_version, ctx->firmware_version);
    int req_len = snprintf(g_ota_req, sizeof(g_ota_req), "POST /fuse-ota/%s/%s/version HTTP/1.1\r\nHost: %s\r\nAuthorization: %s\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_FUSE_HOST, g_ota_auth, (unsigned int)strlen(g_ota_body), g_ota_body);
    if (req_len <= 0)
        return 0;
    printf("Time sync: sending HTTP request...\r\n");
    int n = wifi_http_request(ctx->wifi, ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 5000);
    if (n <= 0)
    {
        printf("Time sync: HTTP request failed\r\n");
        return 0;
    }
    if (ota_try_sync_unix_base_from_text(ctx, (const char *)g_ota_resp))
    {
        printf("Time sync: success!\r\n");
        return 1;
    }
    const uint8_t *res_body = NULL;
    uint32_t body_len = 0;
    if (onenet_ota_http_body(g_ota_resp, (uint32_t)n, &res_body, &body_len))
    {
        uint32_t copy_len = body_len;
        if (copy_len >= sizeof(g_ota_json))
            copy_len = sizeof(g_ota_json) - 1;
        memcpy(g_ota_json, res_body, copy_len);
        g_ota_json[copy_len] = '\0';
        if (ota_try_sync_unix_base_from_text(ctx, g_ota_json))
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
                uint32_t tick_s = PLATFORM_GET_TICK(g_tick) / 1000UL;
                if (now > tick_s)
                {
                    ctx->unix_now_base = now - tick_s;
                    ota_rtc_set_unix_timestamp(ctx, now);
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
                    uint32_t tick_s = PLATFORM_GET_TICK(g_tick) / 1000UL;
                    if (now > tick_s)
                    {
                        ctx->unix_now_base = now - tick_s;
                        ota_rtc_set_unix_timestamp(ctx, now);
                        printf("Time sync: success from JSON data.now=%lu\r\n", (unsigned long)now);
                        cJSON_Delete(root);
                        return 1;
                    }
                }
            }
            cJSON_Delete(root);
        }
    }
    printf("Time sync: failed\r\n");
    return 0;
}

void onenet_ota_process_upgrade(onenet_ota_ctx_t *ctx)
{
    onenet_ota_package_info_t info;
    memset(&info, 0, sizeof(info));
    printf("OTA start\r\n");
    if (!ota_report_version(ctx))
        printf("OTA stop: report version fail\r\n");
    if (!ota_check_upgrade(ctx, &info))
    {
        printf("OTA no package\r\n");
        return;
    }
    if (!ota_check_task_ready(ctx, &info))
    {
        printf("OTA task not ready\r\n");
        return;
    }
    printf("OTA check success, package info:\r\n");
    printf("  tid: %s\r\n", info.tid);
    printf("  target: %s\r\n", info.target);
    printf("  size: %lu\r\n", (unsigned long)info.size);
    printf("  md5: %s\r\n", info.md5_hex);
    int dl_result = ota_download_and_verify(ctx, &info);
    if (dl_result != ONENET_STEP_DOWNLOAD_OK)
    {
        ota_report_result(ctx, &info, dl_result);
        printf("OTA failed (step=%d)\r\n", dl_result);
        return;
    }
    ota_report_result(ctx, &info, ONENET_STEP_UPGRADE_SUCCESS);
    if (ctx->mqtt && MQTT_CHECK_CONNECTED(ctx->mqtt, 0) != PLATFORM_MQTT_OK)
    {
        printf("OTA MQTT: not connected, configuring...\r\n");
        platform_mqtt_user_config_t mqtt_cfg;
        memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
        strncpy(mqtt_cfg.client_id, ONENET_DEVICE_NAME, sizeof(mqtt_cfg.client_id) - 1);
        strncpy(mqtt_cfg.username, ONENET_PRODUCT_ID, sizeof(mqtt_cfg.username) - 1);
        strncpy(mqtt_cfg.password, ONENET_MQTT_TOKEN, sizeof(mqtt_cfg.password) - 1);
        if (MQTT_USERCFG(ctx->mqtt, 0, &mqtt_cfg) == PLATFORM_MQTT_OK &&
            MQTT_CONNECT(ctx->mqtt, 0, ONENET_MQTT_HOST, ONENET_MQTT_PORT, 1) == PLATFORM_MQTT_OK)
        {
            char sub_topic[PLATFORM_MQTT_MAX_TOPIC_LEN];
            snprintf(sub_topic, sizeof(sub_topic), "$sys/%s/%s/thing/property/set", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
            MQTT_SUBSCRIBE(ctx->mqtt, 0, sub_topic, 0);
            printf("OTA MQTT: connected\r\n");
        }
        else
        {
            printf("OTA MQTT: connect failed\r\n");
        }
    }
    onenet_ota_set_firmware_version(ctx, info.target);
    if (ctx->mqtt && MQTT_CHECK_CONNECTED(ctx->mqtt, 0) == PLATFORM_MQTT_OK)
    {
        platform_mqtt_property_t prop;
        memset(&prop, 0, sizeof(prop));
        strncpy(prop.key, "FIRMWARE_VERSION", sizeof(prop.key) - 1);
        strncpy(prop.id, info.target, sizeof(prop.id) - 1);
        prop.value_type = PLATFORM_MQTT_VALUE_STRING;
        MQTT_PUBLISH_PROPERTY(ctx->mqtt, 0, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, &prop, 1, NULL);
        printf("OTA MQTT: FIRMWARE_VERSION set to %s\r\n", info.target);
    }
    printf("OTA download success\r\n");
}
