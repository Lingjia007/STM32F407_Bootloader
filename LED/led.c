#include "led.h"
#include "service_notify.h"

uint16_t led_timer_counter = 0;

void led_control_task(void)
{
    uint32_t cycle_time = led_timer_counter % 2000;
    platform_led_base_t *led = &g_status_led.base;

    if (cycle_time < 1000)
    {
        uint32_t sub_cycle = cycle_time % 500;
        if (sub_cycle < 250)
        {
            LED_ON(led);
        }
        else
        {
            LED_OFF(led);
        }
    }
    else
    {
        LED_OFF(led);
    }
}
