#ifndef AB_PARTITION_H
#define AB_PARTITION_H

#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx_hal.h"

#define SLOT_A_START_ADDR   0x08020000
#define SLOT_A_END_ADDR     0x0807FFFF
#define SLOT_A_SIZE         (384 * 1024)

#define SLOT_B_START_ADDR   0x08080000
#define SLOT_B_END_ADDR     0x080DFFFF
#define SLOT_B_SIZE         (384 * 1024)

#define DOWNLOAD_CACHE_ADDR 0x080E0000
#define DOWNLOAD_CACHE_SIZE (64 * 1024)

#define METADATA_ADDR       0x080F0000
#define METADATA_SIZE       (64 * 1024)

#define AB_METADATA_MAGIC       0x41425441
#define AB_METADATA_VERSION     1

#define AB_BOOT_CONFIRM_MAGIC   0x424F4F54
#define AB_MAX_BOOT_RETRIES     3

typedef enum {
    AB_SLOT_A = 0,
    AB_SLOT_B = 1,
    AB_SLOT_AUTO = 0xFE,
    AB_SLOT_NONE = 0xFF,
} ab_slot_t;

typedef enum {
    AB_STATE_IDLE = 0,
    AB_STATE_TESTING = 1,
    AB_STATE_CONFIRMED = 2,
    AB_STATE_INVALID = 0xFF,
} ab_slot_state_t;

typedef struct __attribute__((packed)) {
    uint32_t fw_version;
    uint32_t security_counter;
    uint32_t fw_size;
    ab_slot_state_t state;
    uint8_t boot_attempts;
    uint8_t reserved[3];
} ab_slot_meta_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    ab_slot_t active_slot;
    ab_slot_meta_t slots[2];
    uint32_t crc32;
} ab_metadata_t;

typedef enum {
    AB_OK = 0,
    AB_ERR_PARAM = -1,
    AB_ERR_METADATA_INVALID = -2,
    AB_ERR_METADATA_CRC = -3,
    AB_ERR_FLASH_READ = -4,
    AB_ERR_FLASH_WRITE = -5,
    AB_ERR_FLASH_ERASE = -6,
    AB_ERR_NO_VALID_SLOT = -7,
    AB_ERR_SLOT_INVALID_FW = -8,
} ab_err_t;

ab_err_t ab_partition_init(void);
ab_slot_t ab_partition_get_active_slot_from_flash(void);
ab_slot_t ab_partition_get_active_slot(void);
ab_slot_t ab_partition_get_inactive_slot(void);
uint32_t ab_partition_get_slot_addr(ab_slot_t slot);
uint32_t ab_partition_get_slot_size(ab_slot_t slot);
uint32_t ab_partition_get_slot_end_addr(ab_slot_t slot);
ab_err_t ab_partition_set_active_slot(ab_slot_t slot);
ab_err_t ab_partition_mark_slot_confirmed(ab_slot_t slot);
ab_err_t ab_partition_increment_boot_attempts(ab_slot_t slot);
ab_err_t ab_partition_reset_boot_attempts(ab_slot_t slot);
ab_err_t ab_partition_update_slot_meta(ab_slot_t slot, uint32_t fw_version,
                                       uint32_t security_counter, uint32_t fw_size);
ab_err_t ab_partition_rollback(void);
ab_err_t ab_partition_validate_slot(ab_slot_t slot);
ab_err_t ab_partition_metadata_flush(void);
const ab_metadata_t *ab_partition_get_metadata(void);
const char *ab_err_str(ab_err_t err);
const char *ab_slot_name(ab_slot_t slot);

#endif
