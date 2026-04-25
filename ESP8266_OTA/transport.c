#include "transport.h"
#include "esp8266_driver.h"
#include "esp8266_uart.h"
#include "usart.h"
#include "stdio.h"
#include <string.h>

extern UART_HandleTypeDef huart1;

#define MQTT_ESP8266_SOCK_ID 1

static uint8_t transport_transparent_mode = 0;
static uint16_t transport_rx_offset = 0;

int transport_sendPacketBuffer(int sock, unsigned char *buf, int buflen)
{
    (void)sock;

    if (buf == NULL || buflen <= 0)
        return -1;

    if (!transport_transparent_mode)
        return -1;

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart1, buf, (uint16_t)buflen, 1000);
    if (status == HAL_OK)
        return buflen;
    return -1;
}

int transport_getdata(unsigned char *buf, int count)
{
    uint16_t received = 0;
    uint8_t *rx_buf;
    uint32_t timeout = 5000;

    if (buf == NULL || count <= 0)
        return -1;

    if (!transport_transparent_mode)
        return -1;

    while (timeout > 0 && received < count)
    {
        rx_buf = esp8266_uart_rx_get_frame();
        if (rx_buf != NULL)
        {
            uint16_t frame_len = esp8266_uart_rx_get_frame_len();

            if (transport_rx_offset >= frame_len)
            {
                esp8266_uart_rx_restart();
                transport_rx_offset = 0;
                continue;
            }

            uint16_t available = frame_len - transport_rx_offset;
            uint16_t needed = count - received;
            uint16_t copy_len = (available < needed) ? available : needed;

            memcpy(buf + received, rx_buf + transport_rx_offset, copy_len);
            received += copy_len;
            transport_rx_offset += copy_len;

            if (transport_rx_offset >= frame_len)
            {
                esp8266_uart_rx_restart();
                transport_rx_offset = 0;
            }

            if (received >= count)
                break;
        }
        HAL_Delay(1);
        timeout--;
    }

    if (received > 0)
        return received;
    if (timeout == 0)
        return 0;
    return -1;
}

int transport_getdatanb(void *sck, unsigned char *buf, int count)
{
    uint8_t *rx_buf;
    uint16_t frame_len;

    (void)sck;

    if (buf == NULL || count <= 0)
        return 0;

    if (!transport_transparent_mode)
        return 0;

    rx_buf = esp8266_uart_rx_get_frame();
    if (rx_buf != NULL)
    {
        frame_len = esp8266_uart_rx_get_frame_len();

        if (transport_rx_offset >= frame_len)
        {
            esp8266_uart_rx_restart();
            transport_rx_offset = 0;
            return 0;
        }

        uint16_t available = frame_len - transport_rx_offset;
        uint16_t copy_len = (available < count) ? available : count;

        memcpy(buf, rx_buf + transport_rx_offset, copy_len);
        transport_rx_offset += copy_len;

        if (transport_rx_offset >= frame_len)
        {
            esp8266_uart_rx_restart();
            transport_rx_offset = 0;
        }

        return copy_len;
    }

    return 0;
}

int transport_open(char *addr, int port)
{
    char port_str[8];

    if (addr == NULL)
        return -1;

    sprintf(port_str, "%d", port);

    if (esp8266_connect_tcp_server(addr, port_str) != ESP8266_EOK)
    {
        return -1;
    }

    HAL_Delay(100);

    if (esp8266_enter_unvarnished() != ESP8266_EOK)
    {
        return -1;
    }

    HAL_Delay(100);

    transport_transparent_mode = 1;

    esp8266_uart_rx_restart();
    transport_rx_offset = 0;

    return MQTT_ESP8266_SOCK_ID;
}

int transport_close(int sock)
{
    (void)sock;

    if (transport_transparent_mode)
    {
        esp8266_exit_unvarnished();
        HAL_Delay(100);

        esp8266_send_at_cmd("AT+CIPCLOSE", "OK", 1000);

        transport_transparent_mode = 0;
        transport_rx_offset = 0;
    }

    return 0;
}
