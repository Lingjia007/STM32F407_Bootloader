#include "esp8266_ota_config.h"
#include "esp8266_driver.h"
#include "onenet_ota.h"
#include <stdio.h>

static char g_ip_buf[16] = {0};

void esp8266_ota_init(void)
{
    printf("ESP8266 OTA Initializing...\r\n");
    
    esp8266_wifi_init(ESP8266_WIFI_SSID, ESP8266_WIFI_PASSWORD, g_ip_buf);
    
    printf("ESP8266 OTA Ready, IP: %s\r\n", g_ip_buf);
}

void esp8266_ota_check_and_upgrade(void)
{
#if ONENET_OTA_ENABLED
    printf("Checking for OTA upgrade...\r\n");
    ONENET_OTA_ProcessUpgrade();
#else
    printf("OTA is disabled\r\n");
#endif
}

void esp8266_ota_set_task_id(const char *tid)
{
    ONENET_OTA_SetTaskId(tid);
}
