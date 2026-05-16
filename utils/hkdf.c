#include "hkdf.h"
#include "sha256.h"
#include <string.h>

static void secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
    {
        *v++ = 0;
    }
}

void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t out[32])
{
    uint8_t k_ipad[64] = {0};
    uint8_t k_opad[64] = {0};
    uint8_t tmp[32];
    size_t i;

    uint8_t key_buf[32];

    if (key_len > 64)
    {
        sha256_ctx_t tctx;
        sha256_init(&tctx);
        sha256_update(&tctx, key, key_len);
        sha256_final(&tctx, key_buf);
        key = key_buf;
        key_len = 32;
    }

    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);
    for (i = 0; i < 64; i++)
    {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, tmp);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, tmp, 32);
    sha256_final(&ctx, out);

    secure_zero(tmp, sizeof(tmp));
    secure_zero(key_buf, sizeof(key_buf));
}

void hkdf_extract(const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  uint8_t prk[32])
{
    uint8_t null_salt[32] = {0};
    if (!salt || salt_len == 0)
    {
        salt = null_salt;
        salt_len = 32;
    }
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

void hkdf_expand(const uint8_t *prk, size_t prk_len,
                 const uint8_t *info, size_t info_len,
                 uint8_t *okm, size_t okm_len)
{
    uint8_t t[32];
    size_t pos = 0;
    uint8_t counter = 1;

    while (pos < okm_len)
    {
        uint8_t buf[32 + 256 + 1];
        size_t buf_len = 0;

        if (pos != 0)
        {
            memcpy(buf, t, 32);
            buf_len = 32;
        }

        if (info && info_len > 0)
        {
            memcpy(buf + buf_len, info, info_len);
            buf_len += info_len;
        }

        buf[buf_len++] = counter++;

        hmac_sha256(prk, prk_len, buf, buf_len, t);

        size_t to_copy = (okm_len - pos > 32) ? 32 : (okm_len - pos);
        memcpy(okm + pos, t, to_copy);
        pos += to_copy;

        secure_zero(buf, sizeof(buf));
    }

    secure_zero(t, sizeof(t));
}

void hkdf(const uint8_t *salt, size_t salt_len,
          const uint8_t *ikm, size_t ikm_len,
          const uint8_t *info, size_t info_len,
          uint8_t *okm, size_t okm_len)
{
    uint8_t prk[32];
    hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
    hkdf_expand(prk, 32, info, info_len, okm, okm_len);
    secure_zero(prk, sizeof(prk));
}
