#include "bootloader_core.h"
#include "main.h"

bootloader_ctx_t bootloader_ctx = {
    .app_jump_addr = APPLICATION_ADDRESS,
    .jump_func = jump_to_app,
};

/**
 * @brief  Jump to user application
 * @param  None
 * @retval None
 */
void jump_to_app(uint32_t app_address)
{
    pFunction jump_fn;
    SCB->VTOR = app_address;
    __DSB();
    __ISB();
    __set_MSP(*(__IO uint32_t *)app_address);
    jump_fn = (pFunction)(*(__IO uint32_t *)(app_address + 4));

    __disable_irq();
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    HAL_RCC_DeInit();
    HAL_DeInit();
    __enable_irq();

    jump_fn();
}
