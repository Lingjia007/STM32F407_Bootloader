#include "platform_config.h"
#include "bootloader_core.h"
#include "ab_partition.h"
#include "main.h"

gpio_led_stm32_t g_status_led;
w25q128_stm32_t g_w25q128_flash;
internal_flash_stm32_t g_internal_flash;
internal_flash_stm32_t g_slot_a_flash;
internal_flash_stm32_t g_slot_b_flash;
internal_flash_stm32_t g_download_cache_flash;
uart_stm32_t g_uart4_console;
uart_stm32_t g_usart1_esp8266;
fatfs_stm32_t g_fatfs_transport;
lfs_stm32_t g_lfs_transport;
rtc_stm32_t g_rtc;
wifi_esp8266_t g_esp8266_wifi;
mqtt_esp8266_t g_esp8266_mqtt;
platform_tick_base_t *g_tick;

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
    
    platform_internal_flash_stm32_register(&g_slot_a_flash,
                                            SLOT_A_START_ADDR,
                                            SLOT_A_END_ADDR,
                                            "slot_a");
    
    platform_internal_flash_stm32_register(&g_slot_b_flash,
                                            SLOT_B_START_ADDR,
                                            SLOT_B_END_ADDR,
                                            "slot_b");
    g_slot_b_flash.relocate_offset = SLOT_B_START_ADDR - SLOT_A_START_ADDR;
    
    platform_internal_flash_stm32_register(&g_download_cache_flash,
                                            DOWNLOAD_CACHE_ADDR,
                                            DOWNLOAD_CACHE_ADDR + DOWNLOAD_CACHE_SIZE - 1,
                                            "download_cache");
    
    platform_uart_stm32_register(&g_uart4_console, &huart4, "uart4_console");
    
    platform_uart_stm32_register(&g_usart1_esp8266, &huart1, "usart1_esp8266");
    
    platform_fatfs_stm32_register(&g_fatfs_transport, "fatfs");
    
    platform_lfs_stm32_register(&g_lfs_transport, "lfs");
    
    platform_rtc_stm32_register(&g_rtc, &hrtc, "rtc");
    
    platform_wifi_esp8266_register(&g_esp8266_wifi, &g_usart1_esp8266.base, "esp8266_wifi");
    
    platform_mqtt_esp8266_register(&g_esp8266_mqtt, &g_esp8266_wifi, "esp8266_mqtt");
    
    g_tick = platform_tick_stm32_get_instance();
}
