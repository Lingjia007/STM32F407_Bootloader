#include "esp8266_uart.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;

static struct
{
    uint8_t buf[ESP8266_UART_RX_BUF_SIZE];
    struct
    {
        uint16_t len    : 15;
        uint16_t finsh  : 1;
    } sta;
} g_uart_rx_frame = {0};

static uint8_t g_uart_tx_buf[ESP8266_UART_TX_BUF_SIZE];

void esp8266_uart_printf(char *fmt, ...)
{
    va_list ap;
    uint16_t len;
    
    va_start(ap, fmt);
    vsprintf((char *)g_uart_tx_buf, fmt, ap);
    va_end(ap);
    
    len = strlen((const char *)g_uart_tx_buf);
    HAL_UART_Transmit(&huart1, g_uart_tx_buf, len, HAL_MAX_DELAY);
}

void esp8266_uart_rx_restart(void)
{
    g_uart_rx_frame.sta.len     = 0;
    g_uart_rx_frame.sta.finsh   = 0;
}

uint8_t *esp8266_uart_rx_get_frame(void)
{
    if (g_uart_rx_frame.sta.finsh == 1)
    {
        g_uart_rx_frame.buf[g_uart_rx_frame.sta.len] = '\0';
        return g_uart_rx_frame.buf;
    }
    else
    {
        return NULL;
    }
}

uint16_t esp8266_uart_rx_get_frame_len(void)
{
    if (g_uart_rx_frame.sta.finsh == 1)
    {
        return g_uart_rx_frame.sta.len;
    }
    else
    {
        return 0;
    }
}

void esp8266_uart_init(uint32_t baudrate)
{
    (void)baudrate;
}

void ESP8266_UART_IRQHandler(void)
{
    uint8_t tmp;

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        (void)huart1.Instance->SR;
        (void)huart1.Instance->DR;
    }

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE) != RESET)
    {
        tmp = (uint8_t)(huart1.Instance->DR & 0xFF);

        if (g_uart_rx_frame.sta.len < (ESP8266_UART_RX_BUF_SIZE - 1))
        {
            g_uart_rx_frame.buf[g_uart_rx_frame.sta.len++] = tmp;
        }
        else
        {
            g_uart_rx_frame.sta.len = ESP8266_UART_RX_BUF_SIZE - 1;
        }
    }

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) != RESET)
    {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        g_uart_rx_frame.sta.finsh = 1;
    }
}
