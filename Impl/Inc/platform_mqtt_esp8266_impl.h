#ifndef PLATFORM_MQTT_ESP8266_IMPL_H
#define PLATFORM_MQTT_ESP8266_IMPL_H

#include "platform_mqtt.h"
#include "platform_wifi_esp8266_impl.h"

typedef struct
{
    platform_mqtt_base_t base;
    wifi_esp8266_t *wifi;
} mqtt_esp8266_t;

void platform_mqtt_esp8266_register(mqtt_esp8266_t *mqtt, wifi_esp8266_t *wifi, const char *name);

#endif
