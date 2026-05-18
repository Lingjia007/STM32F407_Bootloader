#include "ab_partition.h"
#include "platform_internal_flash_stm32_impl.h"
#include <string.h>
#include <stdio.h>

#define AB_META_INSTANCE_ALIGN 64
#define AB_META_MAX_INSTANCES (METADATA_SIZE / AB_META_INSTANCE_ALIGN)

static ab_metadata_t g_ab_metadata;
static uint8_t g_ab_initialized = 0;
static uint8_t g_ab_metadata_dirty = 0;
static int16_t g_ab_current_instance = -1;

static uint32_t ab_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

static uint32_t ab_compute_metadata_crc(const ab_metadata_t *meta)
{
    size_t crc_offset = offsetof(ab_metadata_t, crc32);
    return ab_crc32((const uint8_t *)meta, crc_offset);
}

static int ab_is_valid_sp(uint32_t sp)
{
    return (sp & 0x2FFE0000) == 0x20000000;
}

static int ab_is_valid_active_slot(ab_slot_t slot)
{
    return (slot == AB_SLOT_A || slot == AB_SLOT_B);
}

static int ab_validate_metadata(const ab_metadata_t *meta)
{
    if (meta->magic != AB_METADATA_MAGIC || meta->version != AB_METADATA_VERSION)
        return 0;

    if (meta->crc32 != ab_compute_metadata_crc(meta))
        return 0;

    if (!ab_is_valid_active_slot(meta->active_slot))
        return 0;

    return 1;
}

static int ab_find_latest_instance(void)
{
    for (int i = 0; i < AB_META_MAX_INSTANCES; i++)
    {
        uint32_t addr = METADATA_ADDR + (uint32_t)i * AB_META_INSTANCE_ALIGN;
        uint32_t magic = *(volatile uint32_t *)addr;
        if (magic != AB_METADATA_MAGIC)
            return (i > 0) ? (i - 1) : -1;
    }
    return AB_META_MAX_INSTANCES - 1;
}

static ab_err_t ab_read_metadata(void)
{
    g_ab_current_instance = ab_find_latest_instance();

    if (g_ab_current_instance < 0)
        return AB_ERR_METADATA_INVALID;

    for (int i = g_ab_current_instance; i >= 0; i--)
    {
        uint32_t addr = METADATA_ADDR + (uint32_t)i * AB_META_INSTANCE_ALIGN;
        memcpy(&g_ab_metadata, (void *)addr, sizeof(ab_metadata_t));

        if (!ab_validate_metadata(&g_ab_metadata))
        {
            printf("AB: instance %d validation failed\r\n", i);
            continue;
        }

        if (i != g_ab_current_instance)
            printf("AB: fallback to valid instance %d\r\n", i);

        g_ab_current_instance = i;
        return AB_OK;
    }

    return AB_ERR_METADATA_INVALID;
}

static ab_err_t ab_erase_metadata_sector(void)
{
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error;

    erase_init.TypeErase = TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_11;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        printf("AB: metadata erase failed\r\n");
        return AB_ERR_FLASH_ERASE;
    }

    HAL_FLASH_Lock();
    return AB_OK;
}

static ab_err_t ab_flash_write_words(uint32_t addr, const uint32_t *data, uint32_t word_count)
{
    HAL_FLASH_Unlock();

    for (uint32_t i = 0; i < word_count; i++)
    {
        if (HAL_FLASH_Program(TYPEPROGRAM_WORD, addr, data[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            printf("AB: flash write failed at 0x%08lX\r\n", (unsigned long)addr);
            return AB_ERR_FLASH_WRITE;
        }
        addr += 4;
    }

    HAL_FLASH_Lock();
    return AB_OK;
}

static ab_err_t ab_write_metadata_raw(const ab_metadata_t *meta)
{
    int next_instance = g_ab_current_instance + 1;

    if (next_instance >= AB_META_MAX_INSTANCES)
    {
        printf("AB: metadata area full, compacting...\r\n");
        ab_err_t err = ab_erase_metadata_sector();
        if (err != AB_OK)
            return err;
        next_instance = 0;
        g_ab_current_instance = -1;
    }

    uint32_t addr = METADATA_ADDR + (uint32_t)next_instance * AB_META_INSTANCE_ALIGN;
    ab_err_t err = ab_flash_write_words(addr, (const uint32_t *)meta,
                                        (sizeof(ab_metadata_t) + 3) / 4);
    if (err != AB_OK)
        return err;

    const ab_metadata_t *written = (const ab_metadata_t *)addr;
    if (written->magic != meta->magic ||
        written->version != meta->version ||
        written->active_slot != meta->active_slot ||
        written->crc32 != meta->crc32)
    {
        printf("AB: metadata write verification failed at instance %d\r\n", next_instance);
        return AB_ERR_FLASH_WRITE;
    }

    g_ab_current_instance = next_instance;
    printf("AB: metadata written at instance %d\r\n", next_instance);
    return AB_OK;
}

static void ab_init_default_metadata(void)
{
    memset(&g_ab_metadata, 0, sizeof(g_ab_metadata));
    g_ab_metadata.magic = AB_METADATA_MAGIC;
    g_ab_metadata.version = AB_METADATA_VERSION;
    g_ab_metadata.active_slot = AB_SLOT_A;

    g_ab_metadata.slots[AB_SLOT_A].state = AB_STATE_CONFIRMED;
    g_ab_metadata.slots[AB_SLOT_A].boot_attempts = 0;
    g_ab_metadata.slots[AB_SLOT_A].fw_version = 0;
    g_ab_metadata.slots[AB_SLOT_A].fw_size = 0;
    g_ab_metadata.slots[AB_SLOT_A].security_counter = 0;

    g_ab_metadata.slots[AB_SLOT_B].state = AB_STATE_INVALID;
    g_ab_metadata.slots[AB_SLOT_B].boot_attempts = 0;
    g_ab_metadata.slots[AB_SLOT_B].fw_version = 0;
    g_ab_metadata.slots[AB_SLOT_B].fw_size = 0;
    g_ab_metadata.slots[AB_SLOT_B].security_counter = 0;

    g_ab_metadata.crc32 = ab_compute_metadata_crc(&g_ab_metadata);
}

ab_err_t ab_partition_init(void)
{
    ab_err_t err = ab_read_metadata();
    if (err != AB_OK)
    {
        printf("AB: no valid metadata, initializing defaults\r\n");
        ab_init_default_metadata();
        g_ab_current_instance = -1;
        err = ab_write_metadata_raw(&g_ab_metadata);
        if (err != AB_OK)
            return err;
    }
    else
    {
        printf("AB: metadata valid, active=%s (instance %d)\r\n",
               ab_slot_name(g_ab_metadata.active_slot), g_ab_current_instance);
    }

    g_ab_initialized = 1;
    g_ab_metadata_dirty = 0;
    return AB_OK;
}

ab_slot_t ab_partition_get_active_slot_from_flash(void)
{
    int latest_instance = ab_find_latest_instance();

    if (latest_instance < 0)
        return AB_SLOT_A;

    for (int i = latest_instance; i >= 0; i--)
    {
        uint32_t addr = METADATA_ADDR + (uint32_t)i * AB_META_INSTANCE_ALIGN;
        const ab_metadata_t *meta = (const ab_metadata_t *)addr;

        if (!ab_validate_metadata(meta))
            continue;

        return meta->active_slot;
    }

    return AB_SLOT_A;
}

ab_slot_t ab_partition_get_active_slot(void)
{
    if (!g_ab_initialized || !ab_is_valid_active_slot(g_ab_metadata.active_slot))
        return AB_SLOT_A;
    return g_ab_metadata.active_slot;
}

ab_slot_t ab_partition_get_inactive_slot(void)
{
    if (!g_ab_initialized || !ab_is_valid_active_slot(g_ab_metadata.active_slot))
        return AB_SLOT_B;
    return (g_ab_metadata.active_slot == AB_SLOT_A) ? AB_SLOT_B : AB_SLOT_A;
}

uint32_t ab_partition_get_slot_addr(ab_slot_t slot)
{
    switch (slot)
    {
    case AB_SLOT_A:
        return SLOT_A_START_ADDR;
    case AB_SLOT_B:
        return SLOT_B_START_ADDR;
    default:
        return 0;
    }
}

uint32_t ab_partition_get_slot_size(ab_slot_t slot)
{
    switch (slot)
    {
    case AB_SLOT_A:
        return SLOT_A_SIZE;
    case AB_SLOT_B:
        return SLOT_B_SIZE;
    default:
        return 0;
    }
}

uint32_t ab_partition_get_slot_end_addr(ab_slot_t slot)
{
    switch (slot)
    {
    case AB_SLOT_A:
        return SLOT_A_END_ADDR;
    case AB_SLOT_B:
        return SLOT_B_END_ADDR;
    default:
        return 0;
    }
}

ab_err_t ab_partition_set_active_slot(ab_slot_t slot)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    g_ab_metadata.active_slot = slot;
    g_ab_metadata.slots[slot].state = AB_STATE_TESTING;
    g_ab_metadata.slots[slot].boot_attempts = 0;
    g_ab_metadata_dirty = 1;

    printf("AB: active slot set to %s (TESTING)\r\n", ab_slot_name(slot));
    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_mark_slot_confirmed(ab_slot_t slot)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    g_ab_metadata.slots[slot].state = AB_STATE_CONFIRMED;
    g_ab_metadata.slots[slot].boot_attempts = 0;
    g_ab_metadata_dirty = 1;

    printf("AB: slot %s marked CONFIRMED\r\n", ab_slot_name(slot));
    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_increment_boot_attempts(ab_slot_t slot)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    g_ab_metadata.slots[slot].boot_attempts++;
    g_ab_metadata_dirty = 1;

    printf("AB: slot %s boot_attempts=%d\r\n", ab_slot_name(slot),
           g_ab_metadata.slots[slot].boot_attempts);
    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_reset_boot_attempts(ab_slot_t slot)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    g_ab_metadata.slots[slot].boot_attempts = 0;
    g_ab_metadata_dirty = 1;
    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_update_slot_meta(ab_slot_t slot, uint32_t fw_version,
                                       uint32_t security_counter, uint32_t fw_size)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    g_ab_metadata.slots[slot].fw_version = fw_version;
    g_ab_metadata.slots[slot].security_counter = security_counter;
    g_ab_metadata.slots[slot].fw_size = fw_size;
    g_ab_metadata.slots[slot].state = AB_STATE_TESTING;
    g_ab_metadata.slots[slot].boot_attempts = 0;
    g_ab_metadata_dirty = 1;

    printf("AB: slot %s meta updated: ver=%lu, sec=%lu, size=%lu\r\n",
           ab_slot_name(slot),
           (unsigned long)fw_version,
           (unsigned long)security_counter,
           (unsigned long)fw_size);
    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_rollback(void)
{
    if (!g_ab_initialized)
        return AB_ERR_METADATA_INVALID;

    ab_slot_t current = g_ab_metadata.active_slot;
    ab_slot_t fallback = (current == AB_SLOT_A) ? AB_SLOT_B : AB_SLOT_A;

    printf("AB: rollback from %s to %s\r\n", ab_slot_name(current), ab_slot_name(fallback));

    g_ab_metadata.slots[current].state = AB_STATE_INVALID;
    g_ab_metadata.slots[current].boot_attempts = 0;

    uint32_t fallback_sp = (*(__IO uint32_t *)ab_partition_get_slot_addr(fallback));
    if (!ab_is_valid_sp(fallback_sp))
    {
        printf("AB: fallback slot %s has no valid firmware!\r\n", ab_slot_name(fallback));
        return AB_ERR_NO_VALID_SLOT;
    }

    g_ab_metadata.active_slot = fallback;
    if (g_ab_metadata.slots[fallback].state == AB_STATE_INVALID)
        g_ab_metadata.slots[fallback].state = AB_STATE_CONFIRMED;
    g_ab_metadata.slots[fallback].boot_attempts = 0;
    g_ab_metadata_dirty = 1;

    return ab_partition_metadata_flush();
}

ab_err_t ab_partition_validate_slot(ab_slot_t slot)
{
    if (slot != AB_SLOT_A && slot != AB_SLOT_B)
        return AB_ERR_PARAM;

    uint32_t addr = ab_partition_get_slot_addr(slot);
    uint32_t sp = (*(__IO uint32_t *)addr);

    if (!ab_is_valid_sp(sp))
    {
        printf("AB: slot %s invalid SP=0x%08lX\r\n", ab_slot_name(slot), (unsigned long)sp);
        return AB_ERR_SLOT_INVALID_FW;
    }

    uint32_t reset_handler = (*(__IO uint32_t *)(addr + 4));
    if (reset_handler < addr || reset_handler > ab_partition_get_slot_end_addr(slot))
    {
        printf("AB: slot %s invalid ResetHandler=0x%08lX\r\n",
               ab_slot_name(slot), (unsigned long)reset_handler);
        return AB_ERR_SLOT_INVALID_FW;
    }

    printf("AB: slot %s valid: SP=0x%08lX, Reset=0x%08lX\r\n",
           ab_slot_name(slot), (unsigned long)sp, (unsigned long)reset_handler);
    return AB_OK;
}

ab_err_t ab_partition_metadata_flush(void)
{
    if (!g_ab_metadata_dirty)
        return AB_OK;

    g_ab_metadata.crc32 = ab_compute_metadata_crc(&g_ab_metadata);
    ab_err_t err = ab_write_metadata_raw(&g_ab_metadata);
    if (err == AB_OK)
    {
        g_ab_metadata_dirty = 0;
        printf("AB: metadata flushed (instance %d)\r\n", g_ab_current_instance);
    }
    return err;
}

const ab_metadata_t *ab_partition_get_metadata(void)
{
    return &g_ab_metadata;
}

const char *ab_err_str(ab_err_t err)
{
    switch (err)
    {
    case AB_OK:
        return "OK";
    case AB_ERR_PARAM:
        return "Invalid parameter";
    case AB_ERR_METADATA_INVALID:
        return "Metadata invalid";
    case AB_ERR_METADATA_CRC:
        return "Metadata CRC error";
    case AB_ERR_FLASH_READ:
        return "Flash read error";
    case AB_ERR_FLASH_WRITE:
        return "Flash write error";
    case AB_ERR_FLASH_ERASE:
        return "Flash erase error";
    case AB_ERR_NO_VALID_SLOT:
        return "No valid slot available";
    case AB_ERR_SLOT_INVALID_FW:
        return "Slot firmware invalid";
    default:
        return "Unknown error";
    }
}

const char *ab_slot_name(ab_slot_t slot)
{
    switch (slot)
    {
    case AB_SLOT_A:
        return "A";
    case AB_SLOT_B:
        return "B";
    default:
        return "NONE";
    }
}
