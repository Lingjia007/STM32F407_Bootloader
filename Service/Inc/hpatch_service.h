#ifndef HPATCH_SERVICE_H
#define HPATCH_SERVICE_H

#include "platform_filesystem.h"
#include <stdint.h>
#include <stddef.h>

#define HPATCH_CACHE_SIZE (16 * 1024)
#define HPATCH_DICT_SIZE (4 * 1024)
#define HPATCH_STREAM_BUF_SIZE (8 * 1024)

#define HPATCH_MAX_PATH_LEN 128

typedef enum
{
    HPATCH_OK = 0,
    HPATCH_ERR_OPEN_DIFF = -1,
    HPATCH_ERR_OPEN_OLD = -2,
    HPATCH_ERR_OPEN_OUT = -3,
    HPATCH_ERR_READ_DIFF = -4,
    HPATCH_ERR_READ_OLD = -5,
    HPATCH_ERR_WRITE = -6,
    HPATCH_ERR_DECOMPRESS = -7,
    HPATCH_ERR_PATCH = -8,
    HPATCH_ERR_INVALID_HEAD = -9,
    HPATCH_ERR_MEMORY = -10,
    HPATCH_ERR_PARAM = -11,
} hpatch_err_t;

typedef struct {
    platform_fs_base_t* fs;
    const char* diff_path;
    const char* old_path;
    const char* out_path;
} hpatch_config_t;

hpatch_err_t hpatch_upgrade(const hpatch_config_t* config);

const char* hpatch_err_to_string(hpatch_err_t err);

#endif
