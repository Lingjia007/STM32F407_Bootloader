#include "service_hpatch.h"
#include "hpatch_lite.h"
#include "tuz_dec.h"
#include <string.h>
#include <stdio.h>

static platform_fs_file_t diff_file;
static platform_fs_file_t old_file;
static platform_fs_file_t out_file;
static platform_fs_base_t* fs_instance = NULL;

static uint8_t tuz_dict_and_cache[HPATCH_DICT_SIZE + HPATCH_CACHE_SIZE] __attribute__((aligned(4)));
static uint8_t stream_diff_buf[HPATCH_STREAM_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t patch_temp_buf[HPATCH_CACHE_SIZE] __attribute__((aligned(4)));
static tuz_TStream tuz_stream_obj;

static hpi_pos_t stream_buf_start = 0;
static hpi_pos_t stream_buf_end = 0;
static hpi_pos_t stream_read_pos = 0;
static tuz_BOOL stream_decomp_end = tuz_FALSE;

const char* hpatch_err_to_string(hpatch_err_t err)
{
    switch (err)
    {
        case HPATCH_OK:
            return "Success";
        case HPATCH_ERR_OPEN_DIFF:
            return "Failed to open diff file";
        case HPATCH_ERR_OPEN_OLD:
            return "Failed to open old file";
        case HPATCH_ERR_OPEN_OUT:
            return "Failed to create output file";
        case HPATCH_ERR_READ_DIFF:
            return "Failed to read diff file";
        case HPATCH_ERR_READ_OLD:
            return "Failed to read old file";
        case HPATCH_ERR_WRITE:
            return "Failed to write output file";
        case HPATCH_ERR_DECOMPRESS:
            return "Decompression error";
        case HPATCH_ERR_PATCH:
            return "Patch application error";
        case HPATCH_ERR_INVALID_HEAD:
            return "Invalid diff file header";
        case HPATCH_ERR_MEMORY:
            return "Memory allocation error";
        case HPATCH_ERR_PARAM:
            return "Invalid parameter";
        default:
            return "Unknown error";
    }
}

static hpi_BOOL fs_read_raw(hpi_TInputStreamHandle inputStream, hpi_byte* out_data, hpi_size_t* data_size)
{
    int32_t bytes_read = FS_READ(fs_instance, &diff_file, out_data, (uint32_t)(*data_size));
    if (bytes_read < 0)
    {
        *data_size = 0;
        return hpi_FALSE;
    }
    *data_size = (hpi_size_t)bytes_read;
    return hpi_TRUE;
}

static hpi_BOOL stream_refill_buffer(void)
{
    tuz_size_t decomp_size;
    tuz_TResult tuz_res;

    if (stream_decomp_end)
        return hpi_TRUE;

    stream_buf_start = 0;
    stream_buf_end = 0;

    decomp_size = HPATCH_STREAM_BUF_SIZE;
    tuz_res = tuz_TStream_decompress_partial(&tuz_stream_obj, stream_diff_buf, &decomp_size);

    if (tuz_res == tuz_READ_CODE_ERROR || tuz_res == tuz_DICT_POS_ERROR)
        return hpi_FALSE;

    stream_buf_end = decomp_size;

    if (tuz_res == tuz_STREAM_END)
        stream_decomp_end = tuz_TRUE;

    return hpi_TRUE;
}

static hpi_BOOL stream_read_diff(hpi_TInputStreamHandle inputStream, hpi_byte* out_data, hpi_size_t* data_size)
{
    hpi_size_t request = *data_size;
    hpi_size_t copied = 0;

    while (copied < request)
    {
        if (stream_buf_start >= stream_buf_end)
        {
            if (stream_decomp_end)
                break;
            if (!stream_refill_buffer())
            {
                *data_size = copied;
                return hpi_FALSE;
            }
            if (stream_buf_end == 0)
                break;
        }

        hpi_size_t available = (hpi_size_t)(stream_buf_end - stream_buf_start);
        hpi_size_t to_copy = request - copied;
        if (to_copy > available)
            to_copy = available;

        memcpy(out_data + copied, stream_diff_buf + stream_buf_start, to_copy);
        stream_buf_start += to_copy;
        stream_read_pos += to_copy;
        copied += to_copy;
    }

    *data_size = copied;
    return (copied > 0 || request == 0) ? hpi_TRUE : hpi_FALSE;
}

static hpi_BOOL fs_read_old(hpatchi_listener_t* listener, hpi_pos_t read_from_pos, hpi_byte* out_data, hpi_size_t data_size)
{
    int32_t seek_res = FS_SEEK(fs_instance, &old_file, (int32_t)read_from_pos, FS_SEEK_SET);
    if (seek_res < 0)
        return hpi_FALSE;

    int32_t bytes_read = FS_READ(fs_instance, &old_file, out_data, (uint32_t)data_size);
    if (bytes_read != (int32_t)data_size)
        return hpi_FALSE;

    return hpi_TRUE;
}

static hpi_BOOL fs_write_new(hpatchi_listener_t* listener, const hpi_byte* data, hpi_size_t data_size)
{
    int32_t bytes_written = FS_WRITE(fs_instance, &out_file, data, (uint32_t)data_size);
    if (bytes_written != (int32_t)data_size)
        return hpi_FALSE;
    return hpi_TRUE;
}

static tuz_BOOL tuz_fs_read_code(tuz_TInputStreamHandle inputStream, tuz_byte* out_data, tuz_size_t* data_size)
{
    int32_t bytes_read = FS_READ(fs_instance, &diff_file, out_data, (uint32_t)(*data_size));
    if (bytes_read < 0)
    {
        *data_size = 0;
        return tuz_FALSE;
    }
    *data_size = (tuz_size_t)bytes_read;
    return tuz_TRUE;
}

hpatch_err_t hpatch_upgrade(const hpatch_config_t* config)
{
    hpi_compressType compress_type;
    hpi_pos_t new_size;
    hpi_pos_t uncompress_size;
    hpatchi_listener_t listener;
    hpatch_err_t ret = HPATCH_OK;
    tuz_size_t dict_size;
    tuz_TResult tuz_res;
    int16_t fs_ret;

    if (config == NULL || config->fs == NULL)
        return HPATCH_ERR_PARAM;

    stream_buf_start = 0;
    stream_buf_end = 0;
    stream_read_pos = 0;
    stream_decomp_end = tuz_FALSE;
    fs_instance = config->fs;

    fs_ret = FS_OPEN(fs_instance, &diff_file, config->diff_path, FS_MODE_READ);
    if (fs_ret != (int16_t)FS_STATUS_OK)
        return HPATCH_ERR_OPEN_DIFF;

    fs_ret = FS_OPEN(fs_instance, &old_file, config->old_path, FS_MODE_READ);
    if (fs_ret != (int16_t)FS_STATUS_OK)
    {
        FS_CLOSE(fs_instance, &diff_file);
        return HPATCH_ERR_OPEN_OLD;
    }

    fs_ret = FS_OPEN(fs_instance, &out_file, config->out_path, FS_MODE_CREATE_ALWAYS);
    if (fs_ret != (int16_t)FS_STATUS_OK)
    {
        FS_CLOSE(fs_instance, &diff_file);
        FS_CLOSE(fs_instance, &old_file);
        return HPATCH_ERR_OPEN_OUT;
    }

    listener.diff_data = (hpi_TInputStreamHandle)fs_instance;
    listener.read_diff = fs_read_raw;
    listener.read_old = fs_read_old;
    listener.write_new = fs_write_new;

    if (!hpatch_lite_open(listener.diff_data, listener.read_diff,
                          &compress_type, &new_size, &uncompress_size))
    {
        ret = HPATCH_ERR_INVALID_HEAD;
        goto cleanup;
    }

    if (compress_type == hpi_compressType_tuz)
    {
        dict_size = tuz_TStream_read_dict_size((tuz_TInputStreamHandle)fs_instance, tuz_fs_read_code);
        if (dict_size == 0 || dict_size > HPATCH_DICT_SIZE)
        {
            ret = HPATCH_ERR_DECOMPRESS;
            goto cleanup;
        }

        tuz_res = tuz_TStream_open(&tuz_stream_obj,
                                   (tuz_TInputStreamHandle)fs_instance,
                                   tuz_fs_read_code,
                                   tuz_dict_and_cache,
                                   dict_size,
                                   HPATCH_CACHE_SIZE);

        if (tuz_res != tuz_OK)
        {
            ret = HPATCH_ERR_DECOMPRESS;
            goto cleanup;
        }

        stream_buf_start = 0;
        stream_buf_end = 0;
        stream_read_pos = 0;
        stream_decomp_end = tuz_FALSE;

        listener.diff_data = (hpi_TInputStreamHandle)0;
        listener.read_diff = stream_read_diff;
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
    FS_CLOSE(fs_instance, &diff_file);
    FS_CLOSE(fs_instance, &old_file);
    FS_CLOSE(fs_instance, &out_file);
    fs_instance = NULL;

    return ret;
}
