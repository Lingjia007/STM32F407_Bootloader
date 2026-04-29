#include "service_notify.h"
#include "stm32f4xx_hal.h"

void service_battery_alert(platform_led_base_t *led, uint8_t battery)
{
    if (!led)
        return;

    if (battery < 10)
    {
        for (int i = 0; i < 10; i++)
        {
            LED_ON(led);
            HAL_Delay(100);
            LED_OFF(led);
            HAL_Delay(100);
        }
    }
    else if (battery < 40)
    {
        LED_SET_BRIGHTNESS(led, 30);
        LED_ON(led);
    }
    else
    {
        LED_ON(led);
    }
}

void service_led_blink_pattern(platform_led_base_t *led, uint8_t pattern)
{
    if (!led)
        return;

    switch (pattern)
    {
    case 0:
        LED_OFF(led);
        break;
    case 1:
        LED_ON(led);
        break;
    case 2:
        LED_TOGGLE(led);
        break;
    default:
        break;
    }
}

const char *service_led_get_name(platform_led_base_t *led)
{
    if (!led)
        return NULL;
    return led->name;
}

platform_led_type_t service_led_get_type(platform_led_base_t *led)
{
    if (!led)
        return LED_TYPE_UNKNOWN;
    return led->type;
}

platform_led_state_t service_led_get_current_state(platform_led_base_t *led)
{
    if (!led)
        return LED_STATE_OFF;
    return led->state;
}
