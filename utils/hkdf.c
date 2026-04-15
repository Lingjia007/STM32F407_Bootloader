#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ================= SHA-256 =================
typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} SHA256_CTX;

#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x, 2) ^ ROTRIGHT(x, 13) ^ ROTRIGHT(x, 22))
#define EP1(x) (ROTRIGHT(x, 6) ^ ROTRIGHT(x, 11) ^ ROTRIGHT(x, 25))
#define SIG0(x) (ROTRIGHT(x, 7) ^ ROTRIGHT(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x, 17) ^ ROTRIGHT(x, 19) ^ ((x) >> 10))

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static void secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
    {
        *v++ = 0;
    }
}

void sha256_transform(SHA256_CTX *ctx, const uint8_t data[])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];
    int i, j;

    for (i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (; i < 64; i++)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++)
    {
        t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(SHA256_CTX *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
    {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64)
        {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t hash[])
{
    uint32_t i = ctx->datalen;

    // Pad whatever data is left
    if (ctx->datalen < 56)
    {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    }
    else
    {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;
    ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48;
    ctx->data[56] = ctx->bitlen >> 56;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 8; i++)
    {
        hash[i * 4] = (ctx->state[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (ctx->state[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (ctx->state[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = ctx->state[i] & 0xFF;
    }
}

// ================= HMAC-SHA256 =================
void hmac_sha256(const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t out[32])
{
    uint8_t k_ipad[64] = {0};
    uint8_t k_opad[64] = {0};
    uint8_t tmp[32];
    size_t i;

    // buffer to hold key hash if key_len > 64
    uint8_t key_buf[32];

    // Step 1: hash key if longer than 64 bytes
    if (key_len > 64)
    {
        SHA256_CTX tctx;
        sha256_init(&tctx);
        sha256_update(&tctx, key, key_len);
        sha256_final(&tctx, key_buf);
        key = key_buf; // point to hashed key
        key_len = 32;
    }

    // Step 2: prepare inner and outer padded keys
    memset(k_ipad, 0, 64);
    memset(k_opad, 0, 64);
    memcpy(k_ipad, key, key_len);
    memcpy(k_opad, key, key_len);
    for (i = 0; i < 64; i++)
    {
        k_ipad[i] ^= 0x36;
        k_opad[i] ^= 0x5c;
    }

    // Step 3: inner hash
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, tmp);

    // Step 4: outer hash
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, tmp, 32);
    sha256_final(&ctx, out);

    secure_zero(tmp, sizeof(tmp));
    secure_zero(key_buf, sizeof(key_buf));
}

// ================= HKDF =================
void hkdf_extract(const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  uint8_t prk[32])
{
    uint8_t null_salt[32] = {0};
    if (!salt || salt_len == 0)
    {
        // RFC 5869 says: if salt is not provided, use zeros
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
        uint8_t buf[32 + info_len + 1];
        size_t buf_len = 0;

        if (pos != 0)
        {
            memcpy(buf, t, 32); // prepend previous block
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

// ================= Example =================
#ifdef HKDF_TEST
#include <stdio.h>
int main()
{
    uint8_t ikm[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    uint8_t salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    uint8_t info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    uint8_t okm[42];
    size_t i;

    hkdf(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm, sizeof(okm));

    printf("HKDF output: ");
    for (i = 0; i < sizeof(okm); i++)
        printf("%02x", okm[i]);
    printf("\n");
    return 0;
}
#endif
