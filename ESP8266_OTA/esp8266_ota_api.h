#ifndef ESP8266_OTA_API_H
#define ESP8266_OTA_API_H

#include "service_onenet_ota.h"
#include "platform_config.h"
#include <stdint.h>

void esp8266_ota_init(void);
void esp8266_ota_set_target_internal_flash(void);
void esp8266_ota_set_target_sd_card(void);
void esp8266_ota_set_target_spi_flash(void);
int esp8266_ota_download(void);
int esp8266_ota_sync_time(void);

#endif
