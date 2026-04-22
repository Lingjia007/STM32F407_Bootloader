#ifndef __HPATCH_UPGRADE_H
#define __HPATCH_UPGRADE_H

#include "stm32f4xx_hal.h"
#include "hpatch_lite.h"
#include "tuz_dec.h"
#include "fatfs.h"
#include "lfs.h"

#define HPATCH_CACHE_SIZE (16 * 1024)
#define HPATCH_DICT_SIZE (4 * 1024)
#define HPATCH_STREAM_BUF_SIZE (8 * 1024)

#define HPATCH_MAX_PATH_LEN 128
#define HPATCH_MAX_FILENAME_LEN 128

typedef enum
{
    HPATCH_OK = 0,
    HPATCH_ERR_MOUNT = -1,
    HPATCH_ERR_OPEN_DIFF = -2,
    HPATCH_ERR_OPEN_OLD = -3,
    HPATCH_ERR_OPEN_OUT = -4,
    HPATCH_ERR_READ_DIFF = -5,
    HPATCH_ERR_READ_OLD = -6,
    HPATCH_ERR_WRITE = -7,
    HPATCH_ERR_DECOMPRESS = -8,
    HPATCH_ERR_PATCH = -9,
    HPATCH_ERR_INVALID_HEAD = -10,
    HPATCH_ERR_MEMORY = -11,
} hpatch_upgrade_err_t;

typedef struct
{
    FATFS *fatfs;
    char diff_path[HPATCH_MAX_PATH_LEN];
    char old_path[HPATCH_MAX_PATH_LEN];
    char out_path[HPATCH_MAX_PATH_LEN];
} hpatch_config_t;

typedef struct
{
    lfs_t *lfs;
    const char *diff_path;
    const char *old_path;
    const char *out_path;
} hpatch_lfs_config_t;

hpatch_upgrade_err_t hpatch_upgrade_fatfs(const hpatch_config_t *config);
hpatch_upgrade_err_t hpatch_upgrade_lfs(const hpatch_lfs_config_t *config);

#endif
