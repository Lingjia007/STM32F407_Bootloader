#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE 32

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t data[], size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[SHA256_BLOCK_SIZE]);
void sha256(const uint8_t *data, size_t len, uint8_t hash[SHA256_BLOCK_SIZE]);

#endif
