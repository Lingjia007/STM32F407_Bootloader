#include "aes_decrypt.h"
#include "aes.h"
#include "flash_if.h"
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

int aes_decrypt_file_fatfs(FATFS *fs, const char *src_path, const char *dst_path, const aes_decrypt_config_t *config)
{
    FIL src_file;
    FIL dst_file;
    FRESULT res;
    UINT bytes_read;
    UINT bytes_written;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    uint32_t file_size;
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

    res = f_open(&src_file, src_path, FA_READ);
    if (res != FR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = f_size(&src_file);

    if (file_size <= AES_BLOCKLEN)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    res = f_read(&src_file, file_iv, AES_BLOCKLEN, &bytes_read);
    if (res != FR_OK || bytes_read != AES_BLOCKLEN)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    res = f_open(&dst_file, dst_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK)
    {
        f_close(&src_file);
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

        res = f_read(&src_file, decrypt_buffer, buffer_size, &bytes_read);
        if (res != FR_OK || bytes_read == 0)
        {
            f_close(&dst_file);
            f_close(&src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = bytes_read;

        if (total_read + bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                f_close(&dst_file);
                f_close(&src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            res = f_write(&dst_file, buffer_ptr, decrypted_size, &bytes_written);
            if (res != FR_OK || bytes_written != decrypted_size)
            {
                f_close(&dst_file);
                f_close(&src_file);
                return AES_DECRYPT_ERR_WRITE;
            }
            total_written += bytes_written;
        }

        total_read += bytes_read;
    }

    res = f_sync(&dst_file);
    if (res != FR_OK)
    {
        f_close(&dst_file);
        f_close(&src_file);
        return AES_DECRYPT_ERR_CLOSE;
    }

    res = f_close(&dst_file);
    if (res != FR_OK)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_CLOSE;
    }

    res = f_close(&src_file);
    if (res != FR_OK)
    {
        return AES_DECRYPT_ERR_CLOSE;
    }

    return (int)total_written;
}

int aes_decrypt_file_lfs(lfs_t *lfs, const char *src_path, const char *dst_path, const aes_decrypt_config_t *config)
{
    lfs_file_t src_file;
    lfs_file_t dst_file;
    int res;
    lfs_ssize_t bytes_read;
    lfs_ssize_t bytes_written;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    uint32_t file_size;
    uint32_t encrypted_data_size;
    uint32_t decrypted_size;
    struct AES_ctx ctx;
    uint8_t *buffer_ptr;
    uint32_t buffer_size;
    uint8_t file_iv[AES_BLOCKLEN];
    struct lfs_info info;

    if (lfs == NULL || src_path == NULL || dst_path == NULL || config == NULL)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    res = lfs_stat(lfs, src_path, &info);
    if (res != LFS_ERR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = info.size;

    if (file_size <= AES_BLOCKLEN)
    {
        return AES_DECRYPT_ERR_SIZE;
    }

    res = lfs_file_open(lfs, &src_file, src_path, LFS_O_RDONLY);
    if (res != LFS_ERR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    bytes_read = lfs_file_read(lfs, &src_file, file_iv, AES_BLOCKLEN);
    if (bytes_read != AES_BLOCKLEN)
    {
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    res = lfs_file_open(lfs, &dst_file, dst_path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (res != LFS_ERR_OK)
    {
        lfs_file_close(lfs, &src_file);
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

        bytes_read = lfs_file_read(lfs, &src_file, decrypt_buffer, buffer_size);
        if (bytes_read <= 0)
        {
            lfs_file_close(lfs, &dst_file);
            lfs_file_close(lfs, &src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = bytes_read;

        if (total_read + bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                lfs_file_close(lfs, &dst_file);
                lfs_file_close(lfs, &src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            bytes_written = lfs_file_write(lfs, &dst_file, buffer_ptr, decrypted_size);
            if (bytes_written != (lfs_ssize_t)decrypted_size)
            {
                lfs_file_close(lfs, &dst_file);
                lfs_file_close(lfs, &src_file);
                return AES_DECRYPT_ERR_WRITE;
            }
            total_written += bytes_written;
        }

        total_read += bytes_read;
    }

    res = lfs_file_sync(lfs, &dst_file);
    if (res != LFS_ERR_OK)
    {
        lfs_file_close(lfs, &dst_file);
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_CLOSE;
    }

    res = lfs_file_close(lfs, &dst_file);
    if (res != LFS_ERR_OK)
    {
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_CLOSE;
    }

    res = lfs_file_close(lfs, &src_file);
    if (res != LFS_ERR_OK)
    {
        return AES_DECRYPT_ERR_CLOSE;
    }

    return (int)total_written;
}

int aes_decrypt_to_flash_fatfs(FATFS *fs, const char *src_path, uint32_t flash_addr, const aes_decrypt_config_t *config)
{
    FIL src_file;
    FRESULT res;
    UINT bytes_read;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    uint32_t file_size;
    uint32_t encrypted_data_size;
    uint32_t decrypted_size;
    struct AES_ctx ctx;
    uint8_t *buffer_ptr;
    uint32_t buffer_size;
    uint8_t file_iv[AES_BLOCKLEN];
    uint32_t flash_offset = 0;
    uint32_t StartSector, EndSector;
    FLASH_EraseInitTypeDef pEraseInit;
    uint32_t SectorError;

    if (fs == NULL || src_path == NULL || config == NULL)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    res = f_open(&src_file, src_path, FA_READ);
    if (res != FR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = f_size(&src_file);

    if (file_size <= AES_BLOCKLEN)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    res = f_read(&src_file, file_iv, AES_BLOCKLEN, &bytes_read);
    if (res != FR_OK || bytes_read != AES_BLOCKLEN)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        f_close(&src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    StartSector = GetSector(flash_addr);
    EndSector = GetSector(flash_addr + encrypted_data_size - 1);

    pEraseInit.TypeErase = TYPEERASE_SECTORS;
    pEraseInit.Sector = StartSector;
    pEraseInit.NbSectors = EndSector - StartSector + 1;
    pEraseInit.VoltageRange = VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&pEraseInit, &SectorError) != HAL_OK)
    {
        HAL_FLASH_Lock();
        f_close(&src_file);
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

        res = f_read(&src_file, decrypt_buffer, buffer_size, &bytes_read);
        if (res != FR_OK || bytes_read == 0)
        {
            HAL_FLASH_Lock();
            f_close(&src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = bytes_read;

        if (total_read + bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                HAL_FLASH_Lock();
                f_close(&src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            uint32_t i;
            uint32_t FlashAddress = flash_addr + flash_offset;
            uint32_t *DataWord = (uint32_t *)buffer_ptr;

            for (i = 0; (i < (decrypted_size / 4)) && (FlashAddress <= (USER_FLASH_END_ADDRESS - 4)); i++)
            {
                if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord[i]) == HAL_OK)
                {
                    if (*(uint32_t *)FlashAddress != DataWord[i])
                    {
                        HAL_FLASH_Lock();
                        f_close(&src_file);
                        return AES_DECRYPT_ERR_FLASH_WRITE;
                    }
                    FlashAddress += 4;
                }
                else
                {
                    HAL_FLASH_Lock();
                    f_close(&src_file);
                    return AES_DECRYPT_ERR_FLASH_WRITE;
                }
            }

            total_written += (i * 4);
            flash_offset += (i * 4);
        }

        total_read += bytes_read;
    }

    HAL_FLASH_Lock();
    f_close(&src_file);

    return (int)total_written;
}

int aes_decrypt_to_flash_lfs(lfs_t *lfs, const char *src_path, uint32_t flash_addr, const aes_decrypt_config_t *config)
{
    lfs_file_t src_file;
    int res;
    lfs_ssize_t bytes_read;
    uint32_t total_read = 0;
    uint32_t total_written = 0;
    uint32_t file_size;
    uint32_t encrypted_data_size;
    uint32_t decrypted_size;
    struct AES_ctx ctx;
    uint8_t *buffer_ptr;
    uint32_t buffer_size;
    uint8_t file_iv[AES_BLOCKLEN];
    uint32_t flash_offset = 0;
    uint32_t StartSector, EndSector;
    FLASH_EraseInitTypeDef pEraseInit;
    uint32_t SectorError;
    struct lfs_info info;

    if (lfs == NULL || src_path == NULL || config == NULL)
    {
        return AES_DECRYPT_ERR_PARAM;
    }

    res = lfs_stat(lfs, src_path, &info);
    if (res != LFS_ERR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    file_size = info.size;

    if (file_size <= AES_BLOCKLEN)
    {
        return AES_DECRYPT_ERR_SIZE;
    }

    res = lfs_file_open(lfs, &src_file, src_path, LFS_O_RDONLY);
    if (res != LFS_ERR_OK)
    {
        return AES_DECRYPT_ERR_OPEN_SRC;
    }

    bytes_read = lfs_file_read(lfs, &src_file, file_iv, AES_BLOCKLEN);
    if (bytes_read != AES_BLOCKLEN)
    {
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_READ;
    }

    encrypted_data_size = file_size - AES_BLOCKLEN;

    if (encrypted_data_size % AES_BLOCKLEN != 0)
    {
        lfs_file_close(lfs, &src_file);
        return AES_DECRYPT_ERR_SIZE;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    StartSector = GetSector(flash_addr);
    EndSector = GetSector(flash_addr + encrypted_data_size - 1);

    pEraseInit.TypeErase = TYPEERASE_SECTORS;
    pEraseInit.Sector = StartSector;
    pEraseInit.NbSectors = EndSector - StartSector + 1;
    pEraseInit.VoltageRange = VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&pEraseInit, &SectorError) != HAL_OK)
    {
        HAL_FLASH_Lock();
        lfs_file_close(lfs, &src_file);
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

        bytes_read = lfs_file_read(lfs, &src_file, decrypt_buffer, buffer_size);
        if (bytes_read <= 0)
        {
            HAL_FLASH_Lock();
            lfs_file_close(lfs, &src_file);
            return AES_DECRYPT_ERR_READ;
        }

        AES_CBC_decrypt_buffer(&ctx, decrypt_buffer, bytes_read);

        buffer_ptr = decrypt_buffer;
        decrypted_size = bytes_read;

        if (total_read + bytes_read == encrypted_data_size)
        {
            uint8_t padding_len = decrypt_buffer[bytes_read - 1];

            if (padding_len == 0 || padding_len > AES_BLOCKLEN)
            {
                HAL_FLASH_Lock();
                lfs_file_close(lfs, &src_file);
                return AES_DECRYPT_ERR_PADDING;
            }

            decrypted_size = bytes_read - padding_len;
        }

        if (decrypted_size > 0)
        {
            uint32_t i;
            uint32_t FlashAddress = flash_addr + flash_offset;
            uint32_t *DataWord = (uint32_t *)buffer_ptr;

            for (i = 0; (i < (decrypted_size / 4)) && (FlashAddress <= (USER_FLASH_END_ADDRESS - 4)); i++)
            {
                if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord[i]) == HAL_OK)
                {
                    if (*(uint32_t *)FlashAddress != DataWord[i])
                    {
                        HAL_FLASH_Lock();
                        lfs_file_close(lfs, &src_file);
                        return AES_DECRYPT_ERR_FLASH_WRITE;
                    }
                    FlashAddress += 4;
                }
                else
                {
                    HAL_FLASH_Lock();
                    lfs_file_close(lfs, &src_file);
                    return AES_DECRYPT_ERR_FLASH_WRITE;
                }
            }

            total_written += (i * 4);
            flash_offset += (i * 4);
        }

        total_read += bytes_read;
    }

    HAL_FLASH_Lock();
    lfs_file_close(lfs, &src_file);

    return (int)total_written;
}
