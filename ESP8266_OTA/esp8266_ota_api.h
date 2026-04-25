#ifndef ESP8266_OTA_API_H
#define ESP8266_OTA_API_H

void esp8266_ota_init(void);
void esp8266_ota_check_and_upgrade(void);
void esp8266_ota_set_task_id(const char *tid);

#endif
