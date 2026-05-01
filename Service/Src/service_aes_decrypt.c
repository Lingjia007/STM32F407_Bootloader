#include "service_aes_decrypt.h"
#include "aes.h"
#include "platform_config.h"
#include <string.h>

#define DECRYPT_BUFFER_SIZE 4096

static uint8_t decrypt_buffer[DECRYPT_BUFFER_SIZE] __attribute__((aligned(4)));

int aes_decrypt_buffer(uint8_t *buffer, uint32_t size, const uint8_t *key, const uint8_t *iv)
{
    struct AES_ctx ctx;
    uint32_t padded_size;
    uint8_t padding_len;

    if (buffer == NULL || key == NULL || iv == NULL || size == 0)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    if (size % AES_BLOCKLEN != 0)
    {
        return AES_DECRYPT_ERR_SIZE;
    }

    AES_init_ctx_iv(&ctx, key, iv);
    AES_CBC_decrypt_buffer(&ctx, buffer, size);

    padding_len = buffer[size - 1];

    if (padding_len == 0 || padding_len > AES_BLOCKLEN)
    {
        return AES_DECRYPT_ERR_PADDING;
    }

    for (uint32_t i = size - padding_len; i < size; i++)
    {
        if (buffer[i] != padding_len)
        {
            return AES_DECRYPT_ERR_PADDING;
        }
    }

    padded_size = size - padding_len;

    return (int)padded_size;
}

int aes_decrypt_file(platform_fs_base_t *fs, const char *src_path, const char *dst_path, const aes_decrypt_config_t *config)
{
    platform_fs_file_t src_file;
    platform_fs_file_t dst_file;
    int32_t bytes_read;
    int32_t bytes_written;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    int32_t file_size;
    uint32_t encrypted_data_size;
    uint32_t decrypted_size;
    struct AES_ctx ctx;
    uint8_t *buffer_ptr;
    uint32_t buffer_size;
    uint8_t file_iv[AES_BLOCKLEN];

    if (fs == NULL || src_path == NULL || dst_path == NULL || config == NULL)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    if (FS_OPEN(fs, &src_file, src_path, FS_MODE_READ) != (int16_t)FS_STATUS_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = FS_SIZE(fs, &src_file);
    if (file_size <= AES_BLOCKLEN)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    bytes_read = FS_READ(fs, &src_file, file_iv, AES_BLOCKLEN);
    if (bytes_read != AES_BLOCKLEN)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = (uint32_t)file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    if (FS_OPEN(fs, &dst_file, dst_path, FS_MODE_CREATE_ALWAYS) != (int16_t)FS_STATUS_OK)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_OPEN_DST;
    }

    AES_init_ctx_iv(&ctx, config->key, file_iv);

    while (total_read < encrypted_data_size)
    {
        buffer_size = DECRYPT_BUFFER_SIZE;
        if (encrypted_data_size - total_read < buffer_size)
        {
            buffer_size = encrypted_data_size - total_read;
        }

        bytes_read = FS_READ(fs, &src_file, decrypt_buffer, buffer_size);
        if (bytes_read <= 0)
        {
            FS_CLOSE(fs, &dst_file);
            FS_CLOSE(fs, &src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, (uint32_t)bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = (uint32_t)bytes_read;

        if (total_read + (uint32_t)bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                FS_CLOSE(fs, &dst_file);
                FS_CLOSE(fs, &src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = (uint32_t)bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            bytes_written = FS_WRITE(fs, &dst_file, buffer_ptr, decrypted_size);
            if (bytes_written != (int32_t)decrypted_size)
            {
                FS_CLOSE(fs, &dst_file);
                FS_CLOSE(fs, &src_file);
                return AES_DECRYPT_ERR_WRITE;
            }
            total_written += (uint32_t)bytes_written;
        }

        total_read += (uint32_t)bytes_read;
    }

    FS_SYNC(fs, &dst_file);
    FS_CLOSE(fs, &dst_file);
    FS_CLOSE(fs, &src_file);

    return (int)total_written;
}

int aes_decrypt_to_flash(platform_fs_base_t *fs, const char *src_path, platform_transport_base_t *transport, const aes_decrypt_config_t *config)
{
    platform_fs_file_t src_file;
    int32_t bytes_read;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    int32_t file_size;
    uint32_t encrypted_data_size;
    uint32_t decrypted_size;
    struct AES_ctx ctx;
    uint8_t *buffer_ptr;
    uint32_t buffer_size;
    uint8_t file_iv[AES_BLOCKLEN];

    if (fs == NULL || src_path == NULL || transport == NULL || config == NULL)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    if (FS_OPEN(fs, &src_file, src_path, FS_MODE_READ) != (int16_t)FS_STATUS_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = FS_SIZE(fs, &src_file);
    if (file_size <= AES_BLOCKLEN)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    bytes_read = FS_READ(fs, &src_file, file_iv, AES_BLOCKLEN);
    if (bytes_read != AES_BLOCKLEN)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = (uint32_t)file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    if (TRANSPORT_TARGET_OPEN(transport, "application", encrypted_data_size) != (int16_t)TRANSPORT_STATUS_OK)
    {
        FS_CLOSE(fs, &src_file);
        return AES_DECRYPT_ERR_ERASE;
    }

    AES_init_ctx_iv(&ctx, config->key, file_iv);

    while (total_read < encrypted_data_size)
    {
        buffer_size = DECRYPT_BUFFER_SIZE;
        if (encrypted_data_size - total_read < buffer_size)
        {
            buffer_size = encrypted_data_size - total_read;
        }

        bytes_read = FS_READ(fs, &src_file, decrypt_buffer, buffer_size);
        if (bytes_read <= 0)
        {
            TRANSPORT_TARGET_CLOSE(transport);
            FS_CLOSE(fs, &src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, (uint32_t)bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = (uint32_t)bytes_read;

        if (total_read + (uint32_t)bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                TRANSPORT_TARGET_CLOSE(transport);
                FS_CLOSE(fs, &src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = (uint32_t)bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            if (TRANSPORT_TARGET_WRITE(transport, total_written, buffer_ptr, decrypted_size) != (int16_t)TRANSPORT_STATUS_OK)
            {
                TRANSPORT_TARGET_CLOSE(transport);
                FS_CLOSE(fs, &src_file);
                return AES_DECRYPT_ERR_FLASH_WRITE;
            }
            total_written += decrypted_size;
        }

        total_read += (uint32_t)bytes_read;
    }

    TRANSPORT_TARGET_CLOSE(transport);
    FS_CLOSE(fs, &src_file);

    return (int)total_written;
}
