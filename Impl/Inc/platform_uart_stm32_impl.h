#ifndef PLATFORM_UART_STM32_IMPL_H
#define PLATFORM_UART_STM32_IMPL_H

#include "platform_uart.h"
#include "stm32f4xx_hal.h"

typedef struct
{
    platform_uart_base_t base;
    UART_HandleTypeDef *huart;
} uart_stm32_t;

void platform_uart_stm32_register(uart_stm32_t *uart, UART_HandleTypeDef *huart, const char *name);

#endif
