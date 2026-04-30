#ifndef PLATFORM_RTC_STM32_IMPL_H
#define PLATFORM_RTC_STM32_IMPL_H

#include "platform_rtc.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    platform_rtc_base_t base;
    RTC_HandleTypeDef *hrtc;
} rtc_stm32_t;

void platform_rtc_stm32_register(rtc_stm32_t *rtc, RTC_HandleTypeDef *hrtc, const char *name);

#endif
