#ifndef PLATFORM_GPIO_LED_STM32_IMPL_H
#define PLATFORM_GPIO_LED_STM32_IMPL_H

#include "platform_led.h"
#include "stm32f4xx_hal.h"
#include "main.h"

typedef struct
{
    platform_led_base_t base;
    GPIO_TypeDef *port;
    uint16_t pin;
} gpio_led_stm32_t;

void platform_gpio_led_stm32_register(gpio_led_stm32_t *led, GPIO_TypeDef *port, uint16_t pin, const char *name);

#endif
