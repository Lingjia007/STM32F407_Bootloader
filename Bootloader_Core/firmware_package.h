#ifndef FIRMWARE_PACKAGE_H
#define FIRMWARE_PACKAGE_H

#include <stdint.h>
#include <stddef.h>
#include "platform_transport.h"
#include "aes.h"
#include "sha512.h"

#define FW_PKG_MAGIC 0x01504149
#define FW_PKG_HEADER_VERSION 1
#define FW_PKG_HEADER_SIZE 64
#define FW_PKG_SALT_SIZE 16
#define FW_PKG_IV_SIZE 16
#define FW_PKG_SIGNATURE_SIZE 64
#define FW_PKG_HMAC_SIZE 32
#define FW_PKG_RESERVED_SIZE 5
#define FW_PKG_AES_KEY_SIZE 32
#define FW_PKG_DEVKEY_SIZE 16
#define FW_PKG_UID_SIZE 12
#define FW_PKG_ED25519_PUB_SIZE 32
#define FW_PKG_DECRYPT_BUF_SIZE 4096

#define STM32F4_UID_ADDR 0x1FFF7A10

static const uint8_t FW_PKG_ED25519_PUBLIC_KEY[32] = {
    0x64, 0x5d, 0x28, 0x3b, 0x17, 0xa8, 0x56, 0x07,
    0x38, 0x21, 0x00, 0xe9, 0x59, 0xea, 0x55, 0x42,
    0xbe, 0x57, 0x7f, 0xcb, 0x7d, 0x98, 0x46, 0xcc,
    0xbd, 0xa0, 0xe6, 0xb1, 0x1b, 0xdb, 0x87, 0x89};

#define FW_PKG_OVERHEAD (FW_PKG_HEADER_SIZE + FW_PKG_SALT_SIZE + FW_PKG_SIGNATURE_SIZE)

typedef enum
{
    FW_PKG_IMAGE_APP = 0x01,
    FW_PKG_IMAGE_BOOTLOADER = 0x02,
    FW_PKG_IMAGE_RESOURCE = 0x03,
} fw_pkg_image_type_t;

typedef enum
{
    FW_PKG_ENC_NONE = 0x00,
    FW_PKG_ENC_AES256_CBC = 0x01,
    FW_PKG_ENC_AES256_ECB = 0x02,
    FW_PKG_ENC_AES256_CTR = 0x03,
} fw_pkg_enc_algo_t;

typedef enum
{
    FW_PKG_SIG_NONE = 0x00,
    FW_PKG_SIG_ED25519 = 0x01,
} fw_pkg_sig_algo_t;

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint8_t header_version;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint8_t firmware_patch;
    uint32_t total_payload_size;
    uint8_t image_type;
    uint8_t encryption_algo;
    uint8_t signature_algo;
    uint32_t hardware_compat;
    uint32_t security_counter;
    uint32_t build_timestamp;
    uint8_t reserved[FW_PKG_RESERVED_SIZE];
    uint8_t header_checksum[FW_PKG_HMAC_SIZE];
} fw_pkg_header_t;

typedef enum
{
    FW_PKG_OK = 0,
    FW_PKG_ERR_MAGIC = -1,
    FW_PKG_ERR_HEADER_VERSION = -2,
    FW_PKG_ERR_HMAC = -3,
    FW_PKG_ERR_SIGNATURE = -4,
    FW_PKG_ERR_DECRYPT = -5,
    FW_PKG_ERR_ROLLBACK = -6,
    FW_PKG_ERR_HW_COMPAT = -7,
    FW_PKG_ERR_SIZE = -8,
    FW_PKG_ERR_PARAM = -9,
    FW_PKG_ERR_READ = -10,
    FW_PKG_ERR_WRITE = -11,
    FW_PKG_ERR_ERASE = -12,
    FW_PKG_ERR_UNSUPPORTED = -13,
} fw_pkg_err_t;

typedef struct
{
    const uint8_t *devkey;
    size_t devkey_len;
    const uint8_t *uid;
    size_t uid_len;
    const uint8_t *ed25519_pubkey;
    size_t ed25519_pubkey_len;
    uint32_t hardware_compat;
    uint32_t stored_security_counter;
} fw_pkg_verify_config_t;

typedef struct
{
    fw_pkg_header_t header;
    uint8_t dynamic_salt[FW_PKG_SALT_SIZE];
    uint8_t iv[FW_PKG_IV_SIZE];
    uint8_t signature[FW_PKG_SIGNATURE_SIZE];
    uint32_t ciphertext_size;
    struct AES_ctx aes_ctx;
    uint8_t aes_ctx_initialized;
    fw_pkg_err_t last_error;
} fw_pkg_ctx_t;

void fw_pkg_sha512_feed(struct sha512_state *state,
                        const uint8_t *data, size_t len,
                        uint8_t *pending, size_t *pending_len);

void fw_pkg_sha512_finish(struct sha512_state *state,
                          uint8_t *pending, size_t pending_len,
                          size_t total_stream_size,
                          uint8_t hash[64]);

fw_pkg_err_t fw_pkg_parse_header(fw_pkg_ctx_t *ctx,
                                 const uint8_t *data, size_t len);

fw_pkg_err_t fw_pkg_verify_header_hmac(const fw_pkg_ctx_t *ctx,
                                       const fw_pkg_verify_config_t *config);

fw_pkg_err_t fw_pkg_derive_aes_key(const fw_pkg_ctx_t *ctx,
                                   const fw_pkg_verify_config_t *config,
                                   uint8_t aes_key[FW_PKG_AES_KEY_SIZE]);

fw_pkg_err_t fw_pkg_verify_signature(const fw_pkg_ctx_t *ctx,
                                     const fw_pkg_verify_config_t *config,
                                     const uint8_t *header_and_payload,
                                     size_t header_and_payload_len);

fw_pkg_err_t fw_pkg_verify_signature_hash(const fw_pkg_ctx_t *ctx,
                                          const fw_pkg_verify_config_t *config,
                                          const uint8_t hash[64]);

fw_pkg_err_t fw_pkg_check_rollback(const fw_pkg_ctx_t *ctx,
                                   const fw_pkg_verify_config_t *config);

fw_pkg_err_t fw_pkg_decrypt_init(fw_pkg_ctx_t *ctx,
                                 const uint8_t *aes_key);

fw_pkg_err_t fw_pkg_decrypt_payload(fw_pkg_ctx_t *ctx,
                                    uint8_t *ciphertext,
                                    size_t ciphertext_len);

fw_pkg_err_t fw_pkg_process(const platform_transport_base_t *src_transport,
                            const platform_transport_base_t *tgt_transport,
                            const char *path,
                            const fw_pkg_verify_config_t *config);

const char *fw_pkg_err_str(fw_pkg_err_t err);

#endif
