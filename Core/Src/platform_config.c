#include "platform_config.h"
#include "bootloader_core.h"
#include "main.h"

gpio_led_stm32_t g_status_led;
w25q128_stm32_t g_w25q128_flash;
internal_flash_stm32_t g_internal_flash;
uart_stm32_t g_uart4_console;
uart_stm32_t g_usart1_esp8266;
fatfs_stm32_t g_fatfs_transport;
lfs_stm32_t g_lfs_transport;
rtc_stm32_t g_rtc;

extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart1;
extern RTC_HandleTypeDef hrtc;

void platform_config_init(void)
{
    platform_gpio_led_stm32_register(&g_status_led, LED_GPIO_Port, LED_Pin, "status_led");
    
    platform_w25q128_stm32_register(&g_w25q128_flash, &hspi1, GPIOA, GPIO_PIN_4, "w25q128");
    
    platform_internal_flash_stm32_register(&g_internal_flash, 
                                            APPLICATION_ADDRESS, 
                                            INTERNAL_FLASH_END_ADDR, 
                                            "internal_flash");
    
    platform_uart_stm32_register(&g_uart4_console, &huart4, "uart4_console");
    
    platform_uart_stm32_register(&g_usart1_esp8266, &huart1, "usart1_esp8266");
    
    platform_fatfs_stm32_register(&g_fatfs_transport, "fatfs");
    
    platform_lfs_stm32_register(&g_lfs_transport, "lfs");
    
    platform_rtc_stm32_register(&g_rtc, &hrtc, "rtc");
}
