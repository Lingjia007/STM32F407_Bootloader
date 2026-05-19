#include "platform_internal_flash_stm32_impl.h"
#include "ab_partition.h"
#include <string.h>
#include <stdio.h>

#define RELOCATE_SRC_START  SLOT_A_START_ADDR
#define RELOCATE_SRC_END    SLOT_A_END_ADDR

static int16_t internal_flash_init(void *ctx)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    (void)self;
    return INTERNAL_FLASH_STATUS_OK;
}

static int16_t internal_flash_deinit(void *ctx)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    HAL_FLASH_Lock();

    (void)self;
    return INTERNAL_FLASH_STATUS_OK;
}

static int16_t internal_flash_read(void *ctx, uint32_t addr, uint8_t *buffer, uint32_t size)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    if (buffer == NULL || size == 0)
    {
        return INTERNAL_FLASH_STATUS_INVALID_PARAM;
    }

    if (addr < self->flash_base.start_addr || addr + size > self->flash_base.end_addr + 1)
    {
        return INTERNAL_FLASH_STATUS_ADDRESS_ERROR;
    }

    memcpy(buffer, (void *)addr, size);

    return INTERNAL_FLASH_STATUS_OK;
}

static int16_t internal_flash_write(void *ctx, uint32_t addr, const uint8_t *buffer, uint32_t size)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    if (buffer == NULL || size == 0)
    {
        return INTERNAL_FLASH_STATUS_INVALID_PARAM;
    }

    if (addr < self->flash_base.start_addr || addr + size > self->flash_base.end_addr + 1)
    {
        return INTERNAL_FLASH_STATUS_ADDRESS_ERROR;
    }

    if ((addr % 4) != 0)
    {
        return INTERNAL_FLASH_STATUS_ADDRESS_ERROR;
    }

    HAL_FLASH_Unlock();

    uint32_t write_size = (size / 4) * 4;
    uint32_t i;

    for (i = 0; i < write_size; i += 4)
    {
        uint32_t data_word;
        memcpy(&data_word, buffer + i, 4);

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, addr + i, data_word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return INTERNAL_FLASH_STATUS_WRITE_ERROR;
        }

        if (*(uint32_t *)(addr + i) != data_word)
        {
            HAL_FLASH_Lock();
            return INTERNAL_FLASH_STATUS_VERIFY_ERROR;
        }
    }

    HAL_FLASH_Lock();

    return INTERNAL_FLASH_STATUS_OK;
}

static int16_t internal_flash_erase(void *ctx, uint32_t start_addr, uint32_t end_addr)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    if (start_addr >= end_addr)
    {
        return INTERNAL_FLASH_STATUS_INVALID_PARAM;
    }

    if (start_addr < self->flash_base.start_addr || end_addr > self->flash_base.end_addr)
    {
        return INTERNAL_FLASH_STATUS_ADDRESS_ERROR;
    }

    uint32_t start_sector = internal_flash_get_sector_by_addr(start_addr);
    uint32_t end_sector = internal_flash_get_sector_by_addr(end_addr);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    erase_init.TypeErase = TYPEERASE_SECTORS;
    erase_init.Sector = start_sector;
    erase_init.NbSectors = end_sector - start_sector + 1;
    erase_init.VoltageRange = VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return INTERNAL_FLASH_STATUS_ERASE_ERROR;
    }

    HAL_FLASH_Lock();

    return INTERNAL_FLASH_STATUS_OK;
}

static int16_t internal_flash_erase_sector(void *ctx, uint32_t sector)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);

    if (sector > FLASH_SECTOR_11)
    {
        return INTERNAL_FLASH_STATUS_INVALID_PARAM;
    }

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    erase_init.TypeErase = TYPEERASE_SECTORS;
    erase_init.Sector = sector;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return INTERNAL_FLASH_STATUS_ERASE_ERROR;
    }

    HAL_FLASH_Lock();

    (void)self;
    return INTERNAL_FLASH_STATUS_OK;
}

static uint32_t internal_flash_get_sector_impl(void *ctx, uint32_t addr)
{
    (void)ctx;
    return internal_flash_get_sector_by_addr(addr);
}

static uint32_t internal_flash_get_sector_size_impl(void *ctx, uint32_t sector)
{
    (void)ctx;
    return internal_flash_get_sector_size_by_index(sector);
}

static uint32_t internal_flash_get_total_size_impl(void *ctx)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, flash_base);
    return self->flash_base.total_size;
}

static uint16_t internal_flash_get_protection_status_impl(void *ctx)
{
    (void)ctx;

    FLASH_OBProgramInitTypeDef ob_config;

    HAL_FLASH_Unlock();
    HAL_FLASHEx_OBGetConfig(&ob_config);
    HAL_FLASH_Lock();

    uint16_t status = INTERNAL_FLASH_PROTECTION_NONE;

    if (ob_config.WRPState == OB_WRPSTATE_ENABLE)
    {
        status |= INTERNAL_FLASH_PROTECTION_WRP;
    }

    if (ob_config.RDPLevel != OB_RDP_LEVEL_0)
    {
        status |= INTERNAL_FLASH_PROTECTION_RDP;
    }

    return status;
}

static int16_t internal_flash_set_protection_impl(void *ctx, uint32_t sectors, uint8_t enable)
{
    (void)ctx;

    FLASH_OBProgramInitTypeDef ob_config;
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    HAL_FLASHEx_OBGetConfig(&ob_config);

    ob_config.OptionType = OPTIONBYTE_WRP;
    ob_config.WRPState = enable ? OB_WRPSTATE_ENABLE : OB_WRPSTATE_DISABLE;
    ob_config.WRPSector = sectors;
    ob_config.Banks = FLASH_BANK_1;

    status = HAL_FLASHEx_OBProgram(&ob_config);

    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    return (status == HAL_OK) ? INTERNAL_FLASH_STATUS_OK : INTERNAL_FLASH_STATUS_ERROR;
}

static const platform_internal_flash_ops_t internal_flash_ops = {
    .init = internal_flash_init,
    .deinit = internal_flash_deinit,
    .read = internal_flash_read,
    .write = internal_flash_write,
    .erase = internal_flash_erase,
    .erase_sector = internal_flash_erase_sector,
    .get_sector = internal_flash_get_sector_impl,
    .get_sector_size = internal_flash_get_sector_size_impl,
    .get_total_size = internal_flash_get_total_size_impl,
    .get_protection_status = internal_flash_get_protection_status_impl,
    .set_protection = internal_flash_set_protection_impl,
};

static int16_t internal_flash_tgt_open(const void *ctx, const char *path, uint32_t total_size)
{
    const internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, transport_base);
    (void)path;

    uint32_t start_sector, end_sector;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    ((internal_flash_stm32_t *)self)->written_size = 0;
    ((internal_flash_stm32_t *)self)->pending_len = 0;
    ((internal_flash_stm32_t *)self)->is_open = 0;
    ((internal_flash_stm32_t *)self)->is_erased = 0;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    if (total_size > 0)
    {
        start_sector = internal_flash_get_sector_by_addr(self->flash_base.start_addr);
        end_sector = internal_flash_get_sector_by_addr(self->flash_base.start_addr + total_size - 1);

        erase_init.TypeErase = TYPEERASE_SECTORS;
        erase_init.Sector = start_sector;
        erase_init.NbSectors = end_sector - start_sector + 1;
        erase_init.VoltageRange = VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
        {
            HAL_FLASH_Lock();
            printf("Internal flash erase failed\r\n");
            return TRANSPORT_STATUS_ERASE;
        }
    }

    ((internal_flash_stm32_t *)self)->is_open = 1;
    ((internal_flash_stm32_t *)self)->is_erased = 1;

    printf("Internal flash opened, addr=0x%08lX, size=%lu, relocate=0x%08lX\r\n",
           (unsigned long)self->flash_base.start_addr, (unsigned long)total_size,
           (unsigned long)self->relocate_offset);
    return TRANSPORT_STATUS_OK;
}

static uint32_t internal_flash_relocate_word(uint32_t val, uint32_t offset)
{
    if (offset == 0)
        return val;
    if (val >= RELOCATE_SRC_START && val <= RELOCATE_SRC_END)
        return val + offset;
    return val;
}

static void internal_flash_relocate_buffer(const internal_flash_stm32_t *self,
                                           uint8_t *buf, uint32_t len)
{
    if (self->relocate_offset == 0)
        return;

    uint32_t word_count = len / 4;
    for (uint32_t i = 0; i < word_count; i++)
    {
        uint32_t val;
        memcpy(&val, buf + i * 4, 4);
        uint32_t relocated = internal_flash_relocate_word(val, self->relocate_offset);
        if (relocated != val)
        {
            memcpy(buf + i * 4, &relocated, 4);
        }
    }
}

static int16_t internal_flash_tgt_write(const void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, transport_base);
    uint32_t i;
    uint32_t flash_addr;
    uint32_t data_word;
    uint32_t write_len;
    uint8_t temp_buf[4];
    uint8_t reloc_buf[1024];

    if (data == NULL || len == 0)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    if (!self->is_open)
    {
        printf("Internal flash not open\r\n");
        return TRANSPORT_STATUS_WRITE;
    }

    flash_addr = self->flash_base.start_addr + offset;

    if ((flash_addr % 4) != 0)
    {
        printf("Internal flash: write address not aligned 0x%08lX\r\n", (unsigned long)flash_addr);
        return TRANSPORT_STATUS_WRITE;
    }

    if (self->pending_len > 0)
    {
        uint32_t need = 4 - self->pending_len;
        if (len < need)
        {
            memcpy(self->pending_buf + self->pending_len, data, len);
            self->pending_len += len;
            return TRANSPORT_STATUS_OK;
        }

        memcpy(self->pending_buf + self->pending_len, data, need);
        data_word = *(uint32_t *)self->pending_buf;

        if (self->relocate_offset != 0)
        {
            data_word = internal_flash_relocate_word(data_word, self->relocate_offset);
        }

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, flash_addr, data_word) == HAL_OK)
        {
            if (*(uint32_t *)flash_addr != data_word)
            {
                printf("Internal flash verify failed at 0x%08lX\r\n", (unsigned long)flash_addr);
                return TRANSPORT_STATUS_VERIFY;
            }
        }
        else
        {
            printf("Internal flash program failed at 0x%08lX\r\n", (unsigned long)flash_addr);
            return TRANSPORT_STATUS_WRITE;
        }

        flash_addr += 4;
        data += need;
        len -= need;
        offset += need;
        self->pending_len = 0;
    }

    write_len = (len / 4) * 4;

    uint32_t processed = 0;
    while (processed < write_len)
    {
        uint32_t chunk = write_len - processed;
        if (chunk > sizeof(reloc_buf))
            chunk = sizeof(reloc_buf);
        chunk = (chunk / 4) * 4;

        memcpy(reloc_buf, data + processed, chunk);

        if (self->relocate_offset != 0)
        {
            internal_flash_relocate_buffer(self, reloc_buf, chunk);
        }

        for (i = 0; i < (chunk / 4); i++)
        {
            if (flash_addr > (INTERNAL_FLASH_END_ADDR - 4))
            {
                break;
            }

            memcpy(temp_buf, reloc_buf + (i * 4), 4);
            data_word = *(uint32_t *)temp_buf;

            if (HAL_FLASH_Program(TYPEPROGRAM_WORD, flash_addr, data_word) == HAL_OK)
            {
                if (*(uint32_t *)flash_addr != data_word)
                {
                    printf("Internal flash verify failed at 0x%08lX\r\n", (unsigned long)flash_addr);
                    return TRANSPORT_STATUS_VERIFY;
                }
                flash_addr += 4;
            }
            else
            {
                printf("Internal flash program failed at 0x%08lX\r\n", (unsigned long)flash_addr);
                return TRANSPORT_STATUS_WRITE;
            }
        }

        processed += chunk;
    }

    self->written_size = offset + write_len;

    if (len > write_len)
    {
        uint32_t remain = len - write_len;
        memcpy(self->pending_buf, data + write_len, remain);
        self->pending_len = remain;
    }

    return TRANSPORT_STATUS_OK;
}

static int16_t internal_flash_tgt_close(const void *ctx)
{
    internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, transport_base);

    if (!self->is_open)
    {
        return TRANSPORT_STATUS_OK;
    }

    if (self->pending_len > 0)
    {
        uint32_t flash_addr = self->flash_base.start_addr + self->written_size;
        uint32_t data_word = 0xFFFFFFFF;

        memcpy(&data_word, self->pending_buf, self->pending_len);

        if (self->relocate_offset != 0)
        {
            data_word = internal_flash_relocate_word(data_word, self->relocate_offset);
        }

        printf("Internal flash: flushing %d pending bytes at 0x%08lX\r\n",
               self->pending_len, (unsigned long)flash_addr);

        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, flash_addr, data_word) == HAL_OK)
        {
            if (*(uint32_t *)flash_addr != data_word)
            {
                HAL_FLASH_Lock();
                printf("Internal flash verify failed\r\n");
                return TRANSPORT_STATUS_VERIFY;
            }
        }
        else
        {
            HAL_FLASH_Lock();
            printf("Internal flash program failed\r\n");
            return TRANSPORT_STATUS_WRITE;
        }

        self->pending_len = 0;
    }

    HAL_FLASH_Lock();

    self->is_open = 0;
    printf("Internal flash closed\r\n");

    return TRANSPORT_STATUS_OK;
}

static int16_t internal_flash_tgt_read(const void *ctx, uint32_t offset, uint8_t *buf, uint32_t size, uint32_t *bytes_read)
{
    const internal_flash_stm32_t *self = container_of(ctx, internal_flash_stm32_t, transport_base);

    if (buf == NULL || size == 0)
    {
        return TRANSPORT_STATUS_PARAM;
    }

    uint32_t addr = self->flash_base.start_addr + offset;
    if (addr + size > self->flash_base.end_addr + 1)
    {
        return TRANSPORT_STATUS_READ;
    }

    memcpy(buf, (void *)addr, size);
    *bytes_read = size;

    return TRANSPORT_STATUS_OK;
}

static const platform_transport_target_ops_t internal_flash_target_ops = {
    .open = internal_flash_tgt_open,
    .write = internal_flash_tgt_write,
    .read = internal_flash_tgt_read,
    .close = internal_flash_tgt_close,
};

uint32_t internal_flash_get_sector_by_addr(uint32_t addr)
{
    if (addr < INTERNAL_FLASH_SECTOR_1_ADDR)
        return FLASH_SECTOR_0;
    else if (addr < INTERNAL_FLASH_SECTOR_2_ADDR)
        return FLASH_SECTOR_1;
    else if (addr < INTERNAL_FLASH_SECTOR_3_ADDR)
        return FLASH_SECTOR_2;
    else if (addr < INTERNAL_FLASH_SECTOR_4_ADDR)
        return FLASH_SECTOR_3;
    else if (addr < INTERNAL_FLASH_SECTOR_5_ADDR)
        return FLASH_SECTOR_4;
    else if (addr < INTERNAL_FLASH_SECTOR_6_ADDR)
        return FLASH_SECTOR_5;
    else if (addr < INTERNAL_FLASH_SECTOR_7_ADDR)
        return FLASH_SECTOR_6;
    else if (addr < INTERNAL_FLASH_SECTOR_8_ADDR)
        return FLASH_SECTOR_7;
    else if (addr < INTERNAL_FLASH_SECTOR_9_ADDR)
        return FLASH_SECTOR_8;
    else if (addr < INTERNAL_FLASH_SECTOR_10_ADDR)
        return FLASH_SECTOR_9;
    else if (addr < INTERNAL_FLASH_SECTOR_11_ADDR)
        return FLASH_SECTOR_10;
    else
        return FLASH_SECTOR_11;
}

uint32_t internal_flash_get_sector_size_by_index(uint32_t sector)
{
    switch (sector)
    {
    case FLASH_SECTOR_0:
    case FLASH_SECTOR_1:
    case FLASH_SECTOR_2:
    case FLASH_SECTOR_3:
        return INTERNAL_FLASH_SECTOR_SIZE_16K;
    case FLASH_SECTOR_4:
        return INTERNAL_FLASH_SECTOR_SIZE_64K;
    case FLASH_SECTOR_5:
    case FLASH_SECTOR_6:
    case FLASH_SECTOR_7:
    case FLASH_SECTOR_8:
    case FLASH_SECTOR_9:
    case FLASH_SECTOR_10:
    case FLASH_SECTOR_11:
        return INTERNAL_FLASH_SECTOR_SIZE_128K;
    default:
        return 0;
    }
}

void platform_internal_flash_stm32_register(internal_flash_stm32_t *flash,
                                            uint32_t start_addr,
                                            uint32_t end_addr,
                                            const char *name)
{
    if (flash == NULL)
    {
        return;
    }

    flash->flash_base.ops = &internal_flash_ops;
    flash->flash_base.name = name;
    flash->flash_base.start_addr = start_addr;
    flash->flash_base.end_addr = end_addr;
    flash->flash_base.total_size = end_addr - start_addr + 1;
    flash->flash_base.sector_size = 0;
    flash->flash_base.page_size = 4;
    flash->flash_base.user_data = NULL;

    flash->transport_base.source_ops = NULL;
    flash->transport_base.target_ops = &internal_flash_target_ops;
    flash->transport_base.name = name;
    flash->transport_base.type = TRANSPORT_TYPE_INTERNAL_FLASH;
    flash->transport_base.user_data = NULL;

    flash->written_size = 0;
    flash->relocate_offset = 0;
    flash->pending_len = 0;
    flash->is_open = 0;
    flash->is_erased = 0;
}
