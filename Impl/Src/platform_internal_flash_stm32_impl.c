#include "platform_internal_flash_stm32_impl.h"
#include "flash_if.h"
#include <string.h>
#include <stdio.h>

static int16_t internal_flash_tgt_open(const void* ctx, const char* path, uint32_t total_size)
{
    internal_flash_stm32_t* self = container_of(ctx, internal_flash_stm32_t, base);
    (void)path;

    uint32_t StartSector, EndSector;
    FLASH_EraseInitTypeDef pEraseInit;
    uint32_t SectorError;

    self->total_size = total_size;
    self->written_size = 0;
    self->pending_len = 0;
    self->is_open = 0;
    self->is_erased = 0;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    if (total_size > 0)
    {
        StartSector = GetSector(self->start_addr);
        EndSector = GetSector(self->start_addr + total_size - 1);

        pEraseInit.TypeErase = TYPEERASE_SECTORS;
        pEraseInit.Sector = StartSector;
        pEraseInit.NbSectors = EndSector - StartSector + 1;
        pEraseInit.VoltageRange = VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&pEraseInit, &SectorError) != HAL_OK)
        {
            HAL_FLASH_Lock();
            printf("Internal flash erase failed\r\n");
            return STORAGE_STATUS_ERASE;
        }
    }

    self->is_open = 1;
    self->is_erased = 1;

    printf("Internal flash opened, addr=0x%08lX, size=%lu\r\n", 
           (unsigned long)self->start_addr, (unsigned long)total_size);
    return STORAGE_STATUS_OK;
}

static int16_t internal_flash_tgt_write(const void* ctx, uint32_t offset, const uint8_t* data, uint32_t len)
{
    internal_flash_stm32_t* self = container_of(ctx, internal_flash_stm32_t, base);
    uint32_t i;
    uint32_t FlashAddress;
    uint32_t DataWord;
    uint32_t write_len;
    uint8_t temp_buf[4];

    if (data == NULL || len == 0)
    {
        return STORAGE_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("Internal flash not open\r\n");
        return STORAGE_STATUS_WRITE;
    }

    FlashAddress = self->start_addr + offset;

    if ((FlashAddress % 4) != 0)
    {
        printf("Internal flash: write address not aligned 0x%08lX\r\n", (unsigned long)FlashAddress);
        return STORAGE_STATUS_WRITE;
    }

    if (self->pending_len > 0)
    {
        uint32_t need = 4 - self->pending_len;
        if (len < need)
        {
            memcpy(self->pending_buf + self->pending_len, data, len);
            self->pending_len += len;
            return STORAGE_STATUS_OK;
        }

        memcpy(self->pending_buf + self->pending_len, data, need);
        DataWord = *(uint32_t*)self->pending_buf;

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord) == HAL_OK)
        {
            if (*(uint32_t*)FlashAddress != DataWord)
            {
                printf("Internal flash verify failed at 0x%08lX\r\n", (unsigned long)FlashAddress);
                return STORAGE_STATUS_VERIFY;
            }
        }
        else
        {
            printf("Internal flash program failed at 0x%08lX\r\n", (unsigned long)FlashAddress);
            return STORAGE_STATUS_WRITE;
        }

        FlashAddress += 4;
        data += need;
        len -= need;
        self->pending_len = 0;
    }

    write_len = (len / 4) * 4;

    for (i = 0; i < (write_len / 4); i++)
    {
        if (FlashAddress > (INTERNAL_FLASH_END_ADDRESS - 4))
        {
            break;
        }

        memcpy(temp_buf, data + (i * 4), 4);
        DataWord = *(uint32_t*)temp_buf;

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord) == HAL_OK)
        {
            if (*(uint32_t*)FlashAddress != DataWord)
            {
                printf("Internal flash verify failed at 0x%08lX\r\n", (unsigned long)FlashAddress);
                return STORAGE_STATUS_VERIFY;
            }
            FlashAddress += 4;
        }
        else
        {
            printf("Internal flash program failed at 0x%08lX\r\n", (unsigned long)FlashAddress);
            return STORAGE_STATUS_WRITE;
        }
    }

    self->written_size = offset + write_len;

    if (len > write_len)
    {
        uint32_t remain = len - write_len;
        memcpy(self->pending_buf, data + write_len, remain);
        self->pending_len = remain;
    }

    return STORAGE_STATUS_OK;
}

static int16_t internal_flash_tgt_close(const void* ctx)
{
    internal_flash_stm32_t* self = container_of(ctx, internal_flash_stm32_t, base);

    if (!self->is_open)
    {
        return STORAGE_STATUS_OK;
    }

    if (self->pending_len > 0)
    {
        uint32_t FlashAddress = self->start_addr + self->written_size;
        uint32_t DataWord = 0xFFFFFFFF;

        memcpy(&DataWord, self->pending_buf, self->pending_len);

        printf("Internal flash: flushing %d pending bytes at 0x%08lX\r\n",
               self->pending_len, (unsigned long)FlashAddress);

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, FlashAddress, DataWord) == HAL_OK)
        {
            if (*(uint32_t*)FlashAddress != DataWord)
            {
                HAL_FLASH_Lock();
                printf("Internal flash verify failed\r\n");
                return STORAGE_STATUS_VERIFY;
            }
        }
        else
        {
            HAL_FLASH_Lock();
            printf("Internal flash program failed\r\n");
            return STORAGE_STATUS_WRITE;
        }

        self->pending_len = 0;
    }

    HAL_FLASH_Lock();

    self->is_open = 0;
    printf("Internal flash closed\r\n");

    return STORAGE_STATUS_OK;
}

static const platform_storage_target_ops_t internal_flash_target_ops = {
    .open = internal_flash_tgt_open,
    .write = internal_flash_tgt_write,
    .close = internal_flash_tgt_close,
};

internal_flash_stm32_t g_internal_flash = {
    .base = {
        .source_ops = NULL,
        .target_ops = &internal_flash_target_ops,
        .name = "internal_flash",
        .type = STORAGE_TYPE_INTERNAL_FLASH,
        .user_data = NULL,
    },
    .start_addr = INTERNAL_FLASH_APP_ADDRESS,
    .total_size = 0,
    .written_size = 0,
    .pending_len = 0,
    .is_open = 0,
    .is_erased = 0,
};
