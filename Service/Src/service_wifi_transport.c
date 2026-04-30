#include "service_wifi_transport.h"
#include "platform_wifi_esp8266_impl.h"
#include "platform_config.h"
#include <string.h>
#include <stdio.h>

static uint8_t s_transport_transparent_mode = 0;
static uint16_t s_transport_rx_offset = 0;

int wifi_transport_send_packet(platform_wifi_base_t *wifi, const uint8_t *buf, int buflen)
{
    if (wifi == NULL || buf == NULL || buflen <= 0)
        return -1;

    if (!s_transport_transparent_mode)
        return -1;

    wifi_esp8266_t *esp = container_of(wifi, wifi_esp8266_t, base);
    wifi_esp8266_uart_printf(esp, "%.*s", buflen, buf);
    return buflen;
}

int wifi_transport_get_data(platform_wifi_base_t *wifi, uint8_t *buf, int count)
{
    wifi_esp8266_t *esp = container_of(wifi, wifi_esp8266_t, base);
    uint16_t received = 0;
    uint32_t timeout = 5000;

    if (wifi == NULL || buf == NULL || count <= 0)
        return -1;

    if (!s_transport_transparent_mode)
        return -1;

    while (timeout > 0 && received < count)
    {
        uint8_t *rx_buf = wifi_esp8266_rx_get_frame(esp);
        if (rx_buf != NULL)
        {
            uint16_t frame_len = wifi_esp8266_rx_get_frame_len(esp);

            if (s_transport_rx_offset >= frame_len)
            {
                wifi_esp8266_rx_restart(esp);
                s_transport_rx_offset = 0;
                continue;
            }

            uint16_t available = frame_len - s_transport_rx_offset;
            uint16_t needed = count - received;
            uint16_t copy_len = (available < needed) ? available : needed;

            memcpy(buf + received, rx_buf + s_transport_rx_offset, copy_len);
            received += copy_len;
            s_transport_rx_offset += copy_len;

            if (s_transport_rx_offset >= frame_len)
            {
                wifi_esp8266_rx_restart(esp);
                s_transport_rx_offset = 0;
            }

            if (received >= count)
                break;
        }
        PLATFORM_DELAY_MS(g_tick, 1);
        timeout--;
    }

    if (received > 0)
        return received;
    if (timeout == 0)
        return 0;
    return -1;
}

int wifi_transport_get_data_nb(platform_wifi_base_t *wifi, uint8_t *buf, int count)
{
    wifi_esp8266_t *esp = container_of(wifi, wifi_esp8266_t, base);

    if (wifi == NULL || buf == NULL || count <= 0)
        return 0;

    if (!s_transport_transparent_mode)
        return 0;

    uint8_t *rx_buf = wifi_esp8266_rx_get_frame(esp);
    if (rx_buf != NULL)
    {
        uint16_t frame_len = wifi_esp8266_rx_get_frame_len(esp);

        if (s_transport_rx_offset >= frame_len)
        {
            wifi_esp8266_rx_restart(esp);
            s_transport_rx_offset = 0;
            return 0;
        }

        uint16_t available = frame_len - s_transport_rx_offset;
        uint16_t copy_len = (available < count) ? available : count;

        memcpy(buf, rx_buf + s_transport_rx_offset, copy_len);
        s_transport_rx_offset += copy_len;

        if (s_transport_rx_offset >= frame_len)
        {
            wifi_esp8266_rx_restart(esp);
            s_transport_rx_offset = 0;
        }

        return copy_len;
    }

    return 0;
}

int wifi_transport_open(platform_wifi_base_t *wifi, const char *addr, int port)
{
    if (wifi == NULL || addr == NULL)
        return -1;

    if (WIFI_CONNECT_TCP(wifi, addr, (uint16_t)port) != PLATFORM_WIFI_OK)
        return -1;

    PLATFORM_DELAY_MS(g_tick, 100);

    if (WIFI_ENTER_TRANSPARENT(wifi) != PLATFORM_WIFI_OK)
        return -1;

    PLATFORM_DELAY_MS(g_tick, 100);

    s_transport_transparent_mode = 1;

    wifi_esp8266_t *esp = container_of(wifi, wifi_esp8266_t, base);
    wifi_esp8266_rx_restart(esp);
    s_transport_rx_offset = 0;

    return 1;
}

int wifi_transport_close(platform_wifi_base_t *wifi)
{
    if (wifi == NULL)
        return -1;

    if (s_transport_transparent_mode)
    {
        WIFI_EXIT_TRANSPARENT(wifi);
        PLATFORM_DELAY_MS(g_tick, 100);
        WIFI_SEND_AT_CMD(wifi, "AT+CIPCLOSE", "OK", 1000);
        s_transport_transparent_mode = 0;
        s_transport_rx_offset = 0;
    }

    return 0;
}

int wifi_http_request(platform_wifi_base_t *wifi, const char *host, uint16_t port,
                      const uint8_t *req, uint32_t req_len,
                      uint8_t *resp, uint32_t resp_cap, uint32_t timeout_ms)
{
    if (wifi == NULL || host == NULL || req == NULL || resp == NULL || resp_cap == 0)
        return -1;

    int sock = wifi_transport_open(wifi, host, (int)port);
    if (sock < 0)
        return -1;

    if (wifi_transport_send_packet(wifi, req, (int)req_len) != (int)req_len)
    {
        wifi_transport_close(wifi);
        return -1;
    }

    uint32_t used = 0;
    uint32_t start = PLATFORM_GET_TICK(g_tick);
    uint32_t last_rx = start;

    while ((PLATFORM_GET_TICK(g_tick) - start) < timeout_ms)
    {
        if (used >= resp_cap)
            break;

        int n = wifi_transport_get_data_nb(wifi, resp + used, (int)(resp_cap - used));

        if (n > 0)
        {
            used += (uint32_t)n;
            last_rx = PLATFORM_GET_TICK(g_tick);
            continue;
        }

        if (used > 0 && (PLATFORM_GET_TICK(g_tick) - last_rx) > 300U)
            break;

        PLATFORM_DELAY_MS(g_tick, 10);
    }

    wifi_transport_close(wifi);

    return (int)used;
}
