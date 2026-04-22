#include "hpatch_upgrade.h"
#include <string.h>
#include <stdio.h>

static FIL diff_file;
static FIL old_file;
static FIL out_file;

static uint8_t tuz_dict_and_cache[HPATCH_DICT_SIZE + HPATCH_CACHE_SIZE];
static uint8_t decompressed_diff_buf[HPATCH_MAX_DIFF_DECOMP_SIZE];
static uint8_t patch_temp_buf[HPATCH_CACHE_SIZE];
static tuz_TStream tuz_stream_obj;

static hpi_pos_t decomp_read_pos = 0;
static hpi_pos_t decomp_total_size = 0;

static hpi_BOOL fatfs_read_raw(hpi_TInputStreamHandle inputStream, hpi_byte *out_data, hpi_size_t *data_size)
{
    UINT bytes_read;
    FRESULT res;
    res = f_read(&diff_file, out_data, (UINT)(*data_size), &bytes_read);
    if (res != FR_OK)
    {
        *data_size = 0;
        return hpi_FALSE;
    }
    *data_size = (hpi_size_t)bytes_read;
    return hpi_TRUE;
}

static hpi_BOOL mem_read_diff(hpi_TInputStreamHandle inputStream, hpi_byte *out_data, hpi_size_t *data_size)
{
    hpi_size_t remaining = (hpi_size_t)(decomp_total_size - decomp_read_pos);
    hpi_size_t request = *data_size;
    if (request > remaining)
        request = remaining;
    memcpy(out_data, decompressed_diff_buf + decomp_read_pos, request);
    decomp_read_pos += request;
    *data_size = request;
    return hpi_TRUE;
}

static hpi_BOOL fatfs_read_old(hpatchi_listener_t *listener, hpi_pos_t read_from_pos, hpi_byte *out_data, hpi_size_t data_size)
{
    UINT bytes_read;
    FRESULT res;
    res = f_lseek(&old_file, (FSIZE_t)read_from_pos);
    if (res != FR_OK)
        return hpi_FALSE;
    res = f_read(&old_file, out_data, data_size, &bytes_read);
    if (res != FR_OK || bytes_read != data_size)
        return hpi_FALSE;
    return hpi_TRUE;
}

static hpi_BOOL fatfs_write_new(hpatchi_listener_t *listener, const hpi_byte *data, hpi_size_t data_size)
{
    UINT bytes_written;
    FRESULT res;
    res = f_write(&out_file, data, data_size, &bytes_written);
    if (res != FR_OK || bytes_written != data_size)
        return hpi_FALSE;
    return hpi_TRUE;
}

static tuz_BOOL tuz_fatfs_read_code(tuz_TInputStreamHandle inputStream, tuz_byte *out_data, tuz_size_t *data_size)
{
    UINT bytes_read;
    FRESULT res;
    res = f_read(&diff_file, out_data, (UINT)(*data_size), &bytes_read);
    if (res != FR_OK)
    {
        *data_size = 0;
        return tuz_FALSE;
    }
    *data_size = (tuz_size_t)bytes_read;
    return tuz_TRUE;
}

hpatch_upgrade_err_t hpatch_upgrade_fatfs(const hpatch_config_t *config)
{
    FRESULT res;
    hpi_compressType compress_type;
    hpi_pos_t new_size;
    hpi_pos_t uncompress_size;
    hpatchi_listener_t listener;
    hpatch_upgrade_err_t ret = HPATCH_OK;
    tuz_size_t dict_size;
    tuz_TResult tuz_res;
    tuz_size_t decomp_size;
    hpi_pos_t total_decomped = 0;

    decomp_read_pos = 0;
    decomp_total_size = 0;

    res = f_open(&diff_file, config->diff_path, FA_READ);
    if (res != FR_OK)
        return HPATCH_ERR_OPEN_DIFF;

    res = f_open(&old_file, config->old_path, FA_READ);
    if (res != FR_OK)
    {
        f_close(&diff_file);
        return HPATCH_ERR_OPEN_OLD;
    }

    res = f_open(&out_file, config->out_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        f_close(&diff_file);
        f_close(&old_file);
        return HPATCH_ERR_OPEN_OUT;
    }

    listener.diff_data = (hpi_TInputStreamHandle)config->fatfs;
    listener.read_diff = fatfs_read_raw;
    listener.read_old = fatfs_read_old;
    listener.write_new = fatfs_write_new;

    if (!hpatch_lite_open(listener.diff_data, listener.read_diff,
                          &compress_type, &new_size, &uncompress_size))
    {
        ret = HPATCH_ERR_INVALID_HEAD;
        goto cleanup;
    }

    if (compress_type == hpi_compressType_tuz)
    {
        dict_size = tuz_TStream_read_dict_size((tuz_TInputStreamHandle)config->fatfs, tuz_fatfs_read_code);
        if (dict_size == 0 || dict_size > HPATCH_DICT_SIZE)
        {
            ret = HPATCH_ERR_DECOMPRESS;
            goto cleanup;
        }

        tuz_res = tuz_TStream_open(&tuz_stream_obj,
                                   (tuz_TInputStreamHandle)config->fatfs,
                                   tuz_fatfs_read_code,
                                   tuz_dict_and_cache,
                                   dict_size,
                                   HPATCH_CACHE_SIZE);

        if (tuz_res != tuz_OK)
        {
            ret = HPATCH_ERR_DECOMPRESS;
            goto cleanup;
        }

        if (uncompress_size > HPATCH_MAX_DIFF_DECOMP_SIZE)
        {
            ret = HPATCH_ERR_MEMORY;
            goto cleanup;
        }

        while (total_decomped < uncompress_size)
        {
            decomp_size = (tuz_size_t)(uncompress_size - total_decomped);
            if (decomp_size > HPATCH_CACHE_SIZE)
                decomp_size = HPATCH_CACHE_SIZE;

            tuz_res = tuz_TStream_decompress_partial(&tuz_stream_obj,
                                                     decompressed_diff_buf + total_decomped,
                                                     &decomp_size);

            if (tuz_res == tuz_READ_CODE_ERROR || tuz_res == tuz_DICT_POS_ERROR)
            {
                ret = HPATCH_ERR_DECOMPRESS;
                goto cleanup;
            }

            total_decomped += decomp_size;

            if (tuz_res == tuz_STREAM_END)
                break;
        }

        if (total_decomped < uncompress_size)
        {
            ret = HPATCH_ERR_DECOMPRESS;
            goto cleanup;
        }

        decomp_total_size = total_decomped;
        decomp_read_pos = 0;
        listener.diff_data = (hpi_TInputStreamHandle)0;
        listener.read_diff = mem_read_diff;
    }
    else if (compress_type != hpi_compressType_no)
    {
        ret = HPATCH_ERR_DECOMPRESS;
        goto cleanup;
    }

    if (!hpatch_lite_patch(&listener, new_size, patch_temp_buf, HPATCH_CACHE_SIZE))
    {
        ret = HPATCH_ERR_PATCH;
        goto cleanup;
    }

cleanup:
    f_close(&diff_file);
    f_close(&old_file);
    f_close(&out_file);

    return ret;
}
