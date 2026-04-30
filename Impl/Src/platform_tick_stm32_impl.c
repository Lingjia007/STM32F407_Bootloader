#include "platform_tick.h"
#include "stm32f4xx_hal.h"

static void stm32_tick_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static uint32_t stm32_tick_get_tick(void)
{
    return HAL_GetTick();
}

static const platform_tick_ops_t s_stm32_tick_ops = {
    .delay_ms = stm32_tick_delay_ms,
    .get_tick = stm32_tick_get_tick,
};

static platform_tick_base_t s_stm32_tick;

platform_tick_base_t *platform_tick_stm32_get_instance(void)
{
    if (s_stm32_tick.ops == NULL)
    {
        PLATFORM_TICK_INIT_BASE(&s_stm32_tick, &s_stm32_tick_ops, "stm32_tick");
    }
    return &s_stm32_tick;
}
