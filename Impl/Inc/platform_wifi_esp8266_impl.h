#ifndef PLATFORM_WIFI_ESP8266_IMPL_H
#define PLATFORM_WIFI_ESP8266_IMPL_H

#include "platform_wifi.h"
#include "platform_uart.h"

#define ESP8266_UART_RX_BUF_SIZE 4096
#define ESP8266_UART_TX_BUF_SIZE 512

typedef struct
{
    platform_wifi_base_t base;
    platform_uart_base_t *uart;
    struct
    {
        uint8_t buf[ESP8266_UART_RX_BUF_SIZE];
        struct
        {
            uint16_t len : 15;
            uint16_t finsh : 1;
        } sta;
    } rx_frame;
    uint8_t tx_buf[ESP8266_UART_TX_BUF_SIZE];
} wifi_esp8266_t;

void platform_wifi_esp8266_register(wifi_esp8266_t *wifi, platform_uart_base_t *uart, const char *name);
void wifi_esp8266_uart_irq_handler(wifi_esp8266_t *wifi);
void wifi_esp8266_uart_printf(wifi_esp8266_t *wifi, const char *fmt, ...);
void wifi_esp8266_rx_restart(wifi_esp8266_t *wifi);
uint8_t *wifi_esp8266_rx_get_frame(wifi_esp8266_t *wifi);
uint16_t wifi_esp8266_rx_get_frame_len(wifi_esp8266_t *wifi);

#endif
