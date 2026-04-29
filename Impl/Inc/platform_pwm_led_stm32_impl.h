#ifndef PLATFORM_PWM_LED_STM32_IMPL_H
#define PLATFORM_PWM_LED_STM32_IMPL_H

#include "platform_led.h"
#include "stm32f4xx_hal.h"

typedef struct {
    platform_led_base_t base;
    TIM_HandleTypeDef*  htim;
    uint32_t            channel;
    uint8_t             brightness;
} pwm_led_stm32_t;

#endif
