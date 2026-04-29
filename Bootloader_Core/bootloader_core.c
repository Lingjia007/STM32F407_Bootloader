#include "bootloader_core.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

bootloader_ctx_t bootloader_ctx = {
    .config = {
        .jump = {
            .app_jump_addr = APPLICATION_ADDRESS,
            .jump_func = jump_to_app,
        },
    },
};

#if defined(__CC_ARM)
uint32_t update_flag __attribute__((section("NoInit"), zero_init, used));
#else
volatile uint32_t update_flag __attribute__((used, section("NoInit")));
#endif

void jump_to_app(uint32_t app_address)
{
    pFunction jump_fn;
    uint32_t app_stack_ptr = (*(__IO uint32_t *)app_address);
    uint32_t app_reset_handler = (*(__IO uint32_t *)(app_address + 4));

    if ((app_stack_ptr & 0x2FFE0000) != 0x20000000)
    {
        return;
    }

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    HAL_RCC_DeInit();
    HAL_DeInit();

    SCB->VTOR = app_address;
    __DSB();
    __ISB();

    __set_MSP(app_stack_ptr);

    __enable_irq();

    jump_fn = (pFunction)app_reset_handler;
    jump_fn();

    while (1)
        ;
}

#define BOOTLOADER_BUFFER_SIZE 4096

static uint8_t bootloader_buffer[BOOTLOADER_BUFFER_SIZE] __attribute__((aligned(4)));

static bootloader_err_t storage_status_to_bootloader(int16_t status)
{
    switch (status)
    {
    case STORAGE_STATUS_OK:
        return BOOTLOADER_OK;
    case STORAGE_STATUS_ERROR:
        return BOOTLOADER_ERR_ABORT;
    case STORAGE_STATUS_PARAM:
        return BOOTLOADER_ERR_PARAM;
    case STORAGE_STATUS_OPEN_SRC:
        return BOOTLOADER_ERR_OPEN_SRC;
    case STORAGE_STATUS_OPEN_DST:
        return BOOTLOADER_ERR_OPEN_DST;
    case STORAGE_STATUS_READ:
        return BOOTLOADER_ERR_READ;
    case STORAGE_STATUS_WRITE:
        return BOOTLOADER_ERR_WRITE;
    case STORAGE_STATUS_CLOSE:
        return BOOTLOADER_ERR_CLOSE;
    case STORAGE_STATUS_ERASE:
        return BOOTLOADER_ERR_ERASE;
    case STORAGE_STATUS_VERIFY:
        return BOOTLOADER_ERR_VERIFY;
    default:
        return BOOTLOADER_ERR_ABORT;
    }
}

bootloader_err_t bootloader_download(const platform_storage_base_t *src_storage,
                                     const platform_storage_base_t *tgt_storage,
                                     const char *path)
{
    int16_t err;
    uint32_t total_size = 0;
    uint32_t bytes_read = 0;
    uint32_t total_read = 0;
    uint32_t offset = 0;

    if (src_storage == NULL || tgt_storage == NULL)
    {
        printf("bootloader_download: param null\r\n");
        return BOOTLOADER_ERR_PARAM;
    }

    if (src_storage->source_ops == NULL || tgt_storage->target_ops == NULL)
    {
        printf("bootloader_download: missing ops\r\n");
        return BOOTLOADER_ERR_PARAM;
    }

    printf("bootloader_download: opening source [%s]...\r\n", src_storage->name);
    err = STORAGE_SOURCE_OPEN(src_storage, path, &total_size);
    if (err != STORAGE_STATUS_OK)
    {
        printf("bootloader_download: src open failed err=%d\r\n", err);
        return storage_status_to_bootloader(err);
    }

    printf("bootloader_download: total_size=%lu, opening target [%s]...\r\n",
           (unsigned long)total_size, tgt_storage->name);
    err = STORAGE_TARGET_OPEN(tgt_storage, path, total_size);
    if (err != STORAGE_STATUS_OK)
    {
        printf("bootloader_download: tgt open failed err=%d\r\n", err);
        STORAGE_SOURCE_CLOSE(src_storage);
        return storage_status_to_bootloader(err);
    }

    printf("bootloader_download: starting download loop...\r\n");
    while (total_read < total_size)
    {
        uint32_t to_read = BOOTLOADER_BUFFER_SIZE;
        if (total_size - total_read < to_read)
        {
            to_read = total_size - total_read;
        }

        err = STORAGE_SOURCE_READ(src_storage, bootloader_buffer, to_read, &bytes_read);
        if (err != STORAGE_STATUS_OK)
        {
            printf("bootloader_download: src read failed err=%d\r\n", err);
            STORAGE_TARGET_CLOSE(tgt_storage);
            STORAGE_SOURCE_CLOSE(src_storage);
            return storage_status_to_bootloader(err);
        }

        if (bytes_read == 0)
        {
            printf("bootloader_download: bytes_read=0, breaking\r\n");
            break;
        }

        err = STORAGE_TARGET_WRITE(tgt_storage, offset, bootloader_buffer, bytes_read);
        if (err != STORAGE_STATUS_OK)
        {
            printf("bootloader_download: tgt write failed err=%d\r\n", err);
            STORAGE_TARGET_CLOSE(tgt_storage);
            STORAGE_SOURCE_CLOSE(src_storage);
            return storage_status_to_bootloader(err);
        }

        total_read += bytes_read;
        offset += bytes_read;
        printf("bootloader_download: progress %lu/%lu\r\n", (unsigned long)total_read, (unsigned long)total_size);
    }

    printf("bootloader_download: closing target...\r\n");
    err = STORAGE_TARGET_CLOSE(tgt_storage);
    if (err != STORAGE_STATUS_OK)
    {
        printf("bootloader_download: tgt close failed err=%d\r\n", err);
        STORAGE_SOURCE_CLOSE(src_storage);
        return storage_status_to_bootloader(err);
    }

    printf("bootloader_download: closing source...\r\n");
    err = STORAGE_SOURCE_CLOSE(src_storage);
    if (err != STORAGE_STATUS_OK)
    {
        printf("bootloader_download: src close failed err=%d\r\n", err);
        return storage_status_to_bootloader(err);
    }

    printf("bootloader_download: success\r\n");
    return BOOTLOADER_OK;
}
