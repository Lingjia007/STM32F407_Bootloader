#include "firmware_package.h"
#include "hkdf.h"
#include "aes.h"
#include "edsign.h"
#include "sha512.h"
#include <string.h>
#include <stdio.h>

void fw_pkg_sha512_feed(struct sha512_state *state,
                        const uint8_t *data, size_t len,
                        uint8_t *pending, size_t *pending_len)
{
    size_t offset = 0;

    if (*pending_len > 0)
    {
        size_t need = SHA512_BLOCK_SIZE - *pending_len;
        if (len < need)
        {
            memcpy(pending + *pending_len, data, len);
            *pending_len += len;
            return;
        }
        memcpy(pending + *pending_len, data, need);
        sha512_block(state, pending);
        offset = need;
        *pending_len = 0;
    }

    while (offset + SHA512_BLOCK_SIZE <= len)
    {
        sha512_block(state, data + offset);
        offset += SHA512_BLOCK_SIZE;
    }

    if (offset < len)
    {
        *pending_len = len - offset;
        memcpy(pending, data + offset, *pending_len);
    }
}

void fw_pkg_sha512_finish(struct sha512_state *state,
                          uint8_t *pending, size_t pending_len,
                          size_t total_stream_size,
                          uint8_t hash[64])
{
    sha512_final(state, pending, total_stream_size);
    sha512_get(state, hash, 0, 64);
}

fw_pkg_err_t fw_pkg_parse_header(fw_pkg_ctx_t *ctx,
                                 const uint8_t *data, size_t len)
{
    if (ctx == NULL || data == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    if (len < FW_PKG_HEADER_SIZE)
    {
        return FW_PKG_ERR_SIZE;
    }

    memcpy(&ctx->header, data, FW_PKG_HEADER_SIZE);

    if (ctx->header.magic != FW_PKG_MAGIC)
    {
        printf("fw_pkg: bad magic 0x%08lX (expected 0x%08lX)\r\n",
               (unsigned long)ctx->header.magic, (unsigned long)FW_PKG_MAGIC);
        return FW_PKG_ERR_MAGIC;
    }

    if (ctx->header.header_version != FW_PKG_HEADER_VERSION)
    {
        printf("fw_pkg: unsupported header version %d\r\n", ctx->header.header_version);
        return FW_PKG_ERR_HEADER_VERSION;
    }

    if (ctx->header.total_payload_size < FW_PKG_SALT_SIZE + FW_PKG_IV_SIZE)
    {
        printf("fw_pkg: payload too small %lu\r\n", (unsigned long)ctx->header.total_payload_size);
        return FW_PKG_ERR_SIZE;
    }

    ctx->ciphertext_size = ctx->header.total_payload_size - FW_PKG_SALT_SIZE - FW_PKG_IV_SIZE;

    printf("fw_pkg: header OK, v%u.%u.%u, payload=%lu, cipher=%lu, enc=%u, sig=%u\r\n",
           ctx->header.firmware_major,
           ctx->header.firmware_minor,
           ctx->header.firmware_patch,
           (unsigned long)ctx->header.total_payload_size,
           (unsigned long)ctx->ciphertext_size,
           ctx->header.encryption_algo,
           ctx->header.signature_algo);

    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_verify_header_hmac(const fw_pkg_ctx_t *ctx,
                                       const fw_pkg_verify_config_t *config)
{
    if (ctx == NULL || config == NULL || config->devkey == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    uint8_t computed_hmac[FW_PKG_HMAC_SIZE];
    const uint8_t *header_prefix = (const uint8_t *)&ctx->header;

    hmac_sha256(config->devkey, config->devkey_len,
                header_prefix, FW_PKG_HEADER_SIZE - FW_PKG_HMAC_SIZE,
                computed_hmac);

    if (memcmp(computed_hmac, ctx->header.header_checksum, FW_PKG_HMAC_SIZE) != 0)
    {
        printf("fw_pkg: HMAC verification FAILED\r\n");
        return FW_PKG_ERR_HMAC;
    }

    printf("fw_pkg: HMAC verification OK\r\n");
    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_derive_aes_key(const fw_pkg_ctx_t *ctx,
                                   const fw_pkg_verify_config_t *config,
                                   uint8_t aes_key[FW_PKG_AES_KEY_SIZE])
{
    if (ctx == NULL || config == NULL || config->devkey == NULL || config->uid == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    hkdf(ctx->dynamic_salt, FW_PKG_SALT_SIZE,
         config->devkey, config->devkey_len,
         config->uid, config->uid_len,
         aes_key, FW_PKG_AES_KEY_SIZE);

    printf("fw_pkg: AES key derived via HKDF\r\n");
    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_verify_signature_hash(const fw_pkg_ctx_t *ctx,
                                          const fw_pkg_verify_config_t *config,
                                          const uint8_t hash[64])
{
    if (ctx == NULL || config == NULL || config->ed25519_pubkey == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    if (ctx->header.signature_algo != FW_PKG_SIG_ED25519)
    {
        printf("fw_pkg: signature algo %u not supported\r\n", ctx->header.signature_algo);
        return FW_PKG_ERR_UNSUPPORTED;
    }

    uint8_t result = edsign_verify(ctx->signature,
                                   config->ed25519_pubkey,
                                   hash, 64);

    if (result == 0)
    {
        printf("fw_pkg: Ed25519 signature verification FAILED\r\n");
        return FW_PKG_ERR_SIGNATURE;
    }

    printf("fw_pkg: Ed25519 signature verification OK\r\n");
    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_verify_signature(const fw_pkg_ctx_t *ctx,
                                     const fw_pkg_verify_config_t *config,
                                     const uint8_t *header_and_payload,
                                     size_t header_and_payload_len)
{
    if (ctx == NULL || config == NULL || header_and_payload == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    struct sha512_state state;
    uint8_t pending[SHA512_BLOCK_SIZE];
    size_t pending_len = 0;
    uint8_t hash[64];

    sha512_init(&state);
    fw_pkg_sha512_feed(&state, header_and_payload, header_and_payload_len,
                       pending, &pending_len);
    fw_pkg_sha512_finish(&state, pending, pending_len,
                         header_and_payload_len, hash);

    return fw_pkg_verify_signature_hash(ctx, config, hash);
}

fw_pkg_err_t fw_pkg_check_rollback(const fw_pkg_ctx_t *ctx,
                                   const fw_pkg_verify_config_t *config)
{
    if (ctx == NULL || config == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    if (ctx->header.security_counter < config->stored_security_counter)
    {
        printf("fw_pkg: ROLLBACK detected! pkg_counter=%lu < stored_counter=%lu\r\n",
               (unsigned long)ctx->header.security_counter,
               (unsigned long)config->stored_security_counter);
        return FW_PKG_ERR_ROLLBACK;
    }

    printf("fw_pkg: security counter OK (%lu >= %lu)\r\n",
           (unsigned long)ctx->header.security_counter,
           (unsigned long)config->stored_security_counter);
    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_decrypt_init(fw_pkg_ctx_t *ctx,
                                 const uint8_t *aes_key)
{
    if (ctx == NULL || aes_key == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    switch (ctx->header.encryption_algo)
    {
    case FW_PKG_ENC_NONE:
        ctx->aes_ctx_initialized = 0;
        break;

    case FW_PKG_ENC_AES256_CBC:
        AES_init_ctx_iv(&ctx->aes_ctx, aes_key, ctx->iv);
        ctx->aes_ctx_initialized = 1;
        printf("fw_pkg: AES-256-CBC context initialized\r\n");
        break;

    case FW_PKG_ENC_AES256_CTR:
        AES_init_ctx_iv(&ctx->aes_ctx, aes_key, ctx->iv);
        ctx->aes_ctx_initialized = 1;
        printf("fw_pkg: AES-256-CTR context initialized\r\n");
        break;

    case FW_PKG_ENC_AES256_ECB:
        AES_init_ctx(&ctx->aes_ctx, aes_key);
        ctx->aes_ctx_initialized = 1;
        printf("fw_pkg: AES-256-ECB context initialized\r\n");
        break;

    default:
        printf("fw_pkg: unsupported encryption algo %u\r\n", ctx->header.encryption_algo);
        ctx->aes_ctx_initialized = 0;
        return FW_PKG_ERR_UNSUPPORTED;
    }

    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_decrypt_payload(fw_pkg_ctx_t *ctx,
                                    uint8_t *ciphertext,
                                    size_t ciphertext_len)
{
    if (ctx == NULL || ciphertext == NULL)
    {
        return FW_PKG_ERR_PARAM;
    }

    if (ciphertext_len == 0)
    {
        return FW_PKG_OK;
    }

    if (ctx->header.encryption_algo == FW_PKG_ENC_NONE)
    {
        return FW_PKG_OK;
    }

    if (!ctx->aes_ctx_initialized)
    {
        printf("fw_pkg: AES context not initialized\r\n");
        return FW_PKG_ERR_DECRYPT;
    }

    if (ciphertext_len % AES_BLOCKLEN != 0)
    {
        printf("fw_pkg: ciphertext not aligned to AES block (%lu %% %d = %lu)\r\n",
               (unsigned long)ciphertext_len, AES_BLOCKLEN,
               (unsigned long)(ciphertext_len % AES_BLOCKLEN));
        return FW_PKG_ERR_DECRYPT;
    }

    switch (ctx->header.encryption_algo)
    {
    case FW_PKG_ENC_AES256_CBC:
        AES_CBC_decrypt_buffer(&ctx->aes_ctx, ciphertext, ciphertext_len);
        break;

    case FW_PKG_ENC_AES256_CTR:
        AES_CTR_xcrypt_buffer(&ctx->aes_ctx, ciphertext, ciphertext_len);
        break;

    case FW_PKG_ENC_AES256_ECB:
        for (size_t i = 0; i < ciphertext_len; i += AES_BLOCKLEN)
        {
            AES_ECB_decrypt(&ctx->aes_ctx, ciphertext + i);
        }
        break;

    default:
        return FW_PKG_ERR_UNSUPPORTED;
    }

    return FW_PKG_OK;
}

fw_pkg_err_t fw_pkg_process(const platform_transport_base_t *src_transport,
                            const platform_transport_base_t *tgt_transport,
                            const char *path,
                            const fw_pkg_verify_config_t *config)
{
    fw_pkg_ctx_t ctx;
    int16_t err;
    uint32_t total_size = 0;
    uint32_t bytes_read = 0;
    uint32_t total_read = 0;
    uint32_t flash_offset = 0;
    uint8_t aes_key[FW_PKG_AES_KEY_SIZE];
    struct sha512_state sig_state;
    uint8_t sig_pending[SHA512_BLOCK_SIZE];
    size_t sig_pending_len = 0;
    size_t sig_total_len = 0;

    static uint8_t process_buf[FW_PKG_DECRYPT_BUF_SIZE] __attribute__((aligned(4)));

    memset(&ctx, 0, sizeof(ctx));
    memset(aes_key, 0, sizeof(aes_key));

    if (src_transport == NULL || tgt_transport == NULL || config == NULL)
    {
        printf("fw_pkg_process: null param\r\n");
        return FW_PKG_ERR_PARAM;
    }

    if (config->ed25519_pubkey != NULL)
    {
        sha512_init(&sig_state);
        sig_pending_len = 0;
        sig_total_len = 0;
    }

    printf("fw_pkg_process: opening source [%s]...\r\n", src_transport->name);
    err = TRANSPORT_SOURCE_OPEN(src_transport, path, &total_size);
    if (err != TRANSPORT_STATUS_OK)
    {
        printf("fw_pkg_process: src open failed err=%d\r\n", err);
        return FW_PKG_ERR_READ;
    }

    printf("fw_pkg_process: total_size=%lu\r\n", (unsigned long)total_size);

    if (total_size < FW_PKG_HEADER_SIZE + FW_PKG_SALT_SIZE + FW_PKG_IV_SIZE + FW_PKG_SIGNATURE_SIZE)
    {
        printf("fw_pkg_process: file too small\r\n");
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_SIZE;
    }

    printf("fw_pkg_process: reading header (%d bytes)...\r\n", FW_PKG_HEADER_SIZE);
    err = TRANSPORT_SOURCE_READ(src_transport, process_buf, FW_PKG_HEADER_SIZE, &bytes_read);
    if (err != TRANSPORT_STATUS_OK || bytes_read != FW_PKG_HEADER_SIZE)
    {
        printf("fw_pkg_process: read header failed\r\n");
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_READ;
    }
    total_read += bytes_read;

    fw_pkg_err_t ret = fw_pkg_parse_header(&ctx, process_buf, bytes_read);
    if (ret != FW_PKG_OK)
    {
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return ret;
    }

    if (config->ed25519_pubkey != NULL)
    {
        fw_pkg_sha512_feed(&sig_state, process_buf, FW_PKG_HEADER_SIZE,
                           sig_pending, &sig_pending_len);
        sig_total_len += FW_PKG_HEADER_SIZE;
    }

    ret = fw_pkg_verify_header_hmac(&ctx, config);
    if (ret != FW_PKG_OK)
    {
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return ret;
    }

    if (ctx.header.hardware_compat != config->hardware_compat)
    {
        printf("fw_pkg_process: HW compat mismatch (pkg=0x%08lX, board=0x%08lX)\r\n",
               (unsigned long)ctx.header.hardware_compat, (unsigned long)config->hardware_compat);
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_HW_COMPAT;
    }

    ret = fw_pkg_check_rollback(&ctx, config);
    if (ret != FW_PKG_OK)
    {
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return ret;
    }

    printf("fw_pkg_process: reading salt (%d bytes)...\r\n", FW_PKG_SALT_SIZE);
    err = TRANSPORT_SOURCE_READ(src_transport, ctx.dynamic_salt, FW_PKG_SALT_SIZE, &bytes_read);
    if (err != TRANSPORT_STATUS_OK || bytes_read != FW_PKG_SALT_SIZE)
    {
        printf("fw_pkg_process: read salt failed\r\n");
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_READ;
    }
    total_read += bytes_read;

    if (config->ed25519_pubkey != NULL)
    {
        fw_pkg_sha512_feed(&sig_state, ctx.dynamic_salt, FW_PKG_SALT_SIZE,
                           sig_pending, &sig_pending_len);
        sig_total_len += FW_PKG_SALT_SIZE;
    }

    printf("fw_pkg_process: reading IV (%d bytes)...\r\n", FW_PKG_IV_SIZE);
    err = TRANSPORT_SOURCE_READ(src_transport, ctx.iv, FW_PKG_IV_SIZE, &bytes_read);
    if (err != TRANSPORT_STATUS_OK || bytes_read != FW_PKG_IV_SIZE)
    {
        printf("fw_pkg_process: read IV failed\r\n");
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_READ;
    }
    total_read += bytes_read;

    if (config->ed25519_pubkey != NULL)
    {
        fw_pkg_sha512_feed(&sig_state, ctx.iv, FW_PKG_IV_SIZE,
                           sig_pending, &sig_pending_len);
        sig_total_len += FW_PKG_IV_SIZE;
    }

    if (ctx.header.encryption_algo != FW_PKG_ENC_NONE)
    {
        ret = fw_pkg_derive_aes_key(&ctx, config, aes_key);
        if (ret != FW_PKG_OK)
        {
            TRANSPORT_SOURCE_CLOSE(src_transport);
            return ret;
        }

        ret = fw_pkg_decrypt_init(&ctx, aes_key);
        if (ret != FW_PKG_OK)
        {
            TRANSPORT_SOURCE_CLOSE(src_transport);
            return ret;
        }
    }

    uint32_t ciphertext_remaining = ctx.ciphertext_size;
    uint32_t signature_offset = total_size - FW_PKG_SIGNATURE_SIZE;
    uint32_t decrypted_size = ctx.ciphertext_size;

    printf("fw_pkg_process: opening target, decrypted_size=%lu\r\n",
           (unsigned long)decrypted_size);
    err = TRANSPORT_TARGET_OPEN(tgt_transport, path, decrypted_size);
    if (err != TRANSPORT_STATUS_OK)
    {
        printf("fw_pkg_process: tgt open failed err=%d\r\n", err);
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_ERASE;
    }

    printf("fw_pkg_process: processing ciphertext (%lu bytes)...\r\n",
           (unsigned long)ciphertext_remaining);

    while (ciphertext_remaining > 0)
    {
        uint32_t to_read = FW_PKG_DECRYPT_BUF_SIZE;
        if (ciphertext_remaining < to_read)
        {
            to_read = ciphertext_remaining;
        }

        if (total_read + to_read > signature_offset)
        {
            to_read = signature_offset - total_read;
            if (to_read == 0)
                break;
        }

        err = TRANSPORT_SOURCE_READ(src_transport, process_buf, to_read, &bytes_read);
        if (err != TRANSPORT_STATUS_OK)
        {
            printf("fw_pkg_process: read cipher failed\r\n");
            TRANSPORT_TARGET_CLOSE(tgt_transport);
            TRANSPORT_SOURCE_CLOSE(src_transport);
            return FW_PKG_ERR_READ;
        }

        if (bytes_read == 0)
        {
            break;
        }

        total_read += bytes_read;

        if (config->ed25519_pubkey != NULL)
        {
            fw_pkg_sha512_feed(&sig_state, process_buf, bytes_read,
                               sig_pending, &sig_pending_len);
            sig_total_len += bytes_read;
        }

        if (ctx.header.encryption_algo != FW_PKG_ENC_NONE)
        {
            ret = fw_pkg_decrypt_payload(&ctx, process_buf, bytes_read);
            if (ret != FW_PKG_OK)
            {
                TRANSPORT_TARGET_CLOSE(tgt_transport);
                TRANSPORT_SOURCE_CLOSE(src_transport);
                return ret;
            }
        }

        err = TRANSPORT_TARGET_WRITE(tgt_transport, flash_offset, process_buf, bytes_read);
        if (err != TRANSPORT_STATUS_OK)
        {
            printf("fw_pkg_process: write flash failed\r\n");
            TRANSPORT_TARGET_CLOSE(tgt_transport);
            TRANSPORT_SOURCE_CLOSE(src_transport);
            return FW_PKG_ERR_WRITE;
        }

        flash_offset += bytes_read;
        ciphertext_remaining -= bytes_read;

        printf("fw_pkg_process: progress %lu/%lu\r\n",
               (unsigned long)flash_offset, (unsigned long)decrypted_size);
    }

    if (total_read < signature_offset)
    {
        uint32_t skip = signature_offset - total_read;
        printf("fw_pkg_process: skipping %lu bytes to signature\r\n", (unsigned long)skip);
        while (skip > 0)
        {
            uint32_t to_skip = (skip > FW_PKG_DECRYPT_BUF_SIZE) ? FW_PKG_DECRYPT_BUF_SIZE : skip;
            err = TRANSPORT_SOURCE_READ(src_transport, process_buf, to_skip, &bytes_read);
            if (err != TRANSPORT_STATUS_OK || bytes_read == 0)
            {
                printf("fw_pkg_process: skip read failed\r\n");
                TRANSPORT_TARGET_CLOSE(tgt_transport);
                TRANSPORT_SOURCE_CLOSE(src_transport);
                return FW_PKG_ERR_READ;
            }

            if (config->ed25519_pubkey != NULL)
            {
                fw_pkg_sha512_feed(&sig_state, process_buf, bytes_read,
                                   sig_pending, &sig_pending_len);
                sig_total_len += bytes_read;
            }

            total_read += bytes_read;
            skip -= bytes_read;
        }
    }

    printf("fw_pkg_process: reading signature (%d bytes)...\r\n", FW_PKG_SIGNATURE_SIZE);
    err = TRANSPORT_SOURCE_READ(src_transport, ctx.signature, FW_PKG_SIGNATURE_SIZE, &bytes_read);
    if (err != TRANSPORT_STATUS_OK || bytes_read != FW_PKG_SIGNATURE_SIZE)
    {
        printf("fw_pkg_process: read signature failed\r\n");
        TRANSPORT_TARGET_CLOSE(tgt_transport);
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_READ;
    }

    if (ctx.header.signature_algo == FW_PKG_SIG_ED25519 && config->ed25519_pubkey != NULL)
    {
        uint8_t sig_hash[64];
        fw_pkg_sha512_finish(&sig_state, sig_pending, sig_pending_len,
                             sig_total_len, sig_hash);

        ret = fw_pkg_verify_signature_hash(&ctx, config, sig_hash);
        if (ret != FW_PKG_OK)
        {
            printf("fw_pkg_process: SIGNATURE INVALID - firmware may be tampered!\r\n");
            TRANSPORT_TARGET_CLOSE(tgt_transport);
            TRANSPORT_SOURCE_CLOSE(src_transport);
            return ret;
        }
    }

    printf("fw_pkg_process: closing target...\r\n");
    err = TRANSPORT_TARGET_CLOSE(tgt_transport);
    if (err != TRANSPORT_STATUS_OK)
    {
        printf("fw_pkg_process: tgt close failed\r\n");
        TRANSPORT_SOURCE_CLOSE(src_transport);
        return FW_PKG_ERR_WRITE;
    }

    printf("fw_pkg_process: closing source...\r\n");
    err = TRANSPORT_SOURCE_CLOSE(src_transport);
    if (err != TRANSPORT_STATUS_OK)
    {
        printf("fw_pkg_process: src close failed\r\n");
        return FW_PKG_ERR_READ;
    }

    printf("fw_pkg_process: firmware update SUCCESS\r\n");
    return FW_PKG_OK;
}

const char *fw_pkg_err_str(fw_pkg_err_t err)
{
    switch (err)
    {
    case FW_PKG_OK:
        return "OK";
    case FW_PKG_ERR_MAGIC:
        return "Bad magic number";
    case FW_PKG_ERR_HEADER_VERSION:
        return "Unsupported header version";
    case FW_PKG_ERR_HMAC:
        return "HMAC verification failed";
    case FW_PKG_ERR_SIGNATURE:
        return "Ed25519 signature verification failed";
    case FW_PKG_ERR_DECRYPT:
        return "Decryption failed";
    case FW_PKG_ERR_ROLLBACK:
        return "Security counter rollback detected";
    case FW_PKG_ERR_HW_COMPAT:
        return "Hardware compatibility mismatch";
    case FW_PKG_ERR_SIZE:
        return "Size error";
    case FW_PKG_ERR_PARAM:
        return "Invalid parameter";
    case FW_PKG_ERR_READ:
        return "Read error";
    case FW_PKG_ERR_WRITE:
        return "Write error";
    case FW_PKG_ERR_ERASE:
        return "Erase error";
    case FW_PKG_ERR_UNSUPPORTED:
        return "Unsupported algorithm";
    default:
        return "Unknown error";
    }
}
