#include "onenet_ota.h"
#include "transport.h"
#include "cJSON.h"
#include "md5.h"
#include "ota_flash.h"
#include "fatfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

#define OTA_SD_FILE_PATH "0:ota_firmware.bin"

typedef struct
{
    char tid[48];
    char target[24];
    char md5_hex[33];
    uint8_t md5_bin[16];
    uint32_t size;
} OtaPackageInfo;

static char g_ota_tid[48] = {0};
static char g_ota_auth[256];
static char g_ota_req[768];
static uint8_t g_ota_resp[1600];
static char g_ota_json[1024];
static char g_ota_body[128];
static char g_sign_string[192];
static char g_sign_res_enc[96];
static char g_sign_b64[64];
static char g_sign_enc[128];
static uint8_t g_sign_key_raw[96];
static uint32_t g_ota_unix_now_base = ONENET_AUTH_UNIX_NOW_BASE;
static uint8_t g_ota_write_buf[ONENET_DOWNLOAD_CHUNK_SIZE] __attribute__((aligned(4)));

typedef struct
{
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[5];
} Sha1Ctx;

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

static int ota_build_auth_header(const char *version, const char *res_raw, char *out, uint32_t out_cap)
{
    uint32_t key_len = 0;
    uint8_t digest[20];
    char method_enc[24];
    char version_enc[32];
    char et_text[24];
    char et_enc[32];
    uint32_t now_unix = g_ota_unix_now_base + (HAL_GetTick() / 1000UL);
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

static int ota_http_status_code(const uint8_t *resp, uint32_t len)
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

static int ota_http_body(const uint8_t *resp, uint32_t len, const uint8_t **body, uint32_t *body_len)
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

static int ota_http_request(const char *host, uint16_t port, const uint8_t *req, uint32_t req_len, uint8_t *resp, uint32_t resp_cap, uint32_t timeout_ms)
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
        printf("OTA auth: sync server now=%lu tick=%lu new_base=%lu\r\n",
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

static int ota_download_and_verify(const OtaPackageInfo *info)
{
    if (info == NULL)
        return 0;
    if (!ota_build_auth_header(ONENET_AUTH_FUSE_VER, ONENET_AUTH_FUSE_RES_RAW, g_ota_auth, sizeof(g_ota_auth)))
    {
        printf("OTA download: auth fail\r\n");
        return 0;
    }
    if (info->size == 0U)
    {
        printf("OTA download: bad size=0\r\n");
        return 0;
    }

    FATFS fs;
    FIL fp;
    FRESULT res;
    UINT bytes_written;

    res = f_mount(&fs, "0:", 1);
    if (res != FR_OK)
    {
        printf("OTA download: SD mount fail res=%d\r\n", res);
        return 0;
    }

    f_unlink(OTA_SD_FILE_PATH);

    res = f_open(&fp, OTA_SD_FILE_PATH, FA_CREATE_NEW | FA_WRITE | FA_READ);
    if (res != FR_OK)
    {
        printf("OTA download: SD open fail res=%d, trying FA_CREATE_ALWAYS\r\n", res);
        res = f_open(&fp, OTA_SD_FILE_PATH, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
        if (res != FR_OK)
        {
            printf("OTA download: SD open still fail res=%d\r\n", res);
            f_mount(NULL, "0:", 0);
            return 0;
        }
    }

    const uint32_t chunk_size = ONENET_DOWNLOAD_CHUNK_SIZE;
    uint32_t offset = 0;
    uint32_t last_progress = 0U;
    MD5_CTX md5_ctx;
    MD5Init(&md5_ctx);

    printf("OTA download: start tid=%s size=%lu target=%s\r\n", info->tid, (unsigned long)info->size, info->target);
    ota_report_result(info, 0);

    while (offset < info->size)
    {
        uint32_t end = offset + chunk_size - 1U;
        if (end >= info->size)
            end = info->size - 1U;
        int req_len = snprintf(g_ota_req, sizeof(g_ota_req),
                               "GET /fuse-ota/%s/%s/%s/download HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Range: %lu-%lu\r\n"
                               "Authorization: %s\r\n"
                               "Connection: close\r\n\r\n",
                               ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, info->tid,
                               ONENET_FUSE_HOST,
                               (unsigned long)offset, (unsigned long)end,
                               g_ota_auth);
        if (req_len <= 0 || req_len >= (int)sizeof(g_ota_req))
        {
            printf("OTA download: build req fail offset=%lu end=%lu\r\n", (unsigned long)offset, (unsigned long)end);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }
        int n = ota_http_request(ONENET_FUSE_HOST, ONENET_HTTP_PORT, (const uint8_t *)g_ota_req, (uint32_t)req_len, g_ota_resp, sizeof(g_ota_resp), 12000);
        if (n <= 0)
        {
            printf("OTA download: http fail offset=%lu n=%d\r\n", (unsigned long)offset, n);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }
        int status = ota_http_status_code(g_ota_resp, (uint32_t)n);
        if (!(status == 206 || status == 200))
        {
            printf("OTA download: bad status=%d offset=%lu n=%d\r\n", status, (unsigned long)offset, n);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }
        const uint8_t *body = NULL;
        uint32_t body_len = 0;
        if (!ota_http_body(g_ota_resp, (uint32_t)n, &body, &body_len))
        {
            printf("OTA download: no body offset=%lu\r\n", (unsigned long)offset);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }
        uint32_t expect = end - offset + 1U;

        if (body_len != expect)
        {
            printf("OTA download: size mismatch offset=%lu body_len=%lu expect=%lu resp_len=%d\r\n",
                   (unsigned long)offset, (unsigned long)body_len, (unsigned long)expect, n);
        }

        uint32_t write_len = body_len;
        if (write_len > expect)
            write_len = expect;
        if (write_len == 0U)
        {
            printf("OTA download: write_len=0 offset=%lu body_len=%lu expect=%lu\r\n",
                   (unsigned long)offset, (unsigned long)body_len, (unsigned long)expect);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }

        if (write_len > sizeof(g_ota_write_buf))
        {
            write_len = sizeof(g_ota_write_buf);
        }

        memcpy(g_ota_write_buf, body, write_len);

        res = f_write(&fp, g_ota_write_buf, write_len, &bytes_written);
        if (res != FR_OK || bytes_written != write_len)
        {
            printf("OTA download: SD write fail res=%d written=%u expect=%lu offset=%lu\r\n",
                   res, bytes_written, (unsigned long)write_len, (unsigned long)offset);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }

        res = f_sync(&fp);
        if (res != FR_OK)
        {
            printf("OTA download: SD sync fail res=%d offset=%lu\r\n", res, (unsigned long)offset);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }

        if (offset == 0)
        {
            printf("OTA download: first block write_len=%lu\r\n", (unsigned long)write_len);
            printf("OTA download: first 16 bytes written: ");
            for (int i = 0; i < 16 && i < (int)write_len; i++)
            {
                printf("%02X", g_ota_write_buf[i]);
            }
            printf("\r\n");

            f_sync(&fp);
            {
                uint8_t verify_buf[32];
                UINT verify_read;
                FSIZE_t cur_pos = f_tell(&fp);
                f_lseek(&fp, 0);
                res = f_read(&fp, verify_buf, 32, &verify_read);
                printf("OTA download: immediate verify read res=%d bytes=%u\r\n", res, verify_read);
                if (res == FR_OK && verify_read >= 16)
                {
                    printf("OTA download: immediate verify first 16 bytes: ");
                    for (int i = 0; i < 16; i++)
                    {
                        printf("%02X", verify_buf[i]);
                    }
                    printf("\r\n");
                    if (memcmp(verify_buf, g_ota_write_buf, 16) != 0)
                    {
                        printf("OTA download: FIRST BLOCK VERIFY FAILED!\r\n");
                        printf("OTA download: SD card write error detected!\r\n");
                    }
                }
                f_lseek(&fp, cur_pos);
            }
        }

        MD5Update(&md5_ctx, g_ota_write_buf, write_len);

        offset += write_len;
        uint32_t progress = (offset * 100U) / info->size;
        if (progress > 100U)
            progress = 100U;
        if (progress == 100U || progress >= (last_progress + ONENET_PROGRESS_STEP_PCT))
        {
            ota_report_result(info, (int)progress);
            last_progress = progress;
        }
        printf("OTA download %lu/%lu\r\n", (unsigned long)offset, (unsigned long)info->size);
    }

    f_sync(&fp);
    printf("OTA download: file size before close: %lu bytes\r\n", (unsigned long)f_size(&fp));

    {
        uint8_t check_buf[32];
        UINT check_read;
        FSIZE_t old_pos = f_tell(&fp);
        printf("OTA download: current file pos=%lu\r\n", (unsigned long)old_pos);
        res = f_lseek(&fp, 0);
        printf("OTA download: lseek result=%d\r\n", res);
        res = f_read(&fp, check_buf, 32, &check_read);
        printf("OTA download: read result=%d, bytes=%u\r\n", res, check_read);
        if (res == FR_OK && check_read >= 16)
        {
            printf("OTA download: immediate read first 16 bytes: ");
            for (int i = 0; i < 16; i++)
            {
                printf("%02X", check_buf[i]);
            }
            printf("\r\n");
        }
        f_lseek(&fp, old_pos);
    }

    f_close(&fp);
    f_mount(NULL, "0:", 0);

    uint8_t calc[16];
    MD5Final(&md5_ctx, calc);

    printf("OTA download: verifying SD card data...\r\n");

    res = f_mount(&fs, "0:", 1);
    if (res != FR_OK)
    {
        printf("OTA download: SD remount fail res=%d\r\n", res);
        return 0;
    }

    res = f_open(&fp, OTA_SD_FILE_PATH, FA_READ);
    if (res != FR_OK)
    {
        printf("OTA download: SD reopen fail res=%d\r\n", res);
        f_mount(NULL, "0:", 0);
        return 0;
    }

    printf("OTA download: file size after reopen: %lu bytes\r\n", (unsigned long)f_size(&fp));

    MD5_CTX verify_ctx;
    MD5Init(&verify_ctx);
    uint8_t verify_buf[512];
    uint32_t verify_offset = 0;
    UINT bytes_read;

    while (verify_offset < info->size)
    {
        uint32_t to_read = sizeof(verify_buf);
        if (verify_offset + to_read > info->size)
            to_read = info->size - verify_offset;

        res = f_read(&fp, verify_buf, to_read, &bytes_read);
        if (res != FR_OK || bytes_read != to_read)
        {
            printf("OTA download: SD read fail res=%d read=%u expect=%lu\r\n",
                   res, bytes_read, (unsigned long)to_read);
            f_close(&fp);
            f_mount(NULL, "0:", 0);
            return 0;
        }

        if (verify_offset == 0)
        {
            printf("OTA download: first 16 bytes read from SD: ");
            for (int i = 0; i < 16 && i < (int)to_read; i++)
            {
                printf("%02X", verify_buf[i]);
            }
            printf("\r\n");
        }

        MD5Update(&verify_ctx, verify_buf, to_read);
        verify_offset += to_read;
    }

    f_close(&fp);

    uint8_t verify_md5[16];
    MD5Final(&verify_ctx, verify_md5);

    char download_hex[33], verify_hex[33];
    ota_md5_bin_to_hex(calc, download_hex);
    ota_md5_bin_to_hex(verify_md5, verify_hex);

    printf("OTA download: download_md5=%s\r\n", download_hex);
    printf("OTA download: sdcard_md5=%s\r\n", verify_hex);
    printf("OTA download: remote_md5=%s\r\n", info->md5_hex);

    if (memcmp(calc, verify_md5, 16) != 0)
    {
        printf("OTA download: MD5 mismatch between download and SD card!\r\n");
        f_unlink(OTA_SD_FILE_PATH);
        f_mount(NULL, "0:", 0);
        return 0;
    }

    if (memcmp(calc, info->md5_bin, 16) != 0)
    {
        char local_hex[33];
        ota_md5_bin_to_hex(calc, local_hex);
        printf("OTA md5 mismatch local=%s remote=%s\r\n", local_hex, info->md5_hex);
        f_unlink(OTA_SD_FILE_PATH);
        f_mount(NULL, "0:", 0);
        return 0;
    }

    f_mount(NULL, "0:", 0);

    if (!OTA_SetPendingImage(info->size, info->md5_bin, info->target, info->tid))
    {
        printf("OTA download: set pending image fail\r\n");
        return 0;
    }
    printf("OTA download: verify ok, saved to SD: %s\r\n", OTA_SD_FILE_PATH);
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
    ota_report_result(&info, 201);
    printf("OTA download success, saved to SD card\r\n");
}
