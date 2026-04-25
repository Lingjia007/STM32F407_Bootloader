#include "esp8266_driver.h"
#include <string.h>
#include <stdio.h>

static void esp8266_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void esp8266_hw_reset(void)
{
    esp8266_delay_ms(100);
    esp8266_delay_ms(500);
}

uint8_t esp8266_send_at_cmd(char *cmd, char *ack, uint32_t timeout)
{
    uint8_t *ret = NULL;

    esp8266_uart_rx_restart();
    esp8266_uart_printf("%s\r\n", cmd);

    if ((ack == NULL) || (timeout == 0))
    {
        return ESP8266_EOK;
    }
    else
    {
        while (timeout > 0)
        {
            ret = esp8266_uart_rx_get_frame();

            if (ret != NULL)
            {
                if (strstr((const char *)ret, ack) != NULL)
                {
                    return ESP8266_EOK;
                }
                else
                {
                    esp8266_uart_rx_restart();
                }
            }
            timeout--;
            esp8266_delay_ms(1);
        }

        return ESP8266_ETIMEOUT;
    }
}

uint8_t esp8266_init(void)
{
    uint8_t ret;

    ret = esp8266_sw_reset();

    return ret;
}

uint8_t esp8266_restore(void)
{
    uint8_t ret;

    ret = esp8266_send_at_cmd("AT+RESTORE", "ready", 3000);
    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_at_test(void)
{
    uint8_t ret;
    uint8_t i;

    for (i = 0; i < 10; i++)
    {
        ret = esp8266_send_at_cmd("AT", "OK", 500);
        if (ret == ESP8266_EOK)
        {
            return ESP8266_EOK;
        }
    }

    return ESP8266_ERROR;
}

uint8_t esp8266_set_wifimode(uint8_t mode)
{
    uint8_t ret;

    switch (mode)
    {
    case 0:
    {
        ret = esp8266_send_at_cmd("AT+CWMODE=0", "OK", 500);
        break;
    }
    case 1:
    {
        ret = esp8266_send_at_cmd("AT+CWMODE=1", "OK", 500);
        break;
    }
    case 2:
    {
        ret = esp8266_send_at_cmd("AT+CWMODE=2", "OK", 500);
        break;
    }
    case 3:
    {
        ret = esp8266_send_at_cmd("AT+CWMODE=3", "OK", 500);
        break;
    }
    default:
    {
        return ESP8266_EINVAL;
    }
    }

    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_sw_reset(void)
{
    uint8_t ret;

    ret = esp8266_send_at_cmd("AT+RST", "OK", 500);
    if (ret == ESP8266_EOK)
    {
        esp8266_delay_ms(1000);
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_ate_config(uint8_t cfg)
{
    uint8_t ret;

    switch (cfg)
    {
    case 0:
    {
        ret = esp8266_send_at_cmd("ATE0", "OK", 500);
        break;
    }
    case 1:
    {
        ret = esp8266_send_at_cmd("ATE1", "OK", 500);
        break;
    }
    default:
    {
        return ESP8266_EINVAL;
    }
    }

    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_join_ap(char *ssid, char *pwd)
{
    uint8_t ret;
    char cmd[64];

    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    ret = esp8266_send_at_cmd(cmd, "WIFI GOT IP", 10000);
    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_get_ip(char *buf)
{
    uint8_t ret;
    char *p_start;
    char *p_end;

    ret = esp8266_send_at_cmd("AT+CIFSR", "OK", 500);
    if (ret != ESP8266_EOK)
    {
        return ESP8266_ERROR;
    }

    p_start = strstr((const char *)esp8266_uart_rx_get_frame(), "\"");
    p_end = strstr(p_start + 1, "\"");
    *p_end = '\0';
    sprintf(buf, "%s", p_start + 1);

    return ESP8266_EOK;
}

uint8_t esp8266_connect_tcp_server(char *server_ip, char *server_port)
{
    uint8_t ret;
    char cmd[64];

    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%s", server_ip, server_port);
    ret = esp8266_send_at_cmd(cmd, "CONNECT", 5000);
    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

uint8_t esp8266_enter_unvarnished(void)
{
    uint8_t ret;

    ret = esp8266_send_at_cmd("AT+CIPMODE=1", "OK", 500);
    ret += esp8266_send_at_cmd("AT+CIPSEND", ">", 500);
    if (ret == ESP8266_EOK)
    {
        return ESP8266_EOK;
    }
    else
    {
        return ESP8266_ERROR;
    }
}

void esp8266_exit_unvarnished(void)
{
    esp8266_delay_ms(1000);
    esp8266_uart_printf("+++");
    esp8266_delay_ms(1000);
    esp8266_uart_rx_restart();
    esp8266_send_at_cmd("AT", "OK", 500);
}

void esp8266_wifi_init(char *ssid, char *pwd, char *ip_buf)
{
    uint8_t ret;

    printf("Initializing WiFi...\r\n");

    ret = esp8266_at_test();
    if (ret != ESP8266_EOK)
    {
        printf("ESP8266 AT test failed!\r\n");
        while (1)
        {
            HAL_Delay(200);
        }
    }

    printf("Checking if already connected...\r\n");
    ret = esp8266_get_ip(ip_buf);
    if (ret == ESP8266_EOK && ip_buf[0] != '\0')
    {
        printf("Already connected to WiFi!\r\n");
        printf("IP: %s\r\n", ip_buf);
        return;
    }

    printf("Not connected, connecting to AP...\r\n");

    ret = esp8266_set_wifimode(1);
    if (ret != ESP8266_EOK)
    {
        printf("Set WiFi mode failed!\r\n");
    }

    ret = esp8266_ate_config(0);
    if (ret != ESP8266_EOK)
    {
        printf("ATE config failed!\r\n");
    }

    ret = esp8266_join_ap(ssid, pwd);
    if (ret != ESP8266_EOK)
    {
        printf("Failed to connect to AP!\r\n");
        while (1)
        {
            HAL_Delay(200);
        }
    }

    ret = esp8266_get_ip(ip_buf);
    if (ret != ESP8266_EOK)
    {
        printf("Failed to get IP!\r\n");
        while (1)
        {
            HAL_Delay(200);
        }
    }

    printf("IP: %s\r\n", ip_buf);
}
