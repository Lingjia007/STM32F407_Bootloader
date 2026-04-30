#ifndef SERVICE_WIFI_TRANSPORT_H
#define SERVICE_WIFI_TRANSPORT_H

#include "platform_wifi.h"
#include <stdint.h>

int wifi_transport_send_packet(platform_wifi_base_t *wifi, const uint8_t *buf, int buflen);
int wifi_transport_get_data(platform_wifi_base_t *wifi, uint8_t *buf, int count);
int wifi_transport_get_data_nb(platform_wifi_base_t *wifi, uint8_t *buf, int count);
int wifi_transport_open(platform_wifi_base_t *wifi, const char *addr, int port);
int wifi_transport_close(platform_wifi_base_t *wifi);

int wifi_http_request(platform_wifi_base_t *wifi, const char *host, uint16_t port,
                      const uint8_t *req, uint32_t req_len,
                      uint8_t *resp, uint32_t resp_cap, uint32_t timeout_ms);

#endif
