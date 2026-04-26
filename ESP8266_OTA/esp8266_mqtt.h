#ifndef __ESP8266_MQTT_H
#define __ESP8266_MQTT_H

#include "main.h"
#include <stdint.h>

#define MQTT_MAX_TOPIC_LEN  128
#define MQTT_MAX_PAYLOAD_LEN 512
#define MQTT_MAX_CLIENT_ID_LEN 64
#define MQTT_MAX_USERNAME_LEN 256
#define MQTT_MAX_PASSWORD_LEN 256

typedef struct
{
    char client_id[MQTT_MAX_CLIENT_ID_LEN];
    char username[MQTT_MAX_USERNAME_LEN];
    char password[MQTT_MAX_PASSWORD_LEN];
} mqtt_user_config_t;

typedef struct
{
    char id[32];
    char key[64];
    int value_int;
    float value_float;
    uint8_t value_type;
} mqtt_property_t;

#define MQTT_VALUE_TYPE_INT   0
#define MQTT_VALUE_TYPE_FLOAT 1
#define MQTT_VALUE_TYPE_BOOL  2
#define MQTT_VALUE_TYPE_STRING 3

uint8_t esp8266_mqtt_usercfg(uint8_t link_id, mqtt_user_config_t *config);
uint8_t esp8266_mqtt_connect(uint8_t link_id, const char *host, uint16_t port, uint8_t reconnect);
uint8_t esp8266_mqtt_disconnect(uint8_t link_id);
uint8_t esp8266_mqtt_subscribe(uint8_t link_id, const char *topic, uint8_t qos);
uint8_t esp8266_mqtt_unsubscribe(uint8_t link_id, const char *topic);
uint8_t esp8266_mqtt_publish_raw(uint8_t link_id, const char *topic, const char *payload, uint16_t payload_len, uint8_t qos, uint8_t retain);
uint8_t esp8266_mqtt_publish_property(uint8_t link_id, const char *product_id, const char *device_name, mqtt_property_t *props, uint8_t prop_count, const char *msg_id);
uint8_t esp8266_mqtt_publish_set_reply(uint8_t link_id, const char *product_id, const char *device_name, const char *msg_id, int code, const char *msg);
uint8_t esp8266_mqtt_check_connected(uint8_t link_id);

uint8_t esp8266_mqtt_recv_property_set(char *topic, char *payload, uint16_t max_payload_len, char *msg_id, mqtt_property_t *props, uint8_t max_props, uint8_t *prop_count);

uint8_t esp8266_mqtt_check_property_set_recv(char *topic, char *payload, uint16_t max_payload_len, char *msg_id);

#endif
