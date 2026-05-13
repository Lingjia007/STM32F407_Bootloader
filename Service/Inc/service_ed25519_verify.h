#ifndef SERVICE_ED25519_VERIFY_H
#define SERVICE_ED25519_VERIFY_H

#include "platform_filesystem.h"
#include <stdint.h>
#include <stddef.h>

#define ED25519_VERIFY_OK 0
#define ED25519_VERIFY_ERR_PARAM -1
#define ED25519_VERIFY_ERR_OPEN_FILE -2
#define ED25519_VERIFY_ERR_OPEN_SIG -3
#define ED25519_VERIFY_ERR_READ_FILE -4
#define ED25519_VERIFY_ERR_READ_SIG -5
#define ED25519_VERIFY_ERR_SIG_SIZE -6
#define ED25519_VERIFY_ERR_FAILED -7
#define ED25519_VERIFY_ERR_PUBKEY -8

typedef struct
{
    uint8_t public_key[32];
} ed25519_verify_config_t;

int ed25519_verify_buffer(const uint8_t *data, uint32_t size,
                          const uint8_t *signature, const uint8_t *public_key);

int ed25519_verify_file(platform_fs_base_t *fs, const char *file_path,
                        const char *sig_path, const ed25519_verify_config_t *config);

const char *ed25519_verify_err_to_string(int err);

#endif
