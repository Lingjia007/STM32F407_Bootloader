#include "led.h"

uint16_t led_timer_counter = 0; // 定时器计数器 (ms)

/**
 * @brief  LED控制任务，实现2秒周期闪烁
 *         前1秒：亮250ms → 灭250ms → 亮250ms → 灭250ms
 *         后1秒：灭1秒
 * @param  None
 * @retval None
 */
void led_control_task(void)
{
    // 2秒周期 = 2000ms
    uint32_t cycle_time = led_timer_counter % 2000;

    if (cycle_time < 1000) // 前1秒
    {
        uint32_t sub_cycle = cycle_time % 500; // 每500ms一个子周期
        if (sub_cycle < 250)                   // 前250ms亮
        {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        }
        else // 后250ms灭
        {
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        }
    }
    else // 后1秒全灭
    {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    }
}
