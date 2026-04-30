#include "platform_wifi_esp8266_impl.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static void esp8266_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

void wifi_esp8266_uart_printf(wifi_esp8266_t *wifi, const char *fmt, ...)
{
    va_list ap;
    uint16_t len;

    va_start(ap, fmt);
    vsprintf((char *)wifi->tx_buf, fmt, ap);
    va_end(ap);

    len = strlen((const char *)wifi->tx_buf);
    UART_TRANSMIT(wifi->uart, wifi->tx_buf, len, HAL_MAX_DELAY);
}

void wifi_esp8266_rx_restart(wifi_esp8266_t *wifi)
{
    wifi->rx_frame.sta.len = 0;
    wifi->rx_frame.sta.finsh = 0;
}

uint8_t *wifi_esp8266_rx_get_frame(wifi_esp8266_t *wifi)
{
    if (wifi->rx_frame.sta.finsh == 1)
    {
        wifi->rx_frame.buf[wifi->rx_frame.sta.len] = '\0';
        return wifi->rx_frame.buf;
    }
    return NULL;
}

uint16_t wifi_esp8266_rx_get_frame_len(wifi_esp8266_t *wifi)
{
    if (wifi->rx_frame.sta.finsh == 1)
    {
        return wifi->rx_frame.sta.len;
    }
    return 0;
}

void wifi_esp8266_uart_irq_handler(wifi_esp8266_t *wifi)
{
    if (UART_GET_FLAG(wifi->uart, PLATFORM_UART_FLAG_ORE))
    {
        UART_CLEAR_FLAG(wifi->uart, PLATFORM_UART_FLAG_ORE);
        UART_READ_BYTE(wifi->uart);
    }

    if (UART_GET_FLAG(wifi->uart, PLATFORM_UART_FLAG_RXNE))
    {
        uint8_t tmp = UART_READ_BYTE(wifi->uart);
        if (wifi->rx_frame.sta.len < (ESP8266_UART_RX_BUF_SIZE - 1))
        {
            wifi->rx_frame.buf[wifi->rx_frame.sta.len++] = tmp;
        }
        else
        {
            wifi->rx_frame.sta.len = ESP8266_UART_RX_BUF_SIZE - 1;
        }
    }

    if (UART_GET_FLAG(wifi->uart, PLATFORM_UART_FLAG_IDLE))
    {
        UART_CLEAR_FLAG(wifi->uart, PLATFORM_UART_FLAG_IDLE);
        wifi->rx_frame.sta.finsh = 1;
    }
}

static int16_t esp8266_send_at_cmd_impl(wifi_esp8266_t *wifi, const char *cmd, const char *ack, uint32_t timeout)
{
    uint8_t *ret = NULL;

    wifi_esp8266_rx_restart(wifi);
    wifi_esp8266_uart_printf(wifi, "%s\r\n", cmd);

    if ((ack == NULL) || (timeout == 0))
    {
        return PLATFORM_WIFI_OK;
    }

    while (timeout > 0)
    {
        ret = wifi_esp8266_rx_get_frame(wifi);
        if (ret != NULL)
        {
            if (strstr((const char *)ret, ack) != NULL)
            {
                return PLATFORM_WIFI_OK;
            }
            else
            {
                wifi_esp8266_rx_restart(wifi);
            }
        }
        timeout--;
        esp8266_delay_ms(1);
    }

    return PLATFORM_WIFI_TIMEOUT;
}

static int16_t esp8266_wifi_init(void *ctx)
{
    (void)ctx;
    return PLATFORM_WIFI_OK;
}

static int16_t esp8266_wifi_deinit(void *ctx)
{
    (void)ctx;
    return PLATFORM_WIFI_OK;
}

static int16_t esp8266_at_test(void *ctx)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    for (uint8_t i = 0; i < 10; i++)
    {
        if (esp8266_send_at_cmd_impl(wifi, "AT", "OK", 500) == PLATFORM_WIFI_OK)
        {
            return PLATFORM_WIFI_OK;
        }
    }
    return PLATFORM_WIFI_ERROR;
}

static int16_t esp8266_set_mode(void *ctx, uint8_t mode)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d", mode);
    return esp8266_send_at_cmd_impl(wifi, cmd, "OK", 500);
}

static int16_t esp8266_join_ap(void *ctx, const char *ssid, const char *pwd)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    return esp8266_send_at_cmd_impl(wifi, cmd, "WIFI GOT IP", 10000);
}

static int16_t esp8266_get_ip(void *ctx, char *buf, uint16_t buf_len)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    (void)buf_len;

    int16_t ret = esp8266_send_at_cmd_impl(wifi, "AT+CIFSR", "OK", 500);
    if (ret != PLATFORM_WIFI_OK)
    {
        return PLATFORM_WIFI_ERROR;
    }

    uint8_t *frame = wifi_esp8266_rx_get_frame(wifi);
    char *p_start = strstr((const char *)frame, "STAIP,");
    if (p_start == NULL)
        return PLATFORM_WIFI_ERROR;
    p_start = strstr(p_start, "\"");
    if (p_start == NULL)
        return PLATFORM_WIFI_ERROR;
    char *p_end = strstr(p_start + 1, "\"");
    if (p_end == NULL)
        return PLATFORM_WIFI_ERROR;
    *p_end = '\0';
    sprintf(buf, "%s", p_start + 1);

    return PLATFORM_WIFI_OK;
}

static int16_t esp8266_connect_tcp(void *ctx, const char *host, uint16_t port)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    return esp8266_send_at_cmd_impl(wifi, cmd, "CONNECT", 5000);
}

static int16_t esp8266_disconnect_tcp(void *ctx)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    return esp8266_send_at_cmd_impl(wifi, "AT+CIPCLOSE", "OK", 1000);
}

static int16_t esp8266_enter_transparent(void *ctx)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    int16_t ret = esp8266_send_at_cmd_impl(wifi, "AT+CIPMODE=1", "OK", 500);
    if (ret != PLATFORM_WIFI_OK)
        return PLATFORM_WIFI_ERROR;
    ret = esp8266_send_at_cmd_impl(wifi, "AT+CIPSEND", ">", 500);
    return ret;
}

static int16_t esp8266_exit_transparent(void *ctx)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    esp8266_delay_ms(1000);
    wifi_esp8266_uart_printf(wifi, "+++");
    esp8266_delay_ms(1000);
    wifi_esp8266_rx_restart(wifi);
    esp8266_send_at_cmd_impl(wifi, "AT", "OK", 500);
    return PLATFORM_WIFI_OK;
}

static int16_t esp8266_send_at_cmd_ops(void *ctx, const char *cmd, const char *ack, uint32_t timeout)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    return esp8266_send_at_cmd_impl(wifi, cmd, ack, timeout);
}

static int16_t esp8266_sw_reset(void *ctx)
{
    wifi_esp8266_t *wifi = container_of(ctx, wifi_esp8266_t, base);
    int16_t ret = esp8266_send_at_cmd_impl(wifi, "AT+RST", "OK", 500);
    if (ret == PLATFORM_WIFI_OK)
    {
        esp8266_delay_ms(1000);
    }
    return ret;
}

static int16_t esp8266_hw_reset(void *ctx)
{
    (void)ctx;
    esp8266_delay_ms(100);
    esp8266_delay_ms(500);
    return PLATFORM_WIFI_OK;
}

static const platform_wifi_ops_t esp8266_wifi_ops = {
    .init = esp8266_wifi_init,
    .deinit = esp8266_wifi_deinit,
    .at_test = esp8266_at_test,
    .set_mode = esp8266_set_mode,
    .join_ap = esp8266_join_ap,
    .get_ip = esp8266_get_ip,
    .connect_tcp = esp8266_connect_tcp,
    .disconnect_tcp = esp8266_disconnect_tcp,
    .enter_transparent = esp8266_enter_transparent,
    .exit_transparent = esp8266_exit_transparent,
    .send_at_cmd = esp8266_send_at_cmd_ops,
    .sw_reset = esp8266_sw_reset,
    .hw_reset = esp8266_hw_reset,
};

void platform_wifi_esp8266_register(wifi_esp8266_t *wifi, platform_uart_base_t *uart, const char *name)
{
    if (wifi == NULL || uart == NULL)
    {
        return;
    }

    wifi->uart = uart;
    wifi->rx_frame.sta.len = 0;
    wifi->rx_frame.sta.finsh = 0;
    WIFI_INIT_BASE(&wifi->base, &esp8266_wifi_ops, name);
}
