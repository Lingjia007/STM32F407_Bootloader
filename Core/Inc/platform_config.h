#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

#include "platform_gpio_led_stm32_impl.h"
#include "platform_w25q128_stm32_impl.h"
#include "platform_internal_flash_stm32_impl.h"
#include "platform_uart_stm32_impl.h"
#include "platform_fatfs_stm32_impl.h"
#include "platform_lfs_stm32_impl.h"
#include "platform_rtc_stm32_impl.h"

extern gpio_led_stm32_t g_status_led;
extern w25q128_stm32_t g_w25q128_flash;
extern internal_flash_stm32_t g_internal_flash;
extern uart_stm32_t g_uart4_console;
extern uart_stm32_t g_usart1_esp8266;
extern fatfs_stm32_t g_fatfs_transport;
extern lfs_stm32_t g_lfs_transport;
extern rtc_stm32_t g_rtc;

void platform_config_init(void);

#endif
