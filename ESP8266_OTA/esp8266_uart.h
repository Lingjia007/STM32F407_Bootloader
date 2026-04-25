#ifndef __ESP8266_UART_H
#define __ESP8266_UART_H

#include "main.h"

#define ESP8266_UART_RX_BUF_SIZE 2048
#define ESP8266_UART_TX_BUF_SIZE 512

void esp8266_uart_printf(char *fmt, ...);
void esp8266_uart_rx_restart(void);
uint8_t *esp8266_uart_rx_get_frame(void);
uint16_t esp8266_uart_rx_get_frame_len(void);
void esp8266_uart_init(uint32_t baudrate);
void ESP8266_UART_IRQHandler(void);

#endif
