#ifndef __ESP8266_DRIVER_H
#define __ESP8266_DRIVER_H

#include "main.h"
#include "esp8266_uart.h"

#define ESP8266_EOK 0
#define ESP8266_ERROR 1
#define ESP8266_ETIMEOUT 2
#define ESP8266_EINVAL 3

void esp8266_hw_reset(void);
uint8_t esp8266_send_at_cmd(char *cmd, char *ack, uint32_t timeout);
uint8_t esp8266_init(void);
uint8_t esp8266_restore(void);
uint8_t esp8266_at_test(void);
uint8_t esp8266_set_wifimode(uint8_t mode);
uint8_t esp8266_sw_reset(void);
uint8_t esp8266_ate_config(uint8_t cfg);
uint8_t esp8266_join_ap(char *ssid, char *pwd);
uint8_t esp8266_get_ip(char *buf);
uint8_t esp8266_connect_tcp_server(char *server_ip, char *server_port);
uint8_t esp8266_enter_unvarnished(void);
void esp8266_exit_unvarnished(void);

void esp8266_wifi_init(char *ssid, char *pwd, char *ip_buf);

#endif
