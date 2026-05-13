#include "service_ed25519_verify.h"
#include "edsign.h"
#include "sha512.h"
#include <string.h>

#define VERIFY_BUFFER_SIZE 512

static uint8_t verify_buffer[VERIFY_BUFFER_SIZE] __attribute__((aligned(4)));

int ed25519_verify_buffer(const uint8_t *data, uint32_t size,
                          const uint8_t *signature, const uint8_t *public_key)
{
    if (data == NULL || signature == NULL || public_key == NULL || size == 0)
        return ED25519_VERIFY_ERR_PARAM;

    uint8_t result = edsign_verify(signature, public_key, data, (size_t)size);
    return result ? ED25519_VERIFY_OK : ED25519_VERIFY_ERR_FAILED;
}

int ed25519_verify_file(platform_fs_base_t *fs, const char *file_path,
                        const char *sig_path, const ed25519_verify_config_t *config)
{
    platform_fs_file_t data_file;
    platform_fs_file_t sig_file;
    int32_t bytes_read;
    int32_t file_size;
    uint32_t total_read = 0;
    uint8_t signature[EDSIGN_SIGNATURE_SIZE];
    struct sha512_state hash_state;
    uint8_t file_hash[SHA512_HASH_SIZE];
    uint8_t leftover_buf[SHA512_BLOCK_SIZE];
    uint32_t leftover_len = 0;

    if (fs == NULL || file_path == NULL || sig_path == NULL || config == NULL)
        return ED25519_VERIFY_ERR_PARAM;

    if (FS_OPEN(fs, &sig_file, sig_path, FS_MODE_READ) != (int16_t)FS_STATUS_OK)
        return ED25519_VERIFY_ERR_OPEN_SIG;

    bytes_read = FS_READ(fs, &sig_file, signature, EDSIGN_SIGNATURE_SIZE);
    FS_CLOSE(fs, &sig_file);

    if (bytes_read != EDSIGN_SIGNATURE_SIZE)
        return ED25519_VERIFY_ERR_SIG_SIZE;

    if (FS_OPEN(fs, &data_file, file_path, FS_MODE_READ) != (int16_t)FS_STATUS_OK)
        return ED25519_VERIFY_ERR_OPEN_FILE;

    file_size = FS_SIZE(fs, &data_file);
    if (file_size <= 0)
    {
        FS_CLOSE(fs, &data_file);
        return ED25519_VERIFY_ERR_READ_FILE;
    }

    sha512_init(&hash_state);

    while (total_read < (uint32_t)file_size)
    {
        uint32_t to_read = VERIFY_BUFFER_SIZE;
        if ((uint32_t)file_size - total_read < to_read)
            to_read = (uint32_t)file_size - total_read;

        bytes_read = FS_READ(fs, &data_file, verify_buffer, to_read);
        if (bytes_read <= 0)
        {
            FS_CLOSE(fs, &data_file);
            return ED25519_VERIFY_ERR_READ_FILE;
        }

        uint32_t data_len = (uint32_t)bytes_read;
        uint32_t src_offset = 0;

        if (leftover_len > 0)
        {
            uint32_t needed = SHA512_BLOCK_SIZE - leftover_len;
            if (needed <= data_len)
            {
                memcpy(leftover_buf + leftover_len, verify_buffer, needed);
                sha512_block(&hash_state, leftover_buf);
                leftover_len = 0;
                src_offset = needed;
            }
            else
            {
                memcpy(leftover_buf + leftover_len, verify_buffer, data_len);
                leftover_len += data_len;
                total_read += data_len;
                continue;
            }
        }

        while (src_offset + SHA512_BLOCK_SIZE <= data_len)
        {
            sha512_block(&hash_state, verify_buffer + src_offset);
            src_offset += SHA512_BLOCK_SIZE;
        }

        if (src_offset < data_len)
        {
            leftover_len = data_len - src_offset;
            memcpy(leftover_buf, verify_buffer + src_offset, leftover_len);
        }

        total_read += data_len;
    }

    sha512_final(&hash_state, leftover_buf, (size_t)file_size);

    FS_CLOSE(fs, &data_file);

    sha512_get(&hash_state, file_hash, 0, SHA512_HASH_SIZE);

    uint8_t result = edsign_verify(signature, config->public_key, file_hash, SHA512_HASH_SIZE);
    return result ? ED25519_VERIFY_OK : ED25519_VERIFY_ERR_FAILED;
}

const char *ed25519_verify_err_to_string(int err)
{
    switch (err)
    {
    case ED25519_VERIFY_OK:
        return "Signature verification passed";
    case ED25519_VERIFY_ERR_PARAM:
        return "Invalid parameter";
    case ED25519_VERIFY_ERR_OPEN_FILE:
        return "Failed to open data file";
    case ED25519_VERIFY_ERR_OPEN_SIG:
        return "Failed to open signature file";
    case ED25519_VERIFY_ERR_READ_FILE:
        return "Failed to read data file";
    case ED25519_VERIFY_ERR_READ_SIG:
        return "Failed to read signature file";
    case ED25519_VERIFY_ERR_SIG_SIZE:
        return "Invalid signature file size (expected 64 bytes)";
    case ED25519_VERIFY_ERR_FAILED:
        return "Signature verification failed";
    case ED25519_VERIFY_ERR_PUBKEY:
        return "Invalid public key";
    default:
        return "Unknown error";
    }
}
