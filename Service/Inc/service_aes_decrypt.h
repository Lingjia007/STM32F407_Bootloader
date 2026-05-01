#ifndef SERVICE_AES_DECRYPT_H
#define SERVICE_AES_DECRYPT_H

#include "platform_filesystem.h"
#include "platform_transport.h"
#include <stdint.h>

#define AES_DECRYPT_OK 0
#define AES_DECRYPT_ERR_PARAM -1
#define AES_DECRYPT_ERR_OPEN_SRC -2
#define AES_DECRYPT_ERR_OPEN_DST -3
#define AES_DECRYPT_ERR_READ -4
#define AES_DECRYPT_ERR_WRITE -5
#define AES_DECRYPT_ERR_CLOSE -6
#define AES_DECRYPT_ERR_SIZE -7
#define AES_DECRYPT_ERR_PADDING -8
#define AES_DECRYPT_ERR_ERASE -9
#define AES_DECRYPT_ERR_FLASH_WRITE -10

typedef struct
{
    uint8_t key[32];
    uint8_t iv[16];
} aes_decrypt_config_t;

int aes_decrypt_file(platform_fs_base_t *fs, const char *src_path, const char *dst_path, const aes_decrypt_config_t *config);

int aes_decrypt_to_flash(platform_fs_base_t *fs, const char *src_path, platform_transport_base_t *transport, const aes_decrypt_config_t *config);

int aes_decrypt_buffer(uint8_t *buffer, uint32_t size, const uint8_t *key, const uint8_t *iv);

#endif
